
#include <ne_string.h>

#include "wdfs_controller.h"

inline string_p mk_string(const std::string& string) {
    return string_p(unify_path(string.c_str(), ESCAPE | LEAVESLASH), free);
}

inline string_p mk_string(const string_p& string) {
    return string_p(unify_path(string.get(), ESCAPE | LEAVESLASH), free);
}

wdfs_controller_t::wdfs_controller_t(const std::string& srv, const std::string& rbd)
    : server_(srv), rbd_(rbd)
{}

string_p wdfs_controller_t::remotepath(const char* localpath) const
{
    assert(localpath);
    string_p remotepath(ne_concat(rbd_.c_str(), localpath, NULL), free);
    return (remotepath) ? mk_string(remotepath) : string_p();
}

string_p wdfs_controller_t::remove_server(std::string remotepath) const
{
    if (remotepath.find(server_) != std::string::npos) {
        remotepath = "/" + remotepath.substr(server_.size());
    }
    return mk_string(remotepath);
}


