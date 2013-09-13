#ifndef CACHE_H_
#define CACHE_H_

#include <string>
#include <map>

void cache_initialize();
void cache_destroy();
void cache_add_item(struct stat *stat, const char *remotepath);
void cache_delete_item(const char *remotepath);
int cache_get_item(struct stat *stat, const char *remotepath);

#endif /*CACHE_H_*/

typedef std::string etag_t;

struct webdav_resource_t {
    etag_t etag;
    struct stat stat;
};

struct cached_file_t {
    webdav_resource_t resource;
    uint64_t fd;
};

class file_cache_t {

public:
    
    typedef std::string etag_t;
    typedef std::map<std::string, cached_file_t> cache_t;
    
    cached_file_t get(const std::string& path);
    
    void update(const std::string& path, const cached_file_t& file);
    
private:
    cache_t cache_;
};