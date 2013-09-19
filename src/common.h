
#ifndef WDFS_TYPE_H
#define WDFS_TYPE_H

#include <sys/stat.h>
#include <memory>
#include <string>

#include <boost/optional.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

typedef struct ne_session_s ne_session;
typedef boost::optional<std::string> etag_t;

/* used as mode for unify_path() */
enum {
    ESCAPE     = 0x0,
    UNESCAPE   = 0x1,
    /* do not remove trailing slashes */
    LEAVESLASH = 0x2
};

#define wrap_stat_field(field, type, object) \
    type field() const {return object.st_##field;}; \
    bool has_##field() const {return object.st_##field != 0;}; \
    void update_##field(type f) {if (f) object.st_##field = f;};
    
template <typename T>
inline std::string to_string(const T& rawdata) {
    const char* data = static_cast<const char*>(static_cast<const void*>(&rawdata));
    std::string result;
    for (unsigned int i = 0; i < sizeof(T); ++i) {
        result += std::to_string(data[i]) + '_';
    }
    return result;
}

template <typename T>
std::unique_ptr<T> from_string(const std::string& strdata) {
    std::vector<char> data;
    size_t pos = 0;
    while(pos < strdata.size()) {
        size_t finded = strdata.find('_', pos);
        if (finded == std::string::npos || finded == strdata.size()) break;
        
        std::string number = strdata.substr(pos, finded - pos);
        data.push_back(atoi(number.c_str()));
        pos = finded + 1;
    }
    std::cerr << "strdata:" << strdata << std::endl;
    std::cerr << "sizeof:" << sizeof(T) << std::endl;
    std::cerr << "data:" << data.size() << std::endl;
    assert(data.size() == sizeof(T));
    std::unique_ptr<T> rawdata(new T);
    memcpy(rawdata.get(), &data[0], sizeof(T));
    return rawdata;
}
    
struct webdav_resource_t {
private:
    friend class boost::serialization::access;
    
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & degrees;
        ar & minutes;
        ar & seconds;
    }
public:
    webdav_resource_t() {
        memset(&stat, 0, sizeof(struct stat));
    };
    
    
    wrap_stat_field(mtime, time_t, stat);
    wrap_stat_field(size, off_t, stat);
    
    void update_from(const webdav_resource_t& other) {
        etag = other.etag;
        update_mtime(other.mtime());
        update_size(other.size());
    }
    
    friend std::ostream& operator<<(std::ostream& os, const webdav_resource_t& resource);
    friend std::istream& operator>>(std::istream& is, webdav_resource_t& resource);
    
    etag_t etag;
    struct stat stat;
};

inline std::ostream& operator<<(std::ostream& os, const webdav_resource_t& resource)
{
    os << ((resource.etag) ? resource.etag.get() : "") << std::endl;
    os << to_string(resource.stat) << std::endl;
    return os;
}

inline std::istream& operator>>(std::istream& is, webdav_resource_t& resource)
{
    std::string etag;
    is >> std::noskipws;
    is >> etag;
    is >> std::skipws;
    std::cerr << "ETAG:" << etag << std::endl;
    resource.etag.reset(etag);
    std::string stat;
    is >> stat;
    resource.stat = *from_string<struct stat>(stat);
    return is;
}

struct webdav_context_t {
    ne_session* session;
    
    webdav_resource_t resource;
};

/*макрос для печати отладочной информации. Если приживется...*/
#define wdfs_dbg(format, arg...) do { \
        if (wdfs.debug == true) \
            fprintf(stderr,">>  " format, ## arg);\
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

