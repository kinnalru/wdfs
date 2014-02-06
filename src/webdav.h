#ifndef WEBDAV_H_
#define WEBDAV_H_

#include <ne_request.h>

#include "common.h"

extern ne_session *session;

int setup_webdav_session(ne_uri& uri, const std::string& username, const std::string& password);

int lockfile(const char *remotepath, const int timeout);
int unlockfile(const char *remotepath);
void unlock_all_files();

void create_request_handler(ne_request *req, void *userdata, const char *method, const char *requri);
int post_send_handler(ne_request *req, void *userdata, const ne_status *status);
int get_head(ne_session *session, const std::string& path, webdav_resource_t* resource);

size_t get(ne_session *session, const std::string& path, fuse_context_t* ctx);
size_t put(ne_session *session, const std::string& path, fuse_context_t* ctx);

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


#endif /*WEBDAV_H_*/
