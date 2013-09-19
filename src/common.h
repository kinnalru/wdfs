
#ifndef WDFS_TYPE_H
#define WDFS_TYPE_H

#include <sys/stat.h>
#include <string>

//#include <optional>
#include <boost/optional.hpp>

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
    
    

#define test(field, type) \
    type field() const {};
    
struct webdav_resource_t {
    
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
    
    etag_t etag;
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

