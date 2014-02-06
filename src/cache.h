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
//         ar & fd;
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

    cache_t(const std::string& folder) : folder_(folder) {}
    
    std::string normalize(const std::string& path_raw) const {
        std::shared_ptr<char> path(unify_path(path_raw.c_str(), UNESCAPE), free);
        return std::string(path.get());
    }

    std::string cache_filename(const std::string& path_raw) const {
        const std::string path = normalize(path_raw);
        if (path.find(folder_) != std::string::npos)
            return path;
        else
            return folder_+ "/files/" + path;        
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
    
    item_p restore(const std::string& path_raw) {
        auto cached_file = get(path_raw);
        cached_file->fd = ::open(cache_filename(path_raw).c_str(), O_RDWR);
        if (cached_file->fd > 0) {
            struct stat st;
            memset(&st, 0, sizeof(struct stat));
            ::fstat(cached_file->fd, &st);
            if (st.st_size != cached_file->resource.stat.st_size) {
                std::cerr << "size differs local:" << st.st_size << " remote:" << cached_file->resource.stat.st_size << std::endl;
                ::close(cached_file->fd);
                remove(path_raw);
                return item_p();
            }
        }
        update(path_raw, *cached_file);
        return cached_file;
    }
    
    int create_file(const std::string& path_raw) const {
        const std::string path = cache_filename(path_raw);
        const char *filename = strrchr(path.c_str(), '/');
        filename++;
        const std::string folder(path.c_str(), filename);
        
        if (mkdir_p(folder)) {
            fprintf(stderr, "## mkdir(%s) error\n", folder.c_str());      
            return -1;
        }
        int fd = open(path.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if (fd == -1)
            fprintf(stderr, "## open(%s) error\n", path.c_str());
        
        wdfs_dbg("%s(%s) cached file %s created\n", __func__, path_raw.c_str(), path.c_str());
        
        return fd;        
        
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
        ::unlink(cache_filename(path_raw).c_str());
    }
    
private:
    std::string folder_;
    data_t cache_;
};


#endif /*CACHE_H_*/
