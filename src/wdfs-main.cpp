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

#include <string.h>
#include <errno.h>
#include <assert.h>
#include <glib.h>
#include <fuse_opt.h>
#include <ne_props.h>
#include <ne_dates.h>
#include <ne_redirect.h>

#include <memory>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>

#include "wdfs-main.h"
#include "webdav.h"
#include "cache.h"



/* there are four locking modes available. the simple locking mode locks a file 
 * on open()ing it and unlocks it on close()ing the file. the advanced mode 
 * prevents data curruption by locking the file on open() and holds the lock 
 * until the file was writen and closed or the lock timed out. the eternity 
 * mode holds the lock until wdfs is unmounted or the lock times out. the last
 * mode is to do no locking at all which is the default behaviour. */
#define NO_LOCK 0
#define SIMPLE_LOCK 1
#define ADVANCED_LOCK 2
#define ETERNITY_LOCK 3

std::unique_ptr<cache_t> cache;

static void print_help();
static int call_fuse_main(struct fuse_args *args);

/* define package name and version if config.h is not available. */
#ifndef HAVE_CONFIG_H
    #define PACKAGE_NAME 	"wdfs"
    #define PACKAGE_VERSION	"unknown"
#endif

/* product string according RFC 2616, that is included in every request.     */
const char *project_name = PACKAGE_NAME"/"PACKAGE_VERSION;

/* homepage of this filesystem                                               */
const char *project_uri = "http://noedler.de/projekte/wdfs/";

/* init settings with default values */
struct wdfs_conf wdfs = [] () {
    struct wdfs_conf w;
    w.debug = false;
    w.accept_certificate = false;
    w.redirect = true;
    w.locking_mode = NO_LOCK;
    w.locking_timeout = 300;
    return w;
} ();

enum {
    KEY_HELP,
    KEY_VERSION,
    KEY_VERSION_FULL,
    KEY_DEBUG,
    KEY_LOCKING_MODE,
    KEY_NOOP,
};

#define WDFS_OPT(t, p, v) { t, offsetof(struct wdfs_conf, p), v }

static struct fuse_opt wdfs_opts[] = {
    FUSE_OPT_KEY("-h",				KEY_HELP),
    FUSE_OPT_KEY("--help",			KEY_HELP),
    FUSE_OPT_KEY("-v",				KEY_VERSION),
    FUSE_OPT_KEY("--version",		KEY_VERSION),
    FUSE_OPT_KEY("-vv",				KEY_VERSION_FULL),
    FUSE_OPT_KEY("--all-versions",	KEY_VERSION_FULL),
    FUSE_OPT_KEY("-D",				KEY_DEBUG),
    FUSE_OPT_KEY("wdfs_debug",		KEY_DEBUG),
    FUSE_OPT_KEY("-m %u",			KEY_LOCKING_MODE),
    FUSE_OPT_KEY("-a",				KEY_NOOP),
    WDFS_OPT("-D",					debug, true),
    WDFS_OPT("wdfs_debug",			debug, true),
    WDFS_OPT("-ac",					accept_certificate, true),
    WDFS_OPT("accept_sslcert",		accept_certificate, true),
    WDFS_OPT("-u %s",				username, 0),
    WDFS_OPT("username=%s",			username, 0),
    WDFS_OPT("-p %s",				password, 0),
    WDFS_OPT("password=%s",			password, 0),
    WDFS_OPT("no_redirect",			redirect, false),
    WDFS_OPT("-l",					locking_mode, SIMPLE_LOCK),
    WDFS_OPT("locking",				locking_mode, SIMPLE_LOCK),
    WDFS_OPT("locking=0",			locking_mode, NO_LOCK),
    WDFS_OPT("locking=none",		locking_mode, NO_LOCK),
    WDFS_OPT("locking=1",			locking_mode, SIMPLE_LOCK),
    WDFS_OPT("locking=simple",		locking_mode, SIMPLE_LOCK),
    WDFS_OPT("locking=2",			locking_mode, ADVANCED_LOCK),
    WDFS_OPT("locking=advanced",	locking_mode, ADVANCED_LOCK),
    WDFS_OPT("locking=3",			locking_mode, ETERNITY_LOCK),
    WDFS_OPT("locking=eternity",	locking_mode, ETERNITY_LOCK),
    WDFS_OPT("-t %u",				locking_timeout, 300),
    WDFS_OPT("locking_timeout=%u",	locking_timeout, 300),
    FUSE_OPT_END
};

static int wdfs_opt_proc(
    void *data, const char *option, int key, struct fuse_args *option_list)
{
    switch (key) {
        case KEY_HELP:
            print_help();
            fuse_opt_add_arg(option_list, "-ho");
            call_fuse_main(option_list);
            exit(1);

        case KEY_VERSION:
            fprintf(stderr, "%s version: %s\n", PACKAGE_NAME, PACKAGE_VERSION);
            exit(0);

        case KEY_VERSION_FULL:
            fprintf(stderr, "%s version: %s\n", PACKAGE_NAME, PACKAGE_VERSION);
            fprintf(stderr, "%s homepage: %s\n", PACKAGE_NAME, project_uri);
            fprintf(stderr, "neon version: 0.%d\n", NEON_VERSION);
            fuse_opt_add_arg(option_list, "--version");
            call_fuse_main(option_list);
            exit(0);

        case KEY_DEBUG:
            return fuse_opt_add_arg(option_list, "-f");

        case KEY_LOCKING_MODE:
            if (option[3] != '\0' || option[2] < '0' || option[2] > '3') {
                fprintf(stderr, "%s: unknown locking mode '%s'\n",
                wdfs.program_name, option + 2);
                exit(1);
            } else {
                wdfs.locking_mode = option[2] - '0';
            }
            return 0;

        case KEY_NOOP:
            return 0;

        case FUSE_OPT_KEY_NONOPT:
            if (wdfs.webdav_resource.empty() && 
                    strncmp(option, "http", 4) == 0) {
                wdfs.webdav_resource = option;
                return 0;
            }
            else if (wdfs.mountpoint.empty()) {
                wdfs.mountpoint = option;
                return 1;
            }
            return 1;

        case FUSE_OPT_KEY_OPT:
            return 1;

        default:
            fprintf(stderr, "%s: unknown option '%s'\n",
                wdfs.program_name, option);
            exit(1);
    }
}


/* webdav server base directory. if you are connected to "http://server/dir/"
 * remotepath_basedir is set to "/dir" (starting slash, no ending slash).
 * if connected to the root directory (http://server/) it will be set to "". */
char *remotepath_basedir;

struct readdir_ctx_t {
    void *buf;
    fuse_fill_dir_t filler;
    std::unique_ptr<char> remotepath;
};

/* infos about an open file. used by open(), read(), write() and release()   */
struct file_t {
    file_t() : fd(-1), modified(false) {}
    
    int fd;	/* this file's filehandle                            */
    bool modified;	/* set true if the filehandle's content is modified  */
};

enum field_e {
    TYPE = 0,
    LENGTH,
    MODIFIED,    
    CREATION,    
    ETAG,
    EXECUTE,
    PERMISSIONS,
    END
};

static const auto prop_names = [] {
    std::vector<ne_propname> v(END + 1);
    v[ETAG]     = {"DAV:", "getetag"};
    v[LENGTH]   = {"DAV:", "getcontentlength"};
    v[CREATION] = {"DAV:", "creationdate"};
    v[MODIFIED] = {"DAV:", "getlastmodified"};
    v[TYPE]     = {"DAV:", "resourcetype"};
    v[EXECUTE]  = {"http://apache.org/dav/props/", "executable"};
    v[PERMISSIONS]  = {"X-DAV:", "permissions"};
    v[END]      = {NULL, NULL}; 
    return v;
} ();

static const auto anonymous_prop_names = [] {
    std::vector<ne_propname> v = prop_names;
    for(auto it = v.begin(); it != v.end(); ++ it) it->nspace = NULL;
    return v;
} ();

/* returns the malloc()ed escaped remotepath on success or NULL on error */
static std::unique_ptr<char> get_remotepath(const char *localpath)
{
    assert(localpath);
    std::unique_ptr<char> remotepath(ne_concat(remotepath_basedir, localpath, NULL));
    return (remotepath) 
        ? std::unique_ptr<char>(unify_path(remotepath.get(), ESCAPE | LEAVESLASH))
        : std::unique_ptr<char>();
}

/* returns a filehandle for read and write on success or -1 on error */
static int get_filehandle(const char* localpath = NULL)
{
    if (localpath == NULL) {
        char dummyfile[] = "/tmp/wdfs-tmp-XXXXXX";
        /* mkstemp() replaces XXXXXX by unique random chars and
            * returns a filehandle for reading and writing */
        int fd = mkstemp(dummyfile);
        if (fd == -1)
            fprintf(stderr, "## mkstemp(%s) error\n", dummyfile);
        if (unlink(dummyfile))
            fprintf(stderr, "## unlink() error\n");
        return fd;
    }
    else {
        return cache->create_file(localpath);
    }
}

std::string get_filename(const char* remotepath) {
    /* extract filename from the path. it's the string behind the last '/'. */
    const char *filename = strrchr(remotepath, '/');
    filename++;
    return filename;
}

const char* get_helper(const ne_prop_result_set *results, field_e field) {
    const char* data = ne_propset_value(results, &prop_names[field]);
    return (data) 
        ? data
        : ne_propset_value(results, &anonymous_prop_names[field]);
}

/* evaluates the propfind result set and sets the file's attributes (stat) */
static void set_stat(etag_t& etag, struct stat& stat, const ne_prop_result_set *results)
{
    wdfs_dbg("%s()\n", __func__);

    const char *resourcetype, *contentlength, *lastmodified, *creationdate/*, *executable*/, *modestr, *etagstr;

    assert(results);

    /* get the values from the propfind result set */
    resourcetype	= get_helper(results, TYPE);
    contentlength	= get_helper(results, LENGTH);
    lastmodified	= get_helper(results, MODIFIED);
    creationdate	= get_helper(results, CREATION);
    // 	executable	    = get_helper(results, EXECUTE);
    modestr	        = get_helper(results, PERMISSIONS);
    etagstr         = get_helper(results, ETAG);

    int mode = 0;

    /* webdav collection == directory entry */
    if (resourcetype != NULL && !strstr("<collection", resourcetype)) {
        /* "DT_DIR << 12" equals "S_IFDIR" */
        mode = (modestr) ? atoi(modestr) : 0777;
        mode |= S_IFDIR;
        stat.st_size = 4096;
    } else {
        mode = (modestr) ? atoi(modestr) : 0666;
        mode |= S_IFREG;
        stat.st_size = (contentlength) ? atoll(contentlength) : 0;
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
    stat.st_blocks	= (stat.st_size + 511) / 512;

    /* no need to set a restrict mode, because fuse filesystems can
        * only be accessed by the user that mounted the filesystem.  */
    stat.st_mode &= ~umask(0);
    stat.st_uid = getuid();
    stat.st_gid = getgid();

    if (etagstr) etag.reset(etagstr);
}


/* this method is invoked, if a redirect needs to be done. therefore the current
 * remotepath is freed and set to the redirect target. returns -1 and prints an
 * error if the current host and new host differ. returns 0 on success and -1 
 * on error. side effect: remotepath is freed on error. */
// static int handle_redirect(char **remotepath)
static int handle_redirect(std::unique_ptr<char>& remotepath)
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


/* +++ fuse callback methods +++ */


/* this method is called by ne_simple_propfind() from wdfs_getattr() for a
 * specific file. it sets the file's attributes and and them to the cache. */
static void wdfs_getattr_propfind_callback(
#if NEON_VERSION >= 26
    void *userdata, const ne_uri* href_uri, const ne_prop_result_set *results)
#else
    void *userdata, const char *remotepath, const ne_prop_result_set *results)
#endif
{
#if NEON_VERSION >= 26
    char *remotepath = ne_uri_unparse(href_uri);
#endif

    wdfs_dbg("%s(%s)\n", __func__, remotepath);  

    struct stat *stat = reinterpret_cast<struct stat*>(userdata);
    memset(stat, 0, sizeof(struct stat));

    assert(remotepath);

    cache_t::item_p cached_file(new cache_t::item);
    set_stat(cached_file->resource.etag, cached_file->resource.stat, results);
    if (cache_t::item_p old_file = cache->get(remotepath)) {
        if (cached_file->resource.etag == old_file->resource.etag) {
            cached_file->fd = old_file->fd;
        }
    }

    cache->update(remotepath, *cached_file);

    *stat = cached_file->resource.stat;

#if NEON_VERSION >= 26
    FREE(remotepath);
#endif
}


/* this method returns the file attributes (stat) for a requested file either
 * from the cache or directly from the webdav server by performing a propfind
 * request. */
static int wdfs_getattr(const char *localpath, struct stat *stat)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath);      
    assert(localpath && stat);

    auto remotepath(get_remotepath(localpath));
    if (!remotepath) return -ENOMEM;

    if (auto cached_stat = cache->stat(remotepath.get())) {
        *stat = *cached_stat;
    } 
    else {
        int ret = ne_simple_propfind(
            session, remotepath.get(), NE_DEPTH_ZERO, &prop_names[0],
            wdfs_getattr_propfind_callback, stat);
        /* handle the redirect and retry the propfind with the new target */
        if (ret == NE_REDIRECT && wdfs.redirect == true) {
            if (handle_redirect(remotepath))
                return -ENOENT;
            ret = ne_simple_propfind(
                session, remotepath.get(), NE_DEPTH_ZERO, &prop_names[0],
                wdfs_getattr_propfind_callback, stat);
        }
        if (ret != NE_OK) {
            fprintf(stderr, "## PROPFIND error in %s(): %s\n",
                __func__, ne_get_error(session));
            return -ENOENT;
        }
    }

    return 0;
}

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
    char *remotepath = ne_uri_unparse(href_uri);
#else
    char *remotepath = strdup(remotepath0);
#endif

    wdfs_dbg("%s(%s)\n", __func__, remotepath);      

    struct readdir_ctx_t *ctx = reinterpret_cast<readdir_ctx_t*>(userdata);
    assert(ctx);

    char *remotepath1 = unify_path(remotepath, UNESCAPE);
    char *remotepath2 = unify_path(ctx->remotepath.get(), UNESCAPE);
    if (remotepath1 == NULL || remotepath2 == NULL) {
        free_chars(&remotepath, &remotepath1, &remotepath2, NULL);
        fprintf(stderr, "## fatal error: unify_path() returned NULL\n");
        return;
    }

    /* don't add this directory to itself */
    if (!strcmp(remotepath2, remotepath1)) {
        free_chars(&remotepath, &remotepath1, &remotepath2, NULL);
        return;
    }

    const std::string filename = get_filename(remotepath1);

    /* set this file's attributes. the "ne_prop_result_set *results" contains
        * the file attributes of all files of this collection (directory). this 
        * performs better then single requests for each file in getattr().  */

    cache_t::item_p cached_file(new cache_t::item);
    set_stat(cached_file->resource.etag, cached_file->resource.stat, results);
    if (cache_t::item_p old_file = cache->get(remotepath)) {
        if (cached_file->resource.etag == old_file->resource.etag) {
            cached_file->fd = old_file->fd;
        }
    }

    cache->update(remotepath, *cached_file);

    /* add directory entry */
    if (ctx->filler(ctx->buf, filename.c_str(), &cached_file->resource.stat, 0))
        fprintf(stderr, "## filler() error in %s()!\n", __func__);

    free_chars(&remotepath, &remotepath1, &remotepath2, NULL);
}


/* this method adds the files to the requested directory using the webdav method
 * propfind. the server responds with status code 207 that contains metadata of 
 * all files of the requested collection. for each file the method 
 * wdfs_readdir_propfind_callback() is called. */
static int wdfs_readdir(
    const char *localpath, void *buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info *fi)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath);      
    assert(localpath && filler);

    struct readdir_ctx_t ctx = {
        buf,
        filler,
        get_remotepath(localpath)
    };

    if (!ctx.remotepath) return -ENOMEM;


    int ret = ne_simple_propfind(
        session, ctx.remotepath.get(), NE_DEPTH_ONE,
        &prop_names[0], wdfs_readdir_propfind_callback, &ctx);
    /* handle the redirect and retry the propfind with the redirect target */
    if (ret == NE_REDIRECT && wdfs.redirect == true) {
        if (handle_redirect(ctx.remotepath))
            return -ENOENT;
        ret = ne_simple_propfind(
            session, ctx.remotepath.get(), NE_DEPTH_ONE,
            &prop_names[0], wdfs_readdir_propfind_callback, &ctx);
    }
    if (ret != NE_OK) {
            fprintf(stderr, "## PROPFIND error in %s(): %s\n",
                __func__, ne_get_error(session));
        return -ENOENT;
    }

    struct stat st;
    memset(&st, 0, sizeof(st));
    st.st_mode = S_IFDIR | 0777;
    filler(buf, ".", &st, 0);
    filler(buf, "..", &st, 0);

    return 0;
}


/* author jens, 13.08.2005 11:22:20, location: unknown, refactored in goettingen
 * get the file from the server already at open() and write the data to a new
 * filehandle. also create a "struct open_file" to store the filehandle. */
static int wdfs_open(const char *localpath, struct fuse_file_info *fi)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    wdfs_pr("   ++ by PID %d\n", fuse_get_context()->pid);

    assert(localpath && fi);

    std::unique_ptr<file_t> file(new file_t);

    auto remotepath = get_remotepath(localpath);
    if (!remotepath) return -ENOMEM;
    
    webdav_resource_t resource_full; //resource from PROPFIND request
    webdav_resource_t resource_new;
    if (get_head(session, remotepath.get(), &resource_new)) return -ENOENT;

    if (auto cached_file = cache->get(remotepath.get())) {
        resource_full = cached_file->resource;
        
        if (resource_new.etag != cached_file->resource.etag) {
            wdfs_pr("   -- ETag is diferent - invalidate cache\n");
            cache->remove(remotepath.get());
        }
        else if (!resource_new.etag && resource_new.stat.st_mtime != cached_file->resource.stat.st_mtime) {
            wdfs_pr("   -- There no ETag supported and modtiem different - invalidate cache\n");
            cache->remove(remotepath.get());
        }
        else if (resource_new.stat.st_size != cached_file->resource.stat.st_size) {
            wdfs_pr("   -- Filesize different - invalidate cache\n");
            cache->remove(remotepath.get());
        }
        else if (cached_file->fd == -1) {
            wdfs_pr("   -- There is no file in cache - try to restore cache from disk...\n");
            std::string cachepath = cache->cache_filename(remotepath.get());
            wdfs_pr("   -- Trying to open file %s...\n", cachepath.c_str());            
            int fd = open(cachepath.c_str(), O_RDWR);
            if (fd != -1) {
                wdfs_pr("   ++ Filecache +hit+ RESTORED\n");                        
                cached_file->fd = fd;
                file->fd = cached_file->fd;
                cache->update(remotepath.get(), *cached_file);
            }          
            else {
                wdfs_pr("   -- Filecache +miss+ NOT RESTORED\n");             
                cache->remove(remotepath.get());
            }
        }
        else if (resource_new.stat.st_mtime != cached_file->resource.stat.st_mtime) {
            wdfs_pr("   ++ ETag and sizes are same but modtime different - update cache\n");
            cached_file->resource.update_from(resource_new);
            cache->update(remotepath.get(), *cached_file);
        }
    }

    if (auto cached_file = cache->get(remotepath.get())) {
        wdfs_pr("   ++ Filecache +hit+ no download needed\n");
        file->fd = cached_file->fd;
    }
    else {
        wdfs_pr("   ++ Filecache +miss+ download file\n");
        file->fd = get_filehandle(localpath);
        if (file->fd == -1) return -EIO;
        
        /* try to lock, if locking is enabled and file is not below svn_basedir. */
        if (wdfs.locking_mode != NO_LOCK) {
            if (lockfile(remotepath.get(), wdfs.locking_timeout)) {
                /* locking the file is not possible, because the file is locked by 
                * somebody else. read-only access is allowed. */
                if ((fi->flags & O_ACCMODE) == O_RDONLY) {
                    fprintf(stderr,
                        "## error: file %s is already locked. "
                        "allowing read-only (O_RDONLY) access!\n", remotepath.get());
                } else {
                    return -EACCES;
                }
            }
        }

        webdav_context_t ctx{session};  
        ctx.resource.etag.reset(""); //disable etag facility
        hook_helper_t hooker(session, &ctx);
        
        /* GET the data to the filehandle even if the file is opened O_WRONLY,
        * because the opening application could use pwrite() or use O_APPEND
        * and than the data needs to be present. */   
        if (ne_get(session, remotepath.get(), file->fd)) {
            fprintf(stderr, "## GET error: %s\n", ne_get_error(session));
            return -ENOENT;        
        }

        resource_full.update_from(resource_new);
        resource_full.update_from(ctx.resource);
        cached_file_t cached_file(resource_full, file->fd);
        wdfs_pr("   ++ File downloaded successfuly\n");
        cache->update(remotepath.get(), cached_file);
    }


    /* save our "struct open_file" to the fuse filehandle
        * this looks like a dirty hack too me, but it's the fuse way... */
    fi->fh = reinterpret_cast<uint64_t>(file.release());

    return 0;
}


/* reads data from the filehandle with pread() to fulfill read requests */
static int wdfs_read(
    const char *localpath, char *buf, size_t size,
    off_t offset, struct fuse_file_info *fi)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    assert(localpath && buf && fi);

    file_t *file = reinterpret_cast<file_t*>(fi->fh);

    int ret = pread(file->fd, buf, size, offset);
    if (ret < 0) wdfs_err("pread() error: %d\n", ret);

    return ret;
}


/* writes data to the filehandle with pwrite() to fulfill write requests */
static int wdfs_write(
    const char *localpath, const char *buf, size_t size,
    off_t offset, struct fuse_file_info *fi)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    assert(localpath && buf && fi);

    file_t *file = reinterpret_cast<file_t*>(fi->fh);

    int ret = pwrite(file->fd, buf, size, offset);
    if (ret < 0) wdfs_err("pwrite() error: %d\n", ret);

    /* set this flag, to indicate that data has been modified and needs to be
        * put to the webdav server. */
    file->modified = true;

    return ret;
}


/* author jens, 13.08.2005 11:28:40, location: unknown, refactored in goettingen
 * wdfs_release is called by fuse, when the last reference to the filehandle is
 * removed. this happens if the file is closed. after closing the file it's
 * time to put it to the server, but only if it was modified. */
static int wdfs_release(const char *localpath, struct fuse_file_info *fi)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    assert(localpath);

    file_t *file = reinterpret_cast<file_t*>(fi->fh);

    auto remotepath(get_remotepath(localpath));
    if (!remotepath) return -ENOMEM;

    /* put the file only to the server, if it was modified. */
    if (file->modified == true) {
        
        {
            // Uploading file and updating cache to result values
            auto cached_file = cache->get(remotepath.get());
            
            webdav_context_t ctx{session};  
            hook_helper_t hooker(session, &ctx);
            
            if (ne_put(session, remotepath.get(), file->fd)) {
                fprintf(stderr, "## PUT error: %s\n", ne_get_error(session));
                return -EIO;
            }
            
            cached_file->resource.update_from(ctx.resource);
            cache->update(remotepath.get(), *cached_file);
        }
        
        wdfs_dbg("%s(): PUT the file to the server\n", __func__); 

        /* attributes for this file are no longer up to date.
            * so remove it from cache. */
    //         file_cache.remove(remotepath);//TODO FIXME

        /* unlock if locking is enabled and mode is ADVANCED_LOCK, because data
            * has been read and writen and so now it's time to remove the lock. */
        if (wdfs.locking_mode == ADVANCED_LOCK) {
            if (unlockfile(remotepath.get())) {
                return -EACCES;
            }
        }
    }

    /* if locking is enabled and mode is SIMPLE_LOCK, simple unlock on close() */
    if (wdfs.locking_mode == SIMPLE_LOCK) {
        if (unlockfile(remotepath.get())) {
            return -EACCES;
        }
    }

    /* close filehandle and free memory */
    // close(file->fd);//TODO FIXME refference count
    FREE(file);
    fi->fh = 0;

    return 0;
}


/* author jens, 13.08.2005 11:32:20, location: unknown, refactored in goettingen
 * wdfs_truncate is called by fuse, when a file is opened with the O_TRUNC flag
 * or truncate() is called. according to 'man truncate' if the file previously 
 * was larger than this size, the extra data is lost. if the file previously 
 * was shorter, it is extended, and the extended part is filled with zero bytes.
 */
static int wdfs_truncate(const char *localpath, off_t size)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    wdfs_pr("   ++ at offset %li\n", (long int)size);

    assert(localpath);

    /* the truncate procedure:
        *  1. get the complete file and write into fh_in
        *  2. read size bytes from fh_in to buffer
        *  3. write size bytes from buffer to fh_out
        *  4. read from fh_out and put file to the server
        */

    auto remotepath = get_remotepath(localpath);
    if (!remotepath) return -ENOMEM;

    if (auto cached_file = cache->get(remotepath.get())) {
        int fd = cached_file->fd;
        if (fd != -1) {
            if (int ret = ftruncate(fd, size)) {
                fprintf(stderr, "## ftruncate() error: %d\n", ret);
                return -EIO;                
            }
            else {
                cached_file->resource.stat.st_size = size;
                /* calculate number of 512 byte blocks */
                cached_file->resource.stat.st_blocks = (cached_file->resource.stat.st_size + 511) / 512;
                cache->update(remotepath.get(), *cached_file);
            }
            return 0;
        }
    }

    int ret;
    int fh_in  = get_filehandle();
    int fh_out = get_filehandle();
    if (fh_in == -1 || fh_out == -1)
        return -EIO;

    char buffer[size];
    memset(buffer, 0, size);

    /* if truncate(0) is called, there is no need to get the data, because it
    * would not be used. */
    if (size != 0) {
        if (ne_get(session, remotepath.get(), fh_in)) {
            fprintf(stderr, "## GET error: %s\n", ne_get_error(session));
            close(fh_in);
            close(fh_out);
            return -ENOENT;
        }

        ret = pread(fh_in, buffer, size, 0);
        if (ret < 0) {
            fprintf(stderr, "## pread() error: %d\n", ret);
            close(fh_in);
            close(fh_out);
            return -EIO;
        }
    }

    ret = pwrite(fh_out, buffer, size, 0);
    if (ret < 0) {
        fprintf(stderr, "## pwrite() error: %d\n", ret);
        close(fh_in);
        close(fh_out);
        return -EIO;
    }

    if (ne_put(session, remotepath.get(), fh_out)) {
        fprintf(stderr, "## PUT error: %s\n", ne_get_error(session));
        close(fh_in);
        close(fh_out);
        return -EIO;
    }

    close(fh_in);
    close(fh_out);

    return 0;
}


/* author jens, 12.03.2006 19:44:23, location: goettingen in the winter
 * ftruncate is called on already opened files, truncate on not yet opened
 * files. ftruncate is supported since wdfs 1.2.0 and needs at least 
 * fuse 2.5.0 and linux kernel 2.6.15. */
static int wdfs_ftruncate(
    const char *localpath, off_t size, struct fuse_file_info *fi)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    assert(localpath && fi);

    auto remotepath = get_remotepath(localpath);
    if (!remotepath) return -ENOMEM;

    struct file_t *file = (struct file_t*)(uintptr_t)fi->fh;

    if (int ret = ftruncate(file->fd, size)) {
        fprintf(stderr, "## ftruncate() error: %d\n", ret);
        return -EIO;
    }

    /* set this flag, to indicate that data has been modified and needs to be
        * put to the webdav server. */
    file->modified = true;

    /* update the cache item of the ftruncate()d file */
    cache_t::item_p cached_file = cache->get(remotepath.get());
    assert(cached_file);
    cached_file->resource.stat.st_size = size;
    /* calculate number of 512 byte blocks */
    cached_file->resource.stat.st_blocks	= (cached_file->resource.stat.st_size + 511) / 512;
    cache->update(remotepath.get(), *cached_file);

    return 0;
}


/* author jens, 28.07.2005 18:15:12, location: noedlers garden in trubenhausen
 * this method creates a empty file using the webdav method put. */
static int wdfs_mknod(const char *localpath, mode_t mode, dev_t rdev)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    assert(localpath);

    auto remotepath = get_remotepath(localpath);
    if (!remotepath) return -ENOMEM;

    int fh = get_filehandle();
    if (fh == -1) {
        return -EIO;
    }

    if (ne_put(session, remotepath.get(), fh)) {
        fprintf(stderr, "## PUT error: %s\n", ne_get_error(session));
        close(fh);
        return -EIO;
    }

    close(fh);
    return 0;
}


/* author jens, 03.08.2005 12:03:40, location: goettingen
 * this method creates a directory / collection using the webdav method mkcol. */
static int wdfs_mkdir(const char *localpath, mode_t mode)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    assert(localpath);

    auto remotepath = get_remotepath(localpath);
    if (!remotepath) return -ENOMEM;

    if (ne_mkcol(session, remotepath.get())) {
        fprintf(stderr, "MKCOL error: %s\n", ne_get_error(session));
        return -ENOENT;
    }

    return 0;
}


/* author jens, 30.07.2005 13:08:11, location: heli at heinemanns
 * this methods removes a file or directory using the webdav method delete. */
static int wdfs_unlink(const char *localpath)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    assert(localpath);

    auto remotepath = get_remotepath(localpath);
    if (!remotepath) return -ENOMEM;

    /* unlock the file, to be able to unlink it */
    if (wdfs.locking_mode != NO_LOCK) {
        if (unlockfile(remotepath.get())) {
            return -EACCES;
        }
    }

    int ret = ne_delete(session, remotepath.get());
    if (ret == NE_REDIRECT && wdfs.redirect == true) {
        if (handle_redirect(remotepath))
            return -ENOENT;
        ret = ne_delete(session, remotepath.get());
    }

    /* file successfully deleted! remove it also from the cache. */
    if (ret == 0) {
        cache->remove(remotepath.get());
    /* return more specific error message in case of permission problems */
    } else if (!strcmp(ne_get_error(session), "403 Forbidden")) {
        ret = -EPERM;
    } else {
        fprintf(stderr, "## DELETE error: %s\n", ne_get_error(session));
        ret = -EIO;
    }

    return ret;
}


/* author jens, 31.07.2005 19:13:39, location: heli at heinemanns
 * this methods renames a file. it uses the webdav method move to do that. */
static int wdfs_rename(const char *localpath_src, const char *localpath_dest)
{
    wdfs_dbg("%s(%s -> %s)\n", __func__, localpath_src, localpath_dest); 
    assert(localpath_src && localpath_dest);

    auto remotepath_src  = get_remotepath(localpath_src);
    auto remotepath_dest = get_remotepath(localpath_dest);
    if (!remotepath_src || !remotepath_dest) return -ENOMEM;

    /* unlock the source file, before renaming */
    if (wdfs.locking_mode != NO_LOCK) {
        if (unlockfile(remotepath_src.get())) {
            return -EACCES;
        }
    }

    int ret = ne_move(session, 1, remotepath_src.get(), remotepath_dest.get());
    if (ret == NE_REDIRECT && wdfs.redirect == true) {
        if (handle_redirect(remotepath_src))
            return -ENOENT;
        ret = ne_move(session, 1, remotepath_src.get(), remotepath_dest.get());
    }

    if (ret == 0) {
        /* rename was successful and the source file no longer exists.
            * hence, remove it from the cache. */
        cache->remove(remotepath_src.get());
    } else {
        fprintf(stderr, "## MOVE error: %s\n", ne_get_error(session));
        ret = -EIO;
    }

    return ret;
}


int wdfs_chmod(const char *localpath, mode_t mode)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath);
    assert(localpath);
    
    std::unique_ptr<char> remotepath(get_remotepath(localpath));
    const std::string mode_str = std::to_string(mode);
    const std::string exec_str = (mode & S_IXUSR || mode & S_IXGRP || mode &S_IXOTH) ? "T" : "F";
    
    const ne_proppatch_operation ops[] = {
        {
            &prop_names[EXECUTE],
            ne_propset,
            exec_str.c_str()
        },
        {
            &prop_names[PERMISSIONS],
            ne_propset,
            mode_str.c_str()
        },        
        {NULL}
    };
    
    cache_t::item_p cached_file = cache->get(remotepath.get());
    assert(cached_file);
    
    webdav_context_t ctx{session, cached_file->resource};  
    hook_helper_t hooker(session, &ctx);
    
    if (ne_proppatch(session, remotepath.get(), ops)) {
        fprintf(stderr, "PROPPATCH error: %s\n", ne_get_error(session));
        return -ENOENT;
    }
    
    cached_file->resource = ctx.resource;
    cache->update(remotepath.get(), *cached_file);
    
    return 0;
}


/* this is just a dummy implementation to avoid errors, when setting attributes.
 * a usefull implementation is not possible, because the webdav standard only 
 * defines a "getlastmodified" property that is read-only and just updated when
 * the file's content or properties change. */
static int wdfs_setattr(const char *localpath, struct utimbuf *buf)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath);
    assert(localpath);

    return 0;
}


/* this is a dummy implementation that pretends to have 1000 GB free space :D */
static int wdfs_statfs(const char *localpath, struct statvfs *buf)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath);
    assert(localpath);

    /* taken from sshfs v1.7, thanks miklos! */
    buf->f_bsize = 512;
    buf->f_blocks = buf->f_bfree = buf->f_bavail =
        1000ULL * 1024 * 1024 * 1024 / buf->f_bsize;
    buf->f_files = buf->f_ffree = 1000000000;

    return 0;
}


/* just say hello when fuse takes over control. */
#if FUSE_VERSION >= 26
    static void* wdfs_init(struct fuse_conn_info *conn)
#else
    static void* wdfs_init()
#endif
{
    wdfs_dbg("%s()\n", __func__);

    cache.reset(new cache_t(wdfs.cache_folder));

    try {
        std::ifstream stream((wdfs.cache_folder + "cache").c_str());
        boost::archive::text_iarchive oa(stream);
        oa >> *cache;
    }
    catch(const std::exception& e) {
        wdfs_dbg("%s(): can't load cache\n", __func__);
    }

    wdfs_dbg("%s() restored cache size: %d\n", __func__, cache->size());

    return NULL;
}


/* author jens, 04.08.2005 17:41:12, location: goettingen
 * this method is called, when the filesystems is unmounted. time to clean up! */
static void wdfs_destroy(void*)
{
    wdfs_dbg("%s()\n", __func__);

    try {
        std::ofstream stream((wdfs.cache_folder + "cache").c_str());
        boost::archive::text_oarchive oa(stream);
        oa << *cache;
    }
    catch(const std::exception& e) {
        wdfs_dbg("%s(): can't save cache\n", __func__);
    }
    
    /* free globaly used memory */
    unlock_all_files();
    ne_session_destroy(session);
    FREE(remotepath_basedir);
}

static struct fuse_operations wdfs_operations  = [] () {
    fuse_operations wo;
    
    wo.getattr    = wdfs_getattr;
    wo.readdir    = wdfs_readdir;
    wo.open       = wdfs_open;
    wo.read       = wdfs_read;
    wo.write      = wdfs_write;
    wo.release    = wdfs_release;
    wo.truncate   = wdfs_truncate;
    wo.ftruncate  = wdfs_ftruncate;
    wo.mknod      = wdfs_mknod;
    wo.mkdir      = wdfs_mkdir;
    /* webdav treats file and directory deletions equal, both use wdfs_unlink */
    wo.unlink     = wdfs_unlink;
    wo.rmdir      = wdfs_unlink;
    wo.rename     = wdfs_rename;
    wo.chmod      = wdfs_chmod;
    /* utime should be better named setattr
     * see: http://sourceforge.net/mailarchive/message.php?msg_id=11344401 */
    wo.utime      = wdfs_setattr;
    wo.statfs     = wdfs_statfs;
    wo.init       = wdfs_init;
    wo.destroy    = wdfs_destroy;
    
    return wo;
} ();


/* author jens, 26.08.2005 12:26:59, location: lystrup near aarhus 
 * this method prints help and usage information, call fuse to print its
 * help information. */
static void print_help()
{
    fprintf(stderr,
"usage: %s http[s]://server[:port][/directory/] mountpoint [options]\n\n"
"wdfs options:\n"
"    -v, --version          show version of wdfs\n"
"    -vv, --all-versions    show versions of wdfs, neon and fuse\n"
"    -h, --help             show this help page\n"
"    -D, -o wdfs_debug      enable wdfs debug output\n"
"    -o accept_sslcert      accept ssl certificate, don't prompt the user\n"
"    -o username=arg        replace arg with username of the webdav resource\n"
"    -o password=arg        replace arg with password of the webdav resource\n"
"                           username/password can also be entered interactively\n"
"    -o no_redirect         disable http redirect support\n"
"    -o svn_mode            enable subversion mode to access all revisions\n"
"    -o locking             same as -o locking=simple\n"
"    -o locking=mode        select a file locking mode:\n"
"                           0 or none:     disable file locking (default)\n"
"                           1 or simple:   from open until close\n"
"                           2 or advanced: from open until write + close\n"
"                           3 or eternity: from open until umount or timeout\n"
"    -o locking_timeout=sec timeout for a lock in seconds, -1 means infinite\n"
"                           default is 300 seconds (5 minutes)\n\n"
"wdfs backwards compatibility options: (used until wdfs 1.3.1)\n"
"    -a uri                 address of the webdav resource to mount\n"
"    -ac                    same as -o accept_sslcert\n"
"    -u arg                 same as -o username=arg\n"
"    -p arg                 same as -o password=arg\n"
"    -S                     same as -o svn_mode\n"
"    -l                     same as -o locking=simple\n"
"    -m locking_mode        same as -o locking=mode (only numerical modes)\n"
"    -t seconds             same as -o locking_timeout=sec\n\n",
    wdfs.program_name);
}


/* just a simple wrapper for fuse_main(), because the interface changed...  */
static int call_fuse_main(struct fuse_args *args)
{
#if FUSE_VERSION >= 26
    return fuse_main(args->argc, args->argv, &wdfs_operations, NULL);
#else
    return fuse_main(args->argc, args->argv, &wdfs_operations);
#endif
}

struct test_t {
    int a;
    double b;
};


/* the main method does the option parsing using fuse_opt_parse(), establishes
 * the connection to the webdav resource and finally calls main_fuse(). */
int main(int argc, char *argv[])
{
    int status_program_exec = 1;

    struct fuse_args options = FUSE_ARGS_INIT(argc, argv);
    wdfs.program_name = argv[0];

    if (fuse_opt_parse(&options, &wdfs, wdfs_opts, wdfs_opt_proc) == -1)
        exit(1);


    if (wdfs.webdav_resource.empty()) {
        fprintf(stderr, "%s: missing webdav uri\n", wdfs.program_name);
        exit(1);
    }
    
    std::shared_ptr<ne_uri> uri(new ne_uri);
    if (ne_uri_parse(wdfs.webdav_resource.c_str(), uri.get())) {
        fprintf(stderr,
            "## ne_uri_parse() error: invalid URI '%s'.\n", wdfs.webdav_resource.c_str());
        exit(1);
    }
    
    if(char const* home = getenv("HOME")) {
        auto hasher = std::hash<std::string>();
        wdfs.cache_folder = std::string(home) + "/" + ".wdfs/" + std::to_string(hasher(wdfs.webdav_resource)) + "/";
        if (mkdir_p(wdfs.cache_folder)) {
            fprintf(stderr, "%s: can't create cache folder %s\n", wdfs.program_name, wdfs.cache_folder.c_str());
            exit(1);
        }
        
        auto up = parse_netrc(std::string(home) + "/.netrc", uri->host);
        if (wdfs.username.empty()) wdfs.username = up.first;
        if (wdfs.password.empty()) wdfs.password = up.second;
    }
    else {
        fprintf(stderr, "%s: can't obtain HOME variable\n", wdfs.program_name);
        exit(1);
    }

    if (wdfs.locking_timeout < -1 || wdfs.locking_timeout == 0) {
        fprintf(stderr, "## error: timeout must be bigger than 0 or -1!\n");
        exit(1);
    }

    if (wdfs.debug == true) {
        fprintf(stderr, 
            "wdfs settings:\n  program_name: %s\n  webdav_resource: %s\n"
            "  accept_certificate: %s\n  username: %s\n  password: %s\n"
            "  redirect: %s\n  locking_mode: %i\n"
            "  locking_timeout: %i\n"
            "  cache folder: %s\n  mountpoint: %s\n",
            wdfs.program_name,
            !wdfs.webdav_resource.empty() ? wdfs.webdav_resource.c_str() : "NULL",
            wdfs.accept_certificate == true ? "true" : "false",
            !wdfs.username.empty() ? wdfs.username.c_str() : "NULL",
            !wdfs.password.empty() ? "****" : "NULL",
            wdfs.redirect == true ? "true" : "false",
            wdfs.locking_mode, wdfs.locking_timeout,
            wdfs.cache_folder.c_str(), wdfs.mountpoint.c_str());
    }

    /* set a nice name for /proc/mounts */
    char *fsname = ne_concat("-ofsname=wdfs (", wdfs.webdav_resource.c_str(), ")", NULL);
    fuse_opt_add_arg(&options, fsname);
    FREE(fsname);

    /* ensure that wdfs is called in single thread mode */
    fuse_opt_add_arg(&options, "-s");

    /* wdfs must not use the fuse caching of names (entries) and attributes! */
    fuse_opt_add_arg(&options, "-oentry_timeout=0");
    fuse_opt_add_arg(&options, "-oattr_timeout=0");

    /* reset parameters to avoid storing sensitive data in the process table */
    int arg_number = 1;
    for (; arg_number < argc; arg_number++)
        memset(argv[arg_number], 0, strlen(argv[arg_number]));

    /* set up webdav connection, exit on error */
    if (setup_webdav_session(*uri, wdfs.username, wdfs.password)) {
        status_program_exec = 1;
        goto cleanup;
    }

    /* finally call fuse */
    status_program_exec = call_fuse_main(&options);

    /* clean up and quit wdfs */
cleanup:
    fuse_opt_free_args(&options);

    return status_program_exec;
}
