
#include <boost/algorithm/string/replace.hpp>
#include <ne_string.h>

#include "wdfs_controller.h"

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

string_p wdfs_controller_t::local2full(const char* localpath) const
{
    assert(localpath);
    return mk_string(canonicalize(rbd_ + localpath));
}


string_p wdfs_controller_t::remote2full(const char* remotepath) const
{
    std::string fullpath(remotepath);
    boost::replace_all(fullpath, server_, "");
    return mk_string(canonicalize(fullpath, UNESCAPE));
}




