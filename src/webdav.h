#ifndef WEBDAV_H_
#define WEBDAV_H_

#include <ne_request.h>

#include "common.h"
#include "wdfs_controller.h"

extern ne_session *session;

int setup_webdav_session(ne_uri& uri, const std::string& username, const std::string& password);

int lockfile(const char *remotepath, const int timeout);
int unlockfile(const char *remotepath);
void unlock_all_files();

void create_request_handler(ne_request *req, void *userdata, const char *method, const char *requri);
int post_send_handler(ne_request *req, void *userdata, const ne_status *status);
int get_head(ne_session *session, const std::string& path, webdav_resource_t* resource);

//Helper to hook libneon functions - ne_put, ne_get, etc...
struct hook_helper_t {
    ne_session* session;
    webdav_context_t* ctx;
    
    hook_helper_t(ne_session* s, webdav_context_t* c) : session(s), ctx(c) {
        assert(session);
        assert(ctx);
        ne_hook_create_request(session, create_request_handler, ctx);
        ne_hook_post_send(session, post_send_handler, ctx);
    }
    
    ~hook_helper_t() {
        ne_unhook_post_send(session, post_send_handler, ctx);        
        ne_unhook_create_request(session, create_request_handler, ctx);
    }
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



/* this method is invoked, if a redirect needs to be done. therefore the current
 * remotepath is freed and set to the redirect target. returns -1 and prints an
 * error if the current host and new host differ. returns 0 on success and -1 
 * on error. side effect: remotepath is freed on error. */
// static int handle_redirect(char **remotepath)
int handle_redirect(std::shared_ptr< char >& remotepath);

void set_stat(struct stat& stat, const ne_prop_result_set *results);

stats_t webdav_getattrs(ne_session* session, string_p remotepath, const wdfs_controller_t& wdfs);
stats_t webdav_readdir(ne_session* session, string_p fulldir, const wdfs_controller_t& wdfs);





#endif /*WEBDAV_H_*/
