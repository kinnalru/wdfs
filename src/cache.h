#ifndef CACHE_H_
#define CACHE_H_

#include <string>
#include <map>
#include <memory>
#include <boost/concept_check.hpp>

#include "common.h"

typedef std::unique_ptr<struct stat> stat_p;

class cached_file_t {
    friend class boost::serialization::access;

    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & resource_;
    }
    
public:
    cached_file_t(int f = -1) : fd_ (f) {};
    cached_file_t(const struct stat& s) : resource_(s), fd_(-1) {};
    cached_file_t(const webdav_resource_t& r, int f) : resource_(r), fd_(f) {};
  
    bool differ(const cached_file_t& other) const {
        return resource_.differ(other.resource_);
    }
    
    bool differ(const struct stat& other) const {
        return ::differ(stat(), other);
    }
    
    struct stat stat() const {
        if (fd_ != -1) {
            struct stat st;
            if (::fstat(fd_, &st)) {
                throw std::runtime_error("achtunng fstat");
            }
            return st;
        }
        else {
            return resource_.stat;
        }
    }
    
    int fd() const {
        return fd_;
    }
    
    void set_fd(int fd) {
        fd_ = fd;
    }
    const webdav_resource_t& resource() const {
        return resource_;
    }
    
private:
    webdav_resource_t resource_;
    int fd_;
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

    cache_t(const std::string& folder, const std::string& prefix)
        : folder_(folder), prefix_(prefix)
    {}
    
    std::string normalize(const std::string& path_raw) const {
        std::shared_ptr<char> path(unify_path(path_raw.c_str(), UNESCAPE), free);
        return std::string(path.get());
    }

    std::string cache_filename(const std::string& path_raw) const {
        const std::string path = normalize(path_raw);
        if (path.find(folder_) != std::string::npos) {
            return path;
        }
        else {
            return folder_+ "/files/" + prefix_ + "/" + path;        
        }
    }
    
    inline int size() const {return cache_.size();}
    
    stat_p stat(const std::string& path_raw) const {
        if (auto item = get(path_raw)) {
            return stat_p(new struct stat(item->stat()));
        }
        else {
            return stat_p();
        }
    }
    
    virtual item_p get(const std::string& path_raw) const {
        const std::string path = normalize(path_raw);
        data_t::const_iterator it = cache_.find(path);
        if (it != cache_.end()) {
            return item_p(new item(it->second));
        }
        else {
            return item_p();
        }
    }
    
    item_p restore(const std::string& path_raw) {
        auto cached_file = get(path_raw);
        assert(cached_file->fd() == -1);
        item_p new_file(new item(::open(cache_filename(path_raw).c_str(), O_RDWR)));
        
        if (new_file->fd() != -1)
        {
            if (new_file->differ(cached_file->stat())) {
                ::close(new_file->fd());
                ::unlink(cache_filename(path_raw).c_str());
            }
            else {
                update(path_raw, *new_file);
                return new_file;
            }
        }
        
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
        cache_[path] = v;
    }
    
    void update(const std::string& path_raw, const item& v) {
        const std::string path = normalize(path_raw);
        cache_[path] = v;
    }
    
    std::vector<std::string> infolder(const std::string& path_raw) {
        std::string path = normalize(path_raw);
        
        std::string folder_suffix = (get(path_raw)->stat().st_mode || S_IFDIR) ? "/" : "";
        path += folder_suffix;
        
        const char *filename = strrchr(path.c_str(), '/');
        filename++;
        const std::string folder(path.c_str(), filename);
        std::vector<std::string> files;
        std::for_each(cache_.begin(), cache_.end(),
          [&] (const data_t::value_type& p) {
            if (p.first.empty()) return;
            const char *f = strrchr(p.first.c_str(), '/');
            f++;
            const std::string f2(p.first.c_str(), f);
            if (f2 == folder) {
                files.push_back(p.first);
            }
          }
        );
        return files;
    }
    
    virtual void remove(const std::string& path_raw) {
        const std::string path = normalize(path_raw);
        cache_.erase(path);
        ::unlink(cache_filename(path_raw).c_str());
    }
    
private:
    std::string folder_;
    std::string prefix_;
    data_t cache_;
};


#endif /*CACHE_H_*/
