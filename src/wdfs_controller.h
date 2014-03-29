
#ifndef WDFS_WEBDAV_CONTROLLER_H
#define WDFS_WEBDAV_CONTROLLER_H

#include <memory>
#include <string>


#include "common.h"

struct wdfs_controller_t {
    
    wdfs_controller_t(const std::string& srv, const std::string& rbd)
        : server_(srv), rbd_(rbd)
    {} 

    
    /// returns the escaped remotepath on success or empty pointer on error
    string_p get_remotepath(const char *localpath) const
    {
        assert(localpath);
        string_p remotepath(ne_concat(rbd_.c_str(), localpath, NULL), free);
        return (remotepath) 
            ? std::shared_ptr<char>(unify_path(remotepath.get(), ESCAPE | LEAVESLASH), free)
            : std::shared_ptr<char>();
    }
    
    std::string remove_server(const std::string& remotepath) const
    {
        size_t pos = remotepath.find(server_);
        if (pos != std::string::npos) {
            return "/" + remotepath.substr(server_.size());
        }
        else {
            return remotepath;
        }
    }
    
private:
    const std::string server_;  
    const std::string rbd_;
};







#endif
