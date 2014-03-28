
#ifndef WDFS_WEBDAV_CONTROLLER_H
#define WDFS_WEBDAV_CONTROLLER_H

#include <memory>
#include <string>


#include "common.h"

struct wdfs_controller_t {
    
    wdfs_controller_t(const std::string& rbd)
        : rbd_(rbd)
    {} 

    
    /// returns the escaped remotepath on success or empty pointer on error
    string_p get_remotepath(const char *localpath)
    {
        assert(localpath);
        string_p remotepath(ne_concat(rbd_.c_str(), localpath, NULL), free);
        return (remotepath) 
            ? std::shared_ptr<char>(unify_path(remotepath.get(), ESCAPE | LEAVESLASH), free)
            : std::shared_ptr<char>();
    }
    
private:
    const std::string rbd_;
};







#endif
