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

struct cached_file_t {
    cached_file_t() : fd (-1) {};
    cached_file_t(const webdav_resource_t& r, int f) : resource(r), fd(f) {};
  
    friend std::ostream& operator<<(std::ostream& os, const cached_file_t& cached);
    friend std::istream& operator>>(std::istream& is, cached_file_t& cached);
    
    webdav_resource_t resource;
    int fd;
};

inline std::ostream& operator<<(std::ostream& os, const cached_file_t& cached)
{
    os << cached.resource;
    os << cached.fd << std::endl;
    return os;
}

inline std::istream& operator>>(std::istream& is, cached_file_t& cached)
{
    is >> cached.resource;
    is >> cached.fd;
    return is;
}

template <typename Item>
class cache_t {
    
protected:
    typedef std::map<std::string, Item> data_t;
    
public:
    typedef Item item;
    typedef std::unique_ptr<Item> item_p;

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
    
    void add(const std::string& path_raw, const item& v) {
        const std::string path = normalize(path_raw);
        assert(cache_.find(path) == cache_.end());
        
        std::cerr << "cache +added+ for path:" << path << std::endl;
        cache_[path] = v;
    }
    
    void update(const std::string& path_raw, const item& v) {
        const std::string path = normalize(path_raw);
        assert(cache_.find(path) != cache_.end());
        std::cerr << "cache +updated+ for path:" << path << std::endl;
        cache_[path] = v;
    }
    
    virtual void remove(const std::string& path_raw) {
        const std::string path = normalize(path_raw);
        std::cerr << "cache _removed_ for path:" << path << std::endl;
        cache_.erase(path);
    }
    
// protected:
    data_t cache_;
};


class file_cache_t : public cache_t<cached_file_t> {
public:
    using cache_t<cached_file_t>::add;
    void add(const std::string& path, const webdav_resource_t& resource);
    
    friend std::ostream& operator<<(std::ostream& os, const file_cache_t& cache);
    friend std::istream& operator>>(std::istream& is, file_cache_t& cache);
};

inline std::ostream& operator<<(std::ostream& os, const file_cache_t& cache)
{
    os << cache.cache_.size() << std::endl;
    file_cache_t::data_t::const_iterator it = cache.cache_.begin();
    for(; it != cache.cache_.end(); ++it) {
        os << it->first << std::endl;
        os << it->second;
    }
    return os;
}

inline std::istream& operator>>(std::istream& is, file_cache_t& cache)
{
    int size;
    is >> size;
    std::cerr << "reded size:" << size << std::endl;
    for (int i = 0; i < size; ++ i) {
        std::string key;
        cached_file_t cached;
        is >> key;
        std::cerr << "reded key:" << key << std::endl;
        is >> cached;
        cache.cache_[key] = cached;
    }
    return is;
}


#endif /*CACHE_H_*/