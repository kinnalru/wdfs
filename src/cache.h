#ifndef CACHE_H_
#define CACHE_H_

#include <string>
#include <map>
#include <memory>
#include <boost/foreach.hpp>
#include <boost/filesystem.hpp>
#include <boost/concept_check.hpp>

#include "common.h"
#include "log.h"

typedef std::unique_ptr<struct stat> stat_p;

// class cached_file_t {
//     friend class boost::serialization::access;
// 
//     template<class Archive>
//     void serialize(Archive & ar, const unsigned int version)
//     {
//         ar & resource_;
//     }
//     
// public:
//     cached_file_t(int f = -1) : fd_ (f) {};
//     cached_file_t(const struct stat& s) : resource_(s), fd_(-1) {};
//     cached_file_t(const webdav_resource_t& r, int f) : resource_(r), fd_(f) {};
//   
//     bool differ(const cached_file_t& other) const {
//         return resource_.differ(other.resource_);
//     }
//     
//     bool differ(const struct stat& other) const {
//         return ::differ(stat(), other);
//     }
//     
//     struct stat stat() const {
//         if (fd_ != -1) {
//             struct stat st;
//             if (::fstat(fd_, &st)) {
//                 throw std::runtime_error("achtunng fstat");
//             }
//             return st;
//         }
//         else {
//             return resource_.stat;
//         }
//     }
//     
//     int fd() const {
//         return fd_;
//     }
//     
//     void set_fd(int fd) {
//         fd_ = fd;
//     }
//     const webdav_resource_t& resource() const {
//         return resource_;
//     }
//     
// private:
//     webdav_resource_t resource_;
//     int fd_;
// };

/*! \brief Кеш
 * Кеш основан на файловой системе.
 * stat берется с файловой системы и обновляется на ней
 * если есть расходжение - файл удаляется и загружается заново
 * 
 */

class cache_t {
    friend class boost::serialization::access;

    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & cache_;
    }
    
public:
    typedef std::shared_ptr<webdav_resource_t> item_p;
    typedef std::map<std::string, item_p> data_t;
    

    cache_t(const std::string& folder, const std::string& prefix)
        : folder_(folder), prefix_(prefix)
    {}
    
    std::string normalize(const std::string& path_raw) const {
        //std::shared_ptr<char> path(unify_path(path_raw.c_str(), UNESCAPE | LEAVESLASH), free);
        //return std::string(path.get());
        return canonicalize(path_raw);
    }

    std::string cache_filename(const std::string& path_raw) const {
        const std::string path = normalize(path_raw);
        if (path.find(folder_) != std::string::npos) {
            return path;
        }
        else {
            return folder_+ "/files/" + path;        
        }
    }
    
    std::string internal_filename(std::string fullpath) const {
        std::cerr << "folder:" << folder_ << std::endl;
        if (fullpath.find(folder_+ "/files/") != std::string::npos) {
            fullpath = fullpath.substr((folder_+ "/files/").size());
            return fullpath;
        }
        assert(!"cant make internal filename");
    }
    
    inline int size() const {return cache_.size();}
    
//     stat_p stat(const std::string& path_raw) const {
//         if (auto item = get(path_raw)) {
//             return stat_p(new struct stat(item->stat()));
//         }
//         else {
//             return stat_p();
//         }
//     }
    
    void add(const std::string& path_raw, item_p item) {
        const std::string path = normalize(path_raw);
        LOG_ENTER(path_raw + " normalized: [" + path + "]");
        assert(cache_.find(path) == cache_.end());
        cache_[path] = item;
    }
    
    void update(const std::string& path_raw, item_p item) {
        const std::string path = normalize(path_raw);
        LOG_ENTER(path_raw + " normalized: [" + path + "]");        
        cache_[path] = item;
    }    

    void update(const stats_t& stats) {
        LOG_ENEX("", "");
        BOOST_FOREACH(auto p, stats) {
            wdfs_dbg("Handling [%s]\n", p.first.c_str());
            cache_t::item_p new_item(new webdav_resource_t(p.second));
            if (cache_t::item_p old_item = get(p.first)) {
                if (new_item->differ(*old_item)) {
                    wdfs_dbg("New item differs: removing [%s]\n", p.first.c_str());
                    wdfs_dbg("old: %s\n ", to_string(old_item->stat()).c_str());
                    wdfs_dbg("new: %s\n ", to_string(new_item->stat()).c_str());
                    
                    remove(p.first);
                    add(p.first, new_item);
                }
                wdfs_dbg("Nothing to do\n");
            }
            else {
                add(p.first, new_item);
            }
        }
    }
    
    virtual item_p get(const std::string& path_raw) const {
        const std::string path = normalize(path_raw);
        LOG_ENTER(path_raw + " normalized: [" + path + "]");
        data_t::const_iterator it = cache_.find(path);
        if (it != cache_.end()) {
            return it->second;
        }
        else {
            return item_p();
        }
    }
    
  
//     item_p restore(const std::string& path_raw) {
//         auto cached_file = get(path_raw);
//         std::shared_ptr<cached_resource_t> new_file(new cached_resource_t(cache_filename(path_raw)));
// 
//         if (new_file->differ(*cached_file)) {
//             new_file->file().set_remove(true);
//             return cached_file;
//         }
//         else {
//             update(path_raw, new_file);
//             return new_file;
//         }
//     }
    
    std::unique_ptr<fuse_file_t> create_file(const std::string& path_raw) const {
        const std::string path = cache_filename(path_raw);
        const char *filename = strrchr(path.c_str(), '/');
        filename++;
        const std::string folder(path.c_str(), filename);
        
        LOG_ENEX(path_raw + " normalized: [" + path + "]", "");
        
        if (mkdir_p(folder)) {
            throw api_exception_t("mkdir() error for " + folder, errno);
        }
        int fd = open(path.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if (fd == -1) {
            throw api_exception_t("open() error for " + path, errno);
        }
        
        return std::unique_ptr<fuse_file_t>(new fuse_file_t(path, fd));
    }

    std::unique_ptr<fuse_file_t> get_file(const std::string& path_raw) const {
        return create_file(path_raw);
    }
    
    std::tuple<item_p, std::unique_ptr<fuse_file_t> > get_resource_and_file(const std::string& path_raw) const {
        
        if (auto resource = get(path_raw)) {
            wdfs_dbg("cache updated: opening file...\n");
            std::unique_ptr<fuse_file_t> file = create_file(path_raw);
            
            struct stat st;
            if (::fstat(file->fd, &st)) {
                wdfs_err("1. fstat error\n");
                file.reset();
            }
            else if(differ(st, resource->stat())) {
                wdfs_err("2. cached file differ!\n");
                wdfs_dbg("disk: %s\n ", to_string(st).c_str());
                wdfs_dbg("cache: %s\n ", to_string(resource->stat()).c_str());
                file.reset();
            }
            else {
                wdfs_dbg("2. cached file OK nothing to do!\n");
            }
            return std::make_tuple(resource, std::unique_ptr<fuse_file_t>(file.release()));
        }
        else {
            return std::make_tuple(item_p(), std::unique_ptr<fuse_file_t>());
        }
    }

    
    std::vector<std::string> infolder(const std::string& path_raw) {
        std::string path = normalize(path_raw);
        LOG_ENEX(path_raw + " normalized: [" + path + "]", "");
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
    
//     template <typename ToLocalFile>
    std::vector<std::string> files_in_folder(const std::string& path_raw/*, const ToLocalFile& tlf*/) {
        namespace fs = boost::filesystem;
        
        fs::path path(cache_filename(path_raw));
        
        std::vector<std::string> files;
        if (fs::exists(path) && fs::is_directory(path))
        {
            fs::directory_iterator dend;
            for (fs::directory_iterator dir(path) ; dir != dend ; ++dir)
            {
                files.push_back(internal_filename(dir->path().c_str()));
            }
        }
        return files;
        
        //std::string path = normalize(path_raw);
        //LOG_ENEX(path_raw + " normalized: [" + path + "]", "");
        //std::string folder_suffix = (get_file(path_raw)->stat().st_mode || S_IFDIR) ? "/" : "";
        //path += folder_suffix;
        
        //const char *filename = strrchr(path.c_str(), '/');
        //filename++;
        //const std::string folder(path.c_str(), filename);
        //std::vector<std::string> files;
        
    }
    
    virtual void remove(const std::string& path_raw) {
        const std::string path = normalize(path_raw);
        wdfs_dbg("%s(%s) normalized: [%s] cached filename: [%s]\n", __func__, path_raw.c_str(), path.c_str(), cache_filename(path_raw).c_str());
        cache_.erase(path);
        //::remove(cache_filename(path_raw).c_str());
    }
    
    
    
    
    
    bool fs_exists(const boost::filesystem::path& path) const {
        return boost::filesystem::exists(path);
    }
    
    bool fs_is_directory(const boost::filesystem::path& path) const {
        return boost::filesystem::is_directory(path);
    }
    
    bool fs_regular_file(const boost::filesystem::path& path) const {
        return boost::filesystem::is_regular_file(path);
    }
    
    bool fs_create_directory(const boost::filesystem::path& path) const {
        return (fs_exists(path) && fs_is_directory(path)) || boost::filesystem::create_directory(path);
    }
    
    std::unique_ptr<fuse_file_t> fs_create_file(const boost::filesystem::path& path) const {
        int fd = ::open(path.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if (fd == -1) {
            throw api_exception_t("open() error for " + std::string(path.c_str()), errno);
        }
        
        return std::unique_ptr<fuse_file_t>(new fuse_file_t(path.c_str(), fd));
    }
    
    
private:
    std::string folder_;
    std::string prefix_;
    data_t cache_;
};


#endif /*CACHE_H_*/
