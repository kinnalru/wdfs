/* 
 *  this file is part of wdfs --> http://noedler.de/projekte/wdfs/
 *
 *  wdfs is a webdav filesystem with special features for accessing subversion
 *  repositories. it is based on fuse v2.5+ and neon v0.24.7+.
 * 
 *  copyright (c) 2005 - 2007 jens m. noedler, noedler@web.de
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *  This program is released under the GPL with the additional exemption
 *  that compiling, linking and/or using OpenSSL is allowed.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <termios.h>

#include <memory>

#include <boost/foreach.hpp>

#include <ne_alloc.h>
#include <ne_auth.h>
#include <ne_basic.h>
#include <ne_dates.h>
#include <ne_locks.h>
#include <ne_props.h>
#include <ne_redirect.h>
#include <ne_request.h>
#include <ne_session.h>
#include <ne_socket.h>
#include <ne_string.h>
#include <ne_uri.h>
#include <ne_utils.h>
#include <ne_xml.h>

#include "wdfs-main.h"
#include "webdav.h"
#include "log.h"


/* used to authorize at the webdav server */
struct ne_auth_data {
	const char *username;
	const char *password;
};

ne_session *session;
ne_lock_store *store = NULL;
struct ne_auth_data auth_data;


/* reads from the terminal without displaying the typed chars. used to type
 * the password savely. */
static int fgets_hidden(char *lineptr, size_t n, FILE *stream)
{
	struct termios settings_old, settings_new;
	int ret = 0;

	if (isatty(fileno(stream)))	{
		/* turn echoing off and fail if we can't */
		if (tcgetattr(fileno(stream), &settings_old) == 0) {
			settings_new = settings_old;
			settings_new.c_lflag &= ~ECHO;
			if (tcsetattr(fileno(stream), TCSAFLUSH, &settings_new) == 0)
				ret = 1;
		}
	}
	else
		ret = 1;

	/* read the password */
	if (ret == 1 &&	fgets(lineptr, n, stream) == NULL)
		ret = 0;

	if (isatty(fileno(stream)))
		/* restore terminal */
		(void) tcsetattr(fileno(stream), TCSAFLUSH, &settings_old);

	return ret;
}


/* this method is envoked, if the server requires authentication */
static int ne_set_server_auth_callback(
	void *userdata, const char *realm, 
	int attempt, char *username, char *password)
{
	const size_t length = 100;
	char buffer[length];

	/* ask the user for the username and password if needed */
	if (auth_data.username == NULL) {
		printf("username: ");
		if (fgets(buffer, length, stdin)) {
			int len = strlen(buffer);
			if (buffer[len - 1] == '\n')
				buffer[len - 1] = '\0';
			auth_data.username = strdup(buffer);
		}
	}
	if (auth_data.password == NULL) {
		printf("password: ");
		if (fgets_hidden(buffer, length, stdin)) {
			int len = strlen(buffer);
			if (buffer[len - 1] == '\n')
				buffer[len - 1] = '\0';
			auth_data.password = strdup(buffer);
		}
		printf("\n");
	}

	assert(auth_data.username && auth_data.password);
	strncpy(username, auth_data.username, NE_ABUFSIZ);
	strncpy(password, auth_data.password, NE_ABUFSIZ);

	return attempt;
}


/* this is called from ne_ssl_set_verify() if there is something wrong with the 
 * ssl certificate.  */
static int verify_ssl_certificate(
	void *userdata, int failures, const ne_ssl_certificate *certificate)
{
	ne_uri *uri = (ne_uri*)userdata;
	char from[NE_SSL_VDATELEN], to[NE_SSL_VDATELEN];
	const char *ident;

	ident = ne_ssl_cert_identity(certificate);

	if (ident) {
		fprintf(stderr,
			"WARNING: untrusted server certificate for '%s':\n", ident);
	}

	if (failures & NE_SSL_IDMISMATCH) {
		fprintf(stderr,
			" certificate was issued to hostname '%s' rather than '%s'\n", 
			ne_ssl_cert_identity(certificate), uri->host);
		fprintf(stderr, " this connection could have been intercepted!\n");
	}

	ne_ssl_cert_validity(certificate, from, to);
	printf(" certificate is valid from %s to %s\n", from, to);
	if (failures & NE_SSL_EXPIRED)
		fprintf(stderr, " >> certificate expired! <<\n");

	char *issued_to = ne_ssl_readable_dname(ne_ssl_cert_subject(certificate));
	char *issued_by = ne_ssl_readable_dname(ne_ssl_cert_issuer(certificate));
	printf(" issued to: %s\n", issued_to);
	printf(" issued by: %s\n", issued_by);
	free_chars(&issued_to, &issued_by, NULL);

	/* don't prompt the user if the parameter "-ac" was passed to wdfs */
	if (wdfs_cfg.accept_certificate == true)
		return 0;

	/* prompt the user wether he/she wants to accept this certificate */
	int answer;
	while (1) {
		printf(" do you wish to accept this certificate? (y/n) ");
		answer = getchar();
		/* delete the input buffer (if the char is not a newline) */
		if (answer != '\n')
			while (getchar() != '\n');
		/* stop asking if the answer was 'y' or 'n' */ 
		if (answer == 'y' || answer == 'n')
			break;
	}

	if (answer == 'y') {
		return 0;
	} else {
		printf(" certificate rejected.\n");
		return -1;
	}
}


/* sets up a webdav connection. if the servers needs authentication, the passed
 * parameters username and password are used. if they were not passed they can
 * be entered interactively. this method returns 0 on success or -1 on error. */
int setup_webdav_session(
	   ne_uri& uri, const std::string& username, const std::string& password)
{
    assert(uri.scheme && uri.host && uri.path);    

	auth_data.username = username.c_str();
	auth_data.password = password.c_str();

	/* if no port was defined use the default port */
	uri.port = uri.port ? uri.port : ne_uri_defaultport(uri.scheme);

	ne_debug_init(stderr,0);

	/* needed for ssl connections. it's not documented. nice to know... ;-) */
	ne_sock_init();

	/* create a session object, that allows to access the server */
	session = ne_session_create(uri.scheme, uri.host, uri.port);

	/* init ssl if needed */
	if (!strcasecmp(uri.scheme, "https")) {
#if NEON_VERSION >= 25
		if (ne_has_support(NE_FEATURE_SSL)) {
#else
		if (ne_supports_ssl()) {
#endif
			ne_ssl_trust_default_ca(session);
			ne_ssl_set_verify(session, verify_ssl_certificate, &uri);
		} else {
			fprintf(stderr, "## error: neon ssl support is not enabled.\n");
			ne_session_destroy(session);
			return -1;
		}
	}

	/* enable this for on-demand authentication */
	ne_set_server_auth(session, ne_set_server_auth_callback, NULL);

	/* enable redirect support */
	ne_redirect_register(session);

	/* escape the path for the case that it contains special chars */
	char *path = unify_path(uri.path, ESCAPE | LEAVESLASH);
	if (path == NULL) {
		printf("## error: unify_path() returned NULL\n");
		ne_session_destroy(session);
		return -1;
	}

	ne_set_useragent(session,"wdfs");

	/* try to access the server */
	ne_server_capabilities capabilities;
	int ret = ne_options(session, path, &capabilities);
	if (ret != NE_OK) {
		fprintf(stderr,
			"## error: could not mount remote server '%s'. ", uri.host);
		fprintf(stderr, "reason: %s", ne_get_error(session));
		/* if we got a redirect, print the new destination uri and exit */
		if (ret == NE_REDIRECT) {
			const ne_uri *new_uri = ne_redirect_location(session);
			char *new_uri_string = ne_uri_unparse(new_uri);
			fprintf(stderr, " to '%s'", new_uri_string);
			FREE(new_uri_string);
		}
		fprintf(stderr, ".\n");
		ne_session_destroy(session);
		FREE(path);
		return -1;
	}
	FREE(path);

	/* is this a webdav server that fulfills webdav class 1? */
	if (capabilities.dav_class1 != 1) {
		fprintf(stderr, 
			"## error: '%s' is not a webdav enabled server.\n", uri.host);
		ne_session_destroy(session);
		return -1;
	}

	/* set a useragent string, to identify wdfs in the server log files */
	ne_set_useragent(session, project_name);

	/* save the remotepath, because each fuse callback method need it to 
	 * access the files at the webdav server */
	//remotepath_basedir = remove_ending_slashes(uri.path);
	remotepath_basedir = uri.path;
	if (remotepath_basedir == NULL) {
		ne_session_destroy(session);
		return -1;
	}

	return 0;
}


/* +++++++ locking methods +++++++ */

/* returns the lock for this file from the lockstore on success 
 * or NULL if the lock is not found in the lockstore. */
static struct ne_lock* get_lock_by_path(const char *remotepath)
{
	assert(remotepath);

	/* unless the lockstore is initialized, no lock can be found */
	if (store == NULL)
		return NULL;

	/* generate a ne_uri object to find the lock by its uri */
	ne_uri uri;
	uri.path = (char *)remotepath;
	ne_fill_server_uri(session, &uri);

	/* find the lock for this uri in the lockstore */
	struct ne_lock *lock = NULL;
	lock = ne_lockstore_findbyuri(store, &uri);

	/* ne_fill_server_uri() malloc()d these fields, time to free them */
	free_chars(&uri.scheme, &uri.host, NULL);

	return lock;
}


/* tries to lock the file and returns 0 on success and 1 on error */
int lockfile(const char *remotepath, const int timeout)
{
	assert(remotepath && timeout);

	/* initialize the lockstore, if needed (e.g. first locking a file). */
	if (store == NULL) {
		store = ne_lockstore_create();
		if (store == NULL)
			return 1;
		ne_lockstore_register(store, session);
	}


	/* check, if we already hold a lock for this file */
	struct ne_lock *lock = get_lock_by_path(remotepath);

	/* we already hold a lock for this file, simply return 0 */
	if (lock != NULL) {
		if (wdfs_cfg.debug == true)
			fprintf(stderr, "++ file '%s' is already locked.\n", remotepath);
		return 0;
	}

	/* otherwise lock the file exclusivly */
	lock = ne_lock_create();
	enum ne_lock_scope scope = ne_lockscope_exclusive;
	lock->scope	= scope;
	lock->owner = ne_concat("wdfs, user: ", getenv("USER"), NULL);
	lock->timeout = timeout;
	lock->depth = NE_DEPTH_ZERO;
	ne_fill_server_uri(session, &lock->uri);
	lock->uri.path = ne_strdup(remotepath);

	if (ne_lock(session, lock)) {
		fprintf(stderr, "## ne_lock() error:\n");
		fprintf(stderr, "## could _not_ lock file '%s'.\n", lock->uri.path);
		ne_lock_destroy(lock);
		return 1;
	} else {
		ne_lockstore_add(store, lock);
		if (wdfs_cfg.debug == true)
			fprintf(stderr, "++ locked file '%s'.\n", remotepath);
	}

	return 0;
}


/* tries to unlock the file and returns 0 on success and 1 on error */
int unlockfile(const char *remotepath)
{
	assert(remotepath);

	struct ne_lock *lock = get_lock_by_path(remotepath);

	/* if the lock was not found, the file is already unlocked */
	if (lock == NULL)
		return 0;


	/* if the lock was found, unlock the file */
	if (ne_unlock(session, lock)) {
		fprintf(stderr, "## ne_unlock() error:\n");
		fprintf(stderr, "## could _not_ unlock file '%s'.\n", lock->uri.path);
		ne_lock_destroy(lock);
		return 1;
	} else {
		/* on success remove the lock from the store and destroy the lock */
		ne_lockstore_remove(store, lock);
		ne_lock_destroy(lock);
		if (wdfs_cfg.debug == true)
			fprintf(stderr, "++ unlocked file '%s'.\n", remotepath);
	}

	return 0;
}


/* this method unlocks all files of the lockstore and destroys the lockstore */
void unlock_all_files()
{
	/* only unlock all files, if the lockstore is initialized */
	if (store != NULL) {
		/* get each lock from the lockstore and try to unlock the file */
		struct ne_lock *this_lock = NULL;
		this_lock = ne_lockstore_first(store);
		while (this_lock != NULL) {
			if (ne_unlock(session, this_lock)) {
				fprintf(stderr,
					"## ne_unlock() error:\n"
				 	"## could _not_ unlock file '%s'.\n", this_lock->uri.path);
			} else {
				if (wdfs_cfg.debug == true)
					fprintf(stderr,
						"++ unlocked file '%s'.\n", this_lock->uri.path);
			}
			/* get the next lock from the lockstore */
			this_lock = ne_lockstore_next(store);
		}

		/* finally destroy the lockstore */
		if (wdfs_cfg.debug == true)
			fprintf(stderr, "++ destroying lockstore.\n");
		ne_lockstore_destroy(store);
	}
}

inline void fill_resource(webdav_resource_t* resource, ne_request* request) {
    const char *lastmodified = ne_get_response_header(request, "Last-Modified");
    if (!lastmodified) lastmodified = ne_get_response_header(request, "Date");

    resource->stat().st_mtime = (lastmodified)
        ? ne_rfc1123_parse(lastmodified)
        : 0;
        
    const char *contentlength = ne_get_response_header(request, "Content-Length");
    
    resource->stat().st_size = (contentlength)
        ? atoll(contentlength)
        : 0;
}

void create_request_handler(ne_request *req, void *userdata, const char *method, const char *requri)
{
//     webdav_context_t* ctx = reinterpret_cast<webdav_context_t*>(userdata);

}

inline void set_stat(struct stat& stat, ne_request* request) {
    const char *lastmodified = ne_get_response_header(request, "Last-Modified");
    if (!lastmodified) lastmodified = ne_get_response_header(request, "Date");

    stat.st_mtime = (lastmodified)
        ? ne_rfc1123_parse(lastmodified)
        : 0;
        
    const char *contentlength = ne_get_response_header(request, "Content-Length");
    
    stat.st_size = (contentlength)
        ? atoll(contentlength)
        : 0;
}

int post_send_handler(ne_request* request, void* userdata, const ne_status* status)
{
    webdav_context_t* ctx = reinterpret_cast<webdav_context_t*>(userdata);
    set_stat(ctx->stat, request);
    return NE_OK;
}

int get_head(ne_session* session, const std::string& path, webdav_resource_t* resource)
{
    std::shared_ptr<ne_request> request(
        ne_request_create(session, "HEAD", path.c_str()),
        ne_request_destroy
    );

    int neon_stat = ne_request_dispatch(request.get());

    if (neon_stat != NE_OK) {
        return neon_stat;
    }

    fill_resource(resource, request.get());

    return 0;
}







const char* get_helper(const ne_prop_result_set *results, field_e field) {
    const char* data = ne_propset_value(results, &prop_names[field]);
    return (data) 
        ? data
        : ne_propset_value(results, &anonymous_prop_names[field]);
}

void set_stat(struct stat& stat, const ne_prop_result_set* results)
{
    LOG_ENEX("", "");

    const char *resourcetype, *contentlength, *lastmodified, *creationdate, *executable, *modestr/*, *etagstr*/;

    assert(results);

    /* get the values from the propfind result set */
    resourcetype    = get_helper(results, TYPE);
    contentlength   = get_helper(results, LENGTH);
    lastmodified    = get_helper(results, MODIFIED);
    creationdate    = get_helper(results, CREATION);
    executable      = get_helper(results, EXECUTE);
    modestr         = get_helper(results, PERMISSIONS);
//     etagstr         = get_helper(results, ETAG);

    int mode = 0;

    /* webdav collection == directory entry */
    if (resourcetype != NULL && !strstr("<collection", resourcetype)) {
        /* "DT_DIR << 12" equals "S_IFDIR" */
        mode = (modestr) ? atoi(modestr) : 0777;
        mode |= S_IFDIR;
        stat.st_size = 4096;
        wdfs_dbg("collection\n");
    } else {
        mode = (modestr) ? atoi(modestr) : 0666;
        mode |= S_IFREG;
        stat.st_size = (contentlength) ? atoll(contentlength) : 0;
        wdfs_dbg("regular file\n");
    }

    stat.st_mode = mode;

    stat.st_nlink = 1;
    stat.st_atime = time(NULL);

    if (lastmodified != NULL)
        stat.st_mtime = ne_rfc1123_parse(lastmodified);
    else
        stat.st_mtime = 0;

    if (creationdate != NULL)
        stat.st_ctime = ne_iso8601_parse(creationdate);
    else
        stat.st_ctime = 0;

    /* calculate number of 512 byte blocks */
    stat.st_blocks  = (stat.st_size + 511) / 512;

    /* no need to set a restrict mode, because fuse filesystems can
        * only be accessed by the user that mounted the filesystem.  */
    stat.st_mode &= ~umask(0);
    stat.st_uid = getuid();
    stat.st_gid = getgid();
}

int handle_redirect(std::shared_ptr<char>& remotepath)
{
    wdfs_dbg("%s(%s)\n", __func__, remotepath.get());

    /* get the current_uri and new_uri structs */
    ne_uri current_uri;
    ne_fill_server_uri(session, &current_uri);
    const ne_uri *new_uri = ne_redirect_location(session);

    if (strcasecmp(current_uri.host, new_uri->host)) {
        fprintf(stderr,
            "## error: wdfs does not support redirecting to another host!\n");
        free_chars(&current_uri.host, &current_uri.scheme, NULL);
        return -1;
    }

    /* can't use ne_uri_free() here, because only host and scheme are mallocd */
    free_chars(&current_uri.host, &current_uri.scheme, NULL);

    /* set the new remotepath to the redirect target path */
    remotepath.reset(ne_strdup(new_uri->path));
    return 0;
}


struct getattrs_ctx_t {
  getattrs_ctx_t(const wdfs_controller_t& w) : wdfs(w) {}

  const wdfs_controller_t& wdfs;
  stats_t stats;
};

/* this method is called by ne_simple_propfind() from wdfs_getattr() for a
 * specific file. it sets the file's attributes and and them to the cache. */
static void webdav_getattrs_propfind_callback(
#if NEON_VERSION >= 26
    void *userdata, const ne_uri* href_uri, const ne_prop_result_set *results)
#else
    void *userdata, const char *remotepath, const ne_prop_result_set *results)
#endif
{

#if NEON_VERSION >= 26
    string_p remotepath(ne_uri_unparse(href_uri), free);
#else
    string_p remotepath(strdup(remotepath0), free);
#endif

    LOG_ENEX(remotepath.get(), "");

    getattrs_ctx_t* ctx = reinterpret_cast<getattrs_ctx_t*>(userdata);
    struct stat& stat = ctx->stats[ctx->wdfs.remote2full(remotepath.get()).get()];
    
    wdfs_dbg("%s -> %s\n", remotepath.get(), ctx->wdfs.remote2full(remotepath.get()).get());  

    set_stat(stat, results);
}

stats_t webdav_getattrs(ne_session* session, string_p remotepath, const wdfs_controller_t& wdfs)
{
    getattrs_ctx_t ctx(wdfs);

    LOG_ENEX(remotepath.get(), "");  
    
    int ret = ne_simple_propfind(
        session, canonicalize(remotepath.get(), ESCAPE).c_str(), NE_DEPTH_ZERO, &prop_names[0],
        webdav_getattrs_propfind_callback, &ctx);
    
    /* handle the redirect and retry the propfind with the new target */
    if (ret == NE_REDIRECT && wdfs_cfg.redirect == true) {
        if (handle_redirect(remotepath))
            throw webdav_exception_t(std::string("WEBDAV error in ") + __func__, -ENOENT);
        
        ret = ne_simple_propfind(
            session, remotepath.get(), NE_DEPTH_ZERO, &prop_names[0],
            webdav_getattrs_propfind_callback, &ctx);
    }

    if (ret != NE_OK) {
        throw webdav_exception_t(std::string("WEBDAV error in ") + __func__ + " :" + ne_get_error(session), -ENOENT);
    }

    return ctx.stats;
}


struct readdir_ctx_t {
  readdir_ctx_t(const wdfs_controller_t& w, string_p& rp) : wdfs(w), fulldir(rp) {}

  const wdfs_controller_t& wdfs;
  string_p& fulldir;
  stats_t stats;
};


/* this method is called by ne_simple_propfind() from wdfs_readdir() for each 
 * member (file) of the requested collection. this method extracts the file's
 * attributes from the webdav response, adds it to the cache and calls the fuse
 * filler method to add the file to the requested directory. */
static void wdfs_readdir_propfind_callback(
#if NEON_VERSION >= 26
    void *userdata, const ne_uri* href_uri, const ne_prop_result_set *results)
#else
    void *userdata, const char *remotepath0, const ne_prop_result_set *results)
#endif
{
#if NEON_VERSION >= 26
    string_p remotepath(ne_uri_unparse(href_uri), free);
#else
    string_p remotepath(strdup(remotepath0), free);
#endif

    LOG_ENEX(remotepath, "");  
  
    struct readdir_ctx_t *ctx = reinterpret_cast<readdir_ctx_t*>(userdata);
    assert(ctx);

    string_p fullpath(ctx->wdfs.remote2full(remotepath.get()));
    
    wdfs_dbg("fullpath: [%s]\n", fullpath.get());
    wdfs_dbg("fulldir: [%s]\n", ctx->fulldir.get());
    
    if (!fullpath || !ctx->fulldir) {
        wdfs_err("fatal error: unify_path() returned NULL\n");
        return;
    }

    /* don't add this directory to itself */
    if (strcmp(fullpath.get(), ctx->fulldir.get()) == 0) {
        wdfs_dbg("paths are same: nothing to do\n");
        return;
    }

    /* set this file's attributes. the "ne_prop_result_set *results" contains
        * the file attributes of all files of this collection (directory). this 
        * performs better then single requests for each file in getattr().  */

    struct stat& stat = ctx->stats[fullpath.get()];
    
    wdfs_dbg("%s -> %s\n", remotepath.get(), fullpath.get());  

    set_stat(stat, results);
}

stats_t webdav_readdir(ne_session* session, string_p fulldir, const wdfs_controller_t& wdfs)
{
    readdir_ctx_t ctx(wdfs, fulldir);
    LOG_ENEX(fulldir.get(), "");  
    
    int ret = ne_simple_propfind(
        session, ctx.fulldir.get(), NE_DEPTH_ONE,
        &prop_names[0], wdfs_readdir_propfind_callback, &ctx);
    /* handle the redirect and retry the propfind with the redirect target */
 
    if (ret == NE_REDIRECT && wdfs_cfg.redirect == true) {
        if (handle_redirect(ctx.fulldir))
            throw webdav_exception_t(std::string("WEBDAV error in ") + __func__, -ENOENT);

        ret = ne_simple_propfind(
            session, ctx.fulldir.get(), NE_DEPTH_ONE,
            &prop_names[0], wdfs_readdir_propfind_callback, &ctx);
    }
    
    if (ret != NE_OK) {
        throw webdav_exception_t(std::string("WEBDAV error in ") + __func__ + " :" + ne_get_error(session), -ENOENT);
    }
    
    return ctx.stats;
}

stat_t webdav_head(ne_session* session, string_p fullpath)
{
    LOG_ENEX(fullpath.get(), "");
    std::shared_ptr<ne_request> request(
        ne_request_create(session, "HEAD", canonicalize(fullpath, ESCAPE).get()),
        ne_request_destroy
    );

    int ret = ne_request_dispatch(request.get());

    if (ret != NE_OK) {
        throw webdav_exception_t(std::string("WEBDAV error in ") + __func__ + " :" + ne_get_error(session), -ENOENT);
    }

    stat_t st;
    set_stat(st, request.get());

    return st;
}

std::unique_ptr<fuse_file_t> webdav_get(ne_session* session, string_p fullpath, const std::string& cachedfile)
{
    LOG_ENEX(fullpath.get(), "");
    
    std::vector<char> tmpfile(cachedfile.begin(), cachedfile.end());
    tmpfile.push_back('.');
    tmpfile.insert(tmpfile.end(), 6, 'X');
    tmpfile.push_back('\0');
    
    int fd = ::mkstemp(tmpfile.data());
    std::unique_ptr<fuse_file_t> file(new fuse_file_t(tmpfile.data(), fd));
    
    webdav_context_t ctx{session};  
    hook_helper_t hooker(session, &ctx);

    wdfs_dbg("Getting file %s -> %s\n", fullpath.get(), file->path.c_str());
    
    /* GET the data to the filehandle even if the file is opened O_WRONLY,
    * because the opening application could use pwrite() or use O_APPEND
    * and than the data needs to be present. */   
    std::string remotepath = canonicalize(fullpath.get(), ESCAPE);
    if (ne_get(session, remotepath.c_str(), file->fd)) {
        throw webdav_exception_t(std::string("GET error in ") + __func__ + ":" + ne_get_error(session), -ENOENT);
    }

    struct utimbuf time; 
    time.actime = 0;
    time.modtime = ctx.stat.st_mtime;
    
    if (::utime(file->path.c_str(), &time)) {
        throw api_exception_t("utime() error for " + file->path, errno);
    }
    
    return file;
}

void webdav_put(ne_session* session, string_p fullpath, const std::unique_ptr< fuse_file_t >& file, const wdfs_controller_t& wdfs)
{
    LOG_ENEX(fullpath.get(), "");
     
    webdav_context_t ctx{session};  
    hook_helper_t hooker(session, &ctx);
    
    wdfs_dbg("Putting file %s -> %s\n", fullpath.get(), file->path.c_str());  
    
    std::string remotepath = canonicalize(fullpath.get(), ESCAPE);
    if (ne_put(session, remotepath.c_str(), file->fd)) {
        throw webdav_exception_t(std::string("GET error in ") + __func__ + ":" + ne_get_error(session), -EIO);
    }
    
//     sleep(5);
    
    std::cerr << "p1" << std::endl;
    
    struct stat st = webdav_head(session, fullpath);
    
    std::cerr << "p2" << std::endl;
    
    struct utimbuf time; 
    time.actime = 0;
    time.modtime = st.st_mtime;
    
    wdfs_dbg("       ======= SETTING FILE MODTIME: %d\n", time.modtime);  
    
    if (::utime(file->path.c_str(), &time)) {
        throw api_exception_t("utime() error for " + file->path, errno);
    }
}



