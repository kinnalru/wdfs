
#ifndef WDFS_WEBDAV_CONTROLLER_H
#define WDFS_WEBDAV_CONTROLLER_H

#include <memory>
#include <string>


#include "common.h"

struct wdfs_controller_t {
    
    wdfs_controller_t(const std::string& srv, const std::string& rbd);

    
    /// returns the escaped remotepath on success or empty pointer on error
    string_p remotepath(const char *localpath) const;
    
    /// returns the escaped string without server info
    string_p remove_server(std::string remotepath) const;
    
    string_p local2full(const char *localpath) const;
    string_p full2local(const char *localpath) const;
    string_p remote2full(const char *localpath) const;
    
private:
    const std::string server_;  
    const std::string rbd_;
};







#endif
