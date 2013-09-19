#ifndef CACHE_H_
#define CACHE_H_

#include <string>
#include <map>
#include <memory>

#include "common.h"

typedef std::unique_ptr<struct stat> stat_p;

class cached_file_t {
    friend class boost::serialization::access;

    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & resource;
        ar & fd;
    }
    
public:
    cached_file_t() : fd (-1) {};
    cached_file_t(const webdav_resource_t& r, int f) : resource(r), fd(f) {};
  
    webdav_resource_t resource;
    int fd;
};

class cache_t {
    friend class boost::serialization::access;

    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & cache_;
    }
    
    typedef std::map<std::string, cached_file_t> data_t;
    
public:
    typedef cached_file_t item;
    typedef std::unique_ptr<item> item_p;

    std::string normalize(const std::string& path_raw) const {
        std::unique_ptr<char> path(unify_path(path_raw.c_str(), UNESCAPE));
        return std::string(path.get());
    }
    inline int size() const {return cache_.size();}
    
    stat_p stat(const std::string& path_raw) const {
        if (auto item = get(path_raw)) {
            return stat_p(new struct stat(item->resource.stat));
        }
        else {
            return stat_p();
        }
    }
    
    virtual item_p get(const std::string& path_raw) const {
        const std::string path = normalize(path_raw);
        data_t::const_iterator it = cache_.find(path);
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
        std::cerr << "cache +updated+ for path:" << path << std::endl;
        cache_[path] = v;
    }
    
    virtual void remove(const std::string& path_raw) {
        const std::string path = normalize(path_raw);
        std::cerr << "cache _removed_ for path:" << path << std::endl;
        cache_.erase(path);
    }
    
private:
    data_t cache_;
};


#endif /*CACHE_H_*/
