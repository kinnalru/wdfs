#ifndef CACHE_H_
#define CACHE_H_

#include <string>
#include <map>

#include "common.h"

void cache_initialize();
void cache_destroy();
void cache_add_item(struct stat *stat, const char *remotepath);
void cache_delete_item(const char *remotepath);
int cache_get_item(struct stat *stat, const char *remotepath);


struct cached_file_t {
    cached_file_t() : fd (-1) {};
    cached_file_t(const webdav_resource_t& r, int f) : resource(r), fd(f) {};
    
    webdav_resource_t resource;
    int fd;
};

class file_cache_t {

public:
    
    typedef std::string etag_t;
    typedef std::map<std::string, cached_file_t> cache_t;
    
    cached_file_t get(const std::string& path);
    
    void update(const std::string& path, const cached_file_t& file);
    void update(const std::string& path, const webdav_resource_t& resource);
    
private:
    cache_t cache_;
};


#endif /*CACHE_H_*/