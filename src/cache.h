#ifndef CACHE_H_
#define CACHE_H_

#include <string>
#include <map>
#include <memory>

#include "common.h"

// void cache_initialize();
// void cache_destroy();
// void cache_add_item(struct stat *stat, const char *remotepath);
// void cache_delete_item(const char *remotepath);
// int cache_get_item(struct stat *stat, const char *remotepath);

struct cached_attr_t {
    webdav_resource_t resource;  
};


struct cached_file_t {
    cached_file_t() : fd (-1) {};
    cached_file_t(const webdav_resource_t& r, int f) : resource(r), fd(f) {};
    
    webdav_resource_t resource;
    int fd;
};

template <typename Item>
class cache_t {
    
protected:
    typedef std::map<std::string, Item> data_t;
    
public:
    typedef Item item;
    typedef std::shared_ptr<Item> item_p;

    std::string normalize(const std::string& path_raw) const {
        std::unique_ptr<char> path(unify_path(path_raw.c_str(), UNESCAPE));
        return std::string(path.get());
    }
    
    virtual item_p get(const std::string& path_raw) const {
        const std::string path = normalize(path_raw);
        typename data_t::const_iterator it = cache_.find(path);
        if (it != cache_.end()) {
            std::cerr << "cache +hit+ for path:" << path << std::endl;
            return item_p(new item(it->second));
        }
        else {
            std::cerr << "cache _miss_ for path:" << path << std::endl;
            return item_p();
        }
    }
    
    virtual void add(const std::string& path_raw, const item_p& v) {
        const std::string path = normalize(path_raw);
        if (v.get()) {
            std::cerr << "cache +added+ for path:" << path << std::endl;
            cache_[path] = *v;
        }
        else
            remove(path);
    }
    
    virtual void remove(const std::string& path_raw) {
        const std::string path = normalize(path_raw);
        std::cerr << "cache _removed_ for path:" << path << std::endl;
        cache_.erase(path);
    }
    
protected:
    data_t cache_;
};


class file_cache_t : public cache_t<cached_file_t> {
public:
    using cache_t<cached_file_t>::add;
    void add(const std::string& path, const webdav_resource_t& resource);
};

class attr_cache_t : public cache_t<cached_attr_t> {

};


#endif /*CACHE_H_*/