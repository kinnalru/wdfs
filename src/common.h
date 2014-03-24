
#ifndef WDFS_TYPE_H
#define WDFS_TYPE_H

#include <sys/stat.h>
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

/* used as mode for unify_path() */
enum {
    ESCAPE     = 0x0,
    UNESCAPE   = 0x1,
    /* do not remove trailing slashes */
    LEAVESLASH = 0x2
};


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

#define wrap_stat_field(field, type, object) \
    type field() const {return object.st_##field;}; \
    bool has_##field() const {return object.st_##field != 0;}; \
    void update_##field(type f) {if (f) object.st_##field = f;};
    
    
class webdav_resource_t {
    friend class boost::serialization::access;
    
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        std::string statstr = to_string(stat);
        ar & statstr;
        if (auto startptr = from_string<struct stat>(statstr)) {
            stat = *startptr;
        }
    }
    
public:
    webdav_resource_t() {
        memset(&stat, 0, sizeof(struct stat));
    };
    
    
    wrap_stat_field(mtime, time_t, stat);
    wrap_stat_field(size, off_t, stat);
    
    void update_from(const webdav_resource_t& other) {
        update_mtime(other.mtime());
        update_size(other.size());
    }
    
    struct stat stat;
};

struct webdav_context_t {
    ne_session* session;
    
    webdav_resource_t resource;
};

/*макрос для печати отладочной информации. Если приживется...*/
#define wdfs_dbg(format, arg...) do { \
        if (wdfs.debug == true) \
            fprintf(stderr,">>  " format, ## arg);\
    } while (0)
    
#define wdfs_err(format, arg...) do { \
        if (wdfs.debug == true) \
            fprintf(stderr,"##  " format, ## arg);\
    } while (0)    
    
#define wdfs_pr(format, arg...) do { \
        if (wdfs.debug == true) \
            fprintf(stderr, format, ## arg);\
    } while (0)    

/* removes all trailing slashes from the path. 
 * returns the new malloc()d path or NULL on error.  */
char* remove_ending_slashes(const char *path);

/* unifies the given path by removing the ending slash and escaping or 
 * unescaping the path. returns the new malloc()d string or NULL on error. */
char* unify_path(const char *path_in, int mode);

/* free()s each char passed that is not NULL and sets it to NULL after freeing */
void free_chars(char **arg, ...);

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

