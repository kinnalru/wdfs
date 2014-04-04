
#ifndef WDFS_TYPE_H
#define WDFS_TYPE_H

#include <sys/stat.h>
#include <string.h>
#include <memory>
#include <string>
#include <stdexcept>


#include <boost/optional.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/optional.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

typedef struct ne_session_s ne_session;
typedef boost::optional<std::string> etag_t;
typedef std::shared_ptr<char> string_p;

typedef std::map<std::string, struct stat> stats_t;

struct webdav_exception_t : public std::runtime_error
{
    webdav_exception_t(const std::string& msg, int code)
        : std::runtime_error(msg)
        , code_(code)
    {}
    
    int code() const {
        return code_;
    }
    
private:
    int code_;
};

struct api_exception_t : public std::runtime_error
{
    api_exception_t(const std::string& message, int e)
        : std::runtime_error((message + ": " + strerror(e)).c_str())
        , errno_(e)
    {}
  
    api_exception_t(int e)
        : std::runtime_error(strerror(e))
        , errno_(e)
    {}
    
    int error() const {
        return errno_;
    }
    
private:
    int errno_;
};

/* used as mode for unify_path() */
enum string_mode_e {
    NONE       = 0,
    ESCAPE     = 1 << 1,
    UNESCAPE   = 1 << 2,
    /* do not remove trailing slashes */
    LEAVESLASH = 1 << 3
};

/*
struct file_t {
    
    file_t(const std::string& filename)
        : filename_(filename)
        , fd_(::open(filename.c_str(), O_RDWR))
        , remove_(false)
    {
        if (fd_ == -1) {
            throw api_exception_t("Can't open file", errno);
        }
        update_stat();
    }
    
    ~file_t() {
        if (fd_ != -1) {
            ::close(fd_);
        }
        if (remove_) {
            ::remove(filename_.c_str());
        }
    }
    
    const struct stat& stat() const {return stat_;}
    
    void set_remove(bool r) {remove_ = r;}
    
private:
  
    file_t(const file_t& other) : fd_(-1) {}
    file_t& operator=(const file_t& other) {}
    
  
    void update_stat() {
        if (::fstat(fd_, &stat_)) {
            throw api_exception_t("Can't update file stat", errno);
        }
    }
private:
    const std::string filename_;
    const int fd_;
    bool remove_;
    struct stat stat_;
};*/

inline bool differ(const struct stat& s1, const struct stat s2) {
    return s1.st_mtime != s2.st_mtime || s1.st_size != s2.st_size;
}

#define wrap_stat_field(field, type, object) \
    type field() const {return object.st_##field;}; \
    bool has_##field() const {return object.st_##field != 0;}; \
    void update_##field(type f) {if (f) object.st_##field = f;};

struct resource_t {
    resource_t () {}
    virtual ~resource_t() {}
    
    virtual const struct stat& stat() const = 0;
    virtual struct stat& stat() = 0;
    
    virtual bool differ(const resource_t& other) const {
        return ::differ(stat(), other.stat());
    }
    
    wrap_stat_field(mtime, time_t, stat());
    wrap_stat_field(size, off_t, stat());
    wrap_stat_field(mode, mode_t, stat());
};

  
class webdav_resource_t {
    friend class boost::serialization::access;
    
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
//         std::string statstr = to_string(stat);
//         ar & statstr;
//         if (auto startptr = from_string<struct stat>(statstr)) {
//             stat = *startptr;
//         }
    }
    
public:
    webdav_resource_t() {
        memset(&stat_, 0, sizeof(struct stat));
    };
    
    webdav_resource_t(const struct stat& st) {
        stat_ = st;
    }
    
    const struct stat& stat() const {return stat_;}
    struct stat& stat() {return stat_;}
    
    virtual bool differ(const webdav_resource_t& other) const {
        return ::differ(stat(), other.stat());
    }
    
    wrap_stat_field(mtime, time_t, stat());
    wrap_stat_field(size, off_t, stat());
    wrap_stat_field(mode, mode_t, stat());
    
//     bool differ(const webdav_resource_t& other) const {
//         return ::differ(stat, other.stat);
//     }
//     
//     void update_from(const webdav_resource_t& other) {
//         update_mtime(other.mtime());
//         update_size(other.size());
//     }
    
    struct stat stat_;
};

// class cached_resource_t : public resource_t {
//   
// public:
//     cached_resource_t(const std::string& filename) 
//         : file_(filename)
//     {
//         
//     }
//     
//     virtual const struct stat& stat() const {return file_.stat();}
//     virtual struct stat& stat() {throw std::runtime_error("Can't assign stat in real file");}
// 
//     file_t& file() {return file_;}
//     
// private:
//     file_t file_;
// };



template <typename T>
inline std::string to_string(const T& rawdata) {
    const char* data = static_cast<const char*>(static_cast<const void*>(&rawdata));
    std::string strdata;
    std::for_each(data, data + sizeof(T), [&strdata] (char c) {
        strdata += std::to_string(c) + '_';
    });
    return strdata;
}

template <typename T>
std::unique_ptr<T> from_string(const std::string& strdata) {
    std::vector<char> data;
    size_t pos = 0;
    while (pos < strdata.size()) {
        size_t finded = strdata.find('_', pos);
        if (finded == std::string::npos || finded == strdata.size()) break;
        
        std::string number = strdata.substr(pos, finded - pos);
        data.push_back(atoi(number.c_str()));
        pos = finded + 1;
    }
    std::unique_ptr<T> rawdata;
    if (data.size() == sizeof(T)) {
        rawdata.reset(new T);
        memcpy(rawdata.get(), &data[0], sizeof(T));
    }

    return rawdata;
}


    

struct webdav_context_t {
    ne_session* session;
    
    webdav_resource_t resource;
};

/* removes all trailing slashes from the path. 
 * returns the new malloc()d path or NULL on error.  */
char* remove_ending_slashes(const char *path);

/* unifies the given path by removing the ending slash and escaping or 
 * unescaping the path. returns the new malloc()d string or NULL on error. */
char* unify_path(const char *path_in, int mode);

/* free()s each char passed that is not NULL and sets it to NULL after freeing */
void free_chars(char **arg, ...);

inline string_p mk_string(const std::string& string) {
    return string_p(strdup(string.c_str()), free);
}

inline string_p mk_string(const string_p& string) {
    return string_p(strdup(string.get()), free);
}

std::string canonicalize(const std::string& path, string_mode_e mode = NONE);
string_p canonicalize(const string_p& path, string_mode_e mode = NONE);

int mkdir_p(const std::string& path);

std::pair<std::string, std::string> parse_netrc(const std::string& file, const std::string& host);

/* takes an lvalue and sets it to NULL after freeing. taken from neon. */
#define FREE(x) do { if ((x) != NULL) free((x)); (x) = NULL; } while (0)

inline std::string normalize_etag(const char *etag)
{
    if (!etag) return std::string();

    const char * e = etag;
    if (*e == 'W') return std::string();
    if (!*e) return std::string();

    return std::string(etag);
}






#endif

