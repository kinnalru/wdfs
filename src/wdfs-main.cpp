/*
 *  this file is part of wdfs --> http://noedler.de/projekte/wdfs/
 *
 *  wdfs is a webdav filesystem with special features for accessing subversion
 *  repositories. it is based on fuse v2.5+ and neon v0.24.7+.
 *
 *  copyright (c) 2005 - 2007 jens m. noedler, noedler@web.de
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *  This program is released under the GPL with the additional exemption
 *  that compiling, linking and/or using OpenSSL is allowed.
 */

#include <string.h>
#include <errno.h>
#include <assert.h>
#include <glib.h>
#include <fuse_opt.h>
#include <ne_props.h>
#include <ne_dates.h>
#include <ne_redirect.h>

#include <memory>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>

#include <boost/foreach.hpp>

#include "wdfs-main.h"
#include "wdfs_controller.h"
#include "webdav.h"
#include "cache.h"
#include "log.h"



/* there are four locking modes available. the simple locking mode locks a file 
 * on open()ing it and unlocks it on close()ing the file. the advanced mode 
 * prevents data curruption by locking the file on open() and holds the lock 
 * until the file was writen and closed or the lock timed out. the eternity 
 * mode holds the lock until wdfs is unmounted or the lock times out. the last
 * mode is to do no locking at all which is the default behaviour. */
#define NO_LOCK 0
#define SIMPLE_LOCK 1
#define ADVANCED_LOCK 2
#define ETERNITY_LOCK 3

std::unique_ptr<cache_t> cache;
std::unique_ptr<wdfs_controller_t> wdfs;

static void print_help();
static int call_fuse_main(struct fuse_args *args);

/* define package name and version if config.h is not available. */
#ifndef HAVE_CONFIG_H
    #define PACKAGE_NAME 	"wdfs"
    #define PACKAGE_VERSION	"unknown"
#endif

/* product string according RFC 2616, that is included in every request.     */
const char *project_name = PACKAGE_NAME "/" PACKAGE_VERSION;

/* homepage of this filesystem                                               */
const char *project_uri = "http://noedler.de/projekte/wdfs/";

/* init settings with default values */
struct wdfs_conf wdfs_cfg = [] () {
    struct wdfs_conf w;
    w.debug = false;
    w.accept_certificate = false;
    w.redirect = true;
    w.locking_mode = NO_LOCK;
    w.locking_timeout = 300;
    w.cachedir1 = 0;
    return w;
} ();

enum {
    KEY_HELP,
    KEY_VERSION,
    KEY_VERSION_FULL,
    KEY_DEBUG,
    KEY_LOCKING_MODE,
    KEY_NOOP,
};

#define WDFS_OPT(t, p, v) { t, offsetof(struct wdfs_conf, p), v }

static struct fuse_opt wdfs_opts[] = {
    FUSE_OPT_KEY("-h",				KEY_HELP),
    FUSE_OPT_KEY("--help",			KEY_HELP),
    FUSE_OPT_KEY("-v",				KEY_VERSION),
    FUSE_OPT_KEY("--version",		KEY_VERSION),
    FUSE_OPT_KEY("-vv",				KEY_VERSION_FULL),
    FUSE_OPT_KEY("--all-versions",	KEY_VERSION_FULL),
    FUSE_OPT_KEY("-D",				KEY_DEBUG),
    FUSE_OPT_KEY("wdfs_debug",		KEY_DEBUG),
    FUSE_OPT_KEY("-m %u",			KEY_LOCKING_MODE),
    FUSE_OPT_KEY("-a",				KEY_NOOP),
    WDFS_OPT("-D",					debug, true),
    WDFS_OPT("wdfs_debug",			debug, true),
    WDFS_OPT("-ac",					accept_certificate, true),
    WDFS_OPT("accept_sslcert",		accept_certificate, true),
    WDFS_OPT("-u %s",				username, 0),
    WDFS_OPT("username=%s",			username, 0),
    WDFS_OPT("-p %s",				password, 0),
    WDFS_OPT("password=%s",			password, 0),
    WDFS_OPT("cachedir=%s",     cachedir1, 0),    
    WDFS_OPT("no_redirect",			redirect, false),
    WDFS_OPT("-l",					locking_mode, SIMPLE_LOCK),
    WDFS_OPT("locking",				locking_mode, SIMPLE_LOCK),
    WDFS_OPT("locking=0",			locking_mode, NO_LOCK),
    WDFS_OPT("locking=none",		locking_mode, NO_LOCK),
    WDFS_OPT("locking=1",			locking_mode, SIMPLE_LOCK),
    WDFS_OPT("locking=simple",		locking_mode, SIMPLE_LOCK),
    WDFS_OPT("locking=2",			locking_mode, ADVANCED_LOCK),
    WDFS_OPT("locking=advanced",	locking_mode, ADVANCED_LOCK),
    WDFS_OPT("locking=3",			locking_mode, ETERNITY_LOCK),
    WDFS_OPT("locking=eternity",	locking_mode, ETERNITY_LOCK),
    WDFS_OPT("-t %u",				locking_timeout, 300),
    WDFS_OPT("locking_timeout=%u",	locking_timeout, 300),
    FUSE_OPT_END
};

static int wdfs_opt_proc(
    void *data, const char *option, int key, struct fuse_args *option_list)
{
    switch (key) {
        case KEY_HELP:
            print_help();
            fuse_opt_add_arg(option_list, "-ho");
            call_fuse_main(option_list);
            exit(1);

        case KEY_VERSION:
            fprintf(stderr, "%s version: %s\n", PACKAGE_NAME, PACKAGE_VERSION);
            exit(0);

        case KEY_VERSION_FULL:
            fprintf(stderr, "%s version: %s\n", PACKAGE_NAME, PACKAGE_VERSION);
            fprintf(stderr, "%s homepage: %s\n", PACKAGE_NAME, project_uri);
            fprintf(stderr, "neon version: 0.%d\n", NEON_VERSION);
            fuse_opt_add_arg(option_list, "--version");
            call_fuse_main(option_list);
            exit(0);

        case KEY_DEBUG:
            return fuse_opt_add_arg(option_list, "-f");

        case KEY_LOCKING_MODE:
            if (option[3] != '\0' || option[2] < '0' || option[2] > '3') {
                fprintf(stderr, "%s: unknown locking mode '%s'\n",
                wdfs_cfg.program_name, option + 2);
                exit(1);
            } else {
                wdfs_cfg.locking_mode = option[2] - '0';
            }
            return 0;

        case KEY_NOOP:
            return 0;

        case FUSE_OPT_KEY_NONOPT:
            if (wdfs_cfg.webdav_resource.empty() && 
                    strncmp(option, "http", 4) == 0) {
                wdfs_cfg.webdav_resource = option;
                return 0;
            }
            else if (wdfs_cfg.mountpoint.empty()) {
                wdfs_cfg.mountpoint = option;
                return 1;
            }
            return 1;

        case FUSE_OPT_KEY_OPT:
            return 1;

        default:
            fprintf(stderr, "%s: unknown option '%s'\n",
                wdfs_cfg.program_name, option);
            exit(1);
    }
}


/* webdav server base directory. if you are connected to "http://server/dir/"
 * remotepath_basedir is set to "/dir" (starting slash, no ending slash).
 * if connected to the root directory (http://server/) it will be set to "". */
char *remotepath_basedir = 0;

struct readdir_ctx_t {
    void *buf;
    fuse_fill_dir_t filler;
    std::shared_ptr<char> remotepath;
    std::vector<std::string> oldfiles;
};

/* infos about an open file. used by open(), read(), write() and release()   */
struct fuse_file_t {
    fuse_file_t() : fd(-1), modified(false) {}
    
    int fd;	        // this file's filehandle
    bool modified;	// set true if the filehandle's content is modified 
};


/* returns a filehandle for read and write on success or -1 on error */
static int get_filehandle()
{
    char dummyfile[] = "/tmp/wdfs-tmp-XXXXXX";
    /* mkstemp() replaces XXXXXX by unique random chars and
        * returns a filehandle for reading and writing */
    int fd = mkstemp(dummyfile);
    if (fd == -1)
        fprintf(stderr, "## mkstemp(%s) error\n", dummyfile);
    if (unlink(dummyfile))
        fprintf(stderr, "## unlink() error\n");
    return fd;
}

std::string get_filename(const char* remotepath) {
    /* extract filename from the path. it's the string behind the last '/'. */
    const char *filename = strrchr(remotepath, '/');
    filename++;
    return filename;
}


/* +++ fuse callback methods +++ */


/* this method returns the file attributes (stat) for a requested file either
 * from the cache or directly from the webdav server by performing a propfind
 * request. */
static int wdfs_getattr(const char *localpath, struct stat *stat)
{
    LOG_ENEX(localpath, "");  
    try {
        auto fullpath(wdfs->local2full(localpath));
        if (!fullpath) return -ENOMEM;
        
        wdfs_dbg("localpath: [%s]\n", localpath);
        wdfs_dbg("fullpath: [%s]\n", fullpath.get());
        
        if (auto resource = cache->get(fullpath.get())) {
            wdfs_dbg("cache hit: applying stat\n");
            *stat = resource->stat();
        } 
        else {
            wdfs_dbg("cache NOT hit: updating...\n");
            auto oldfiles = cache->infolder(fullpath.get());
            auto stats = webdav_getattrs(session, fullpath, *wdfs);
            
            oldfiles.erase(
                std::remove_if(oldfiles.begin(), oldfiles.end(), [&stats] (const std::string& s) {
                    return stats.find(s) != stats.end();
                }),
                oldfiles.end()
            );
            
            BOOST_FOREACH(auto file, oldfiles) {
                wdfs_dbg("removing unexistent file: [%s]\n", file.c_str());
                cache->remove(file);
            }
            
            cache->update(stats);
            if (auto resource = cache->get(fullpath.get())) {
                wdfs_dbg("cache updated: applying stat\n");
                *stat = resource->stat();
            }
            else {
                wdfs_dbg("error\n");
                return -EFAULT;
            }
        }
        
        return 0;
    }
    catch (const webdav_exception_t& e) {
        wdfs_err("Error in %s: %s\n", __func__, e.what());
        return e.code();
    }
    catch (const std::exception& e) {
        wdfs_err("Error in %s: %s\n", __func__, e.what());
        return -EFAULT;
    }
    catch (...) {
        wdfs_err("Unknown error in %s\n", __func__);
        return -EFAULT;
    }
}

/* this method adds the files to the requested directory using the webdav method
 * propfind. the server responds with status code 207 that contains metadata of 
 * all files of the requested collection. for each file the method 
 * wdfs_readdir_propfind_callback() is called. */
static int wdfs_readdir(
    const char *localpath, void *buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info *fi)
{
    assert(localpath && filler);    
    LOG_ENEX(localpath, "");

    try {
        auto filldir = wdfs->local2full(localpath);
        auto oldfiles = cache->infolder(filldir.get());
        auto stats = webdav_readdir(session, filldir, *wdfs);
        
        oldfiles.erase(
            std::remove_if(oldfiles.begin(), oldfiles.end(), [&stats] (const std::string& s) {
                return stats.find(s) != stats.end();
            }),
            oldfiles.end()
        );
        
        BOOST_FOREACH(auto file, oldfiles) {
            wdfs_dbg("removing unexistent file: [%s]\n", file.c_str());
            cache->remove(file);
        }

        cache->update(stats);
            
        /* add directory entry */
        BOOST_FOREACH(auto p, stats) {
            auto filename = p.first;
            filename = get_filename(filename.c_str());
            wdfs_dbg("fill file: [%s]\n", filename.c_str());
            if (filler(buf, filename.c_str(), &p.second, 0))
                wdfs_err("Can't fill fuse file: [%s]\n", filename.c_str());
        }

            
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_mode = S_IFDIR | 0777;
        filler(buf, ".", &st, 0);
        filler(buf, "..", &st, 0);

        return 0;
    }
    catch (const webdav_exception_t& e) {
        wdfs_err("Error in %s: %s\n", __func__, e.what());
        return e.code();
    }
    catch (const std::exception& e) {
        wdfs_err("Error in %s: %s\n", __func__, e.what());
        return -EFAULT;
    }
    catch (...) {
        wdfs_err("Unknown error in %s\n", __func__);
        return -EFAULT;
    }    
}


/* author jens, 13.08.2005 11:22:20, location: unknown, refactored in goettingen
 * get the file from the server already at open() and write the data to a new
 * filehandle. also create a "struct open_file" to store the filehandle. */
static int wdfs_open(const char *localpath, struct fuse_file_info *fi)
{
    LOG_ENEX(localpath, "");
    wdfs_pr("by PID %d\n", fuse_get_context()->pid);

    assert(localpath && fi);

    auto fullpath = wdfs->local2full(localpath);
    
    std::unique_ptr<fuse_file_t> file(new fuse_file_t());
    
    stats_t stats;
    stats[fullpath.get()] = webdav_head(session, fullpath);

    cache->update(stats);
    if (auto resource = cache->get(fullpath.get())) {
        wdfs_dbg("cache updated: opening file...\n");
        file->fd = cache->create_file(fullpath.get());
        
        struct stat st;
        if (::fstat(file->fd, &st)) {
            wdfs_err("1. fstat error\n");
            ::close(file->fd);
            file->fd = -1;
            cache->remove(fullpath.get());
        }
        else if(differ(st, resource->stat())) {
            wdfs_err("2. cached file differ!\n");
            ::close(file->fd);
            file->fd = -1;
            cache->remove(fullpath.get());
        }
        else {
            wdfs_err("2. cached file OK nothing to do!\n");
        }
    }
    else {
        wdfs_dbg("error\n");
        return -EFAULT;
    }
    
    if (file->fd != -1) {
        wdfs_pr("filecache hit no download needed\n");
    }
    else {
        file->fd = cache->create_file(fullpath.get());
        if (file->fd == -1) return -EIO;
        
        /* try to lock, if locking is enabled and file is not below svn_basedir. */
        if (wdfs_cfg.locking_mode != NO_LOCK) {
            if (lockfile(fullpath.get(), wdfs_cfg.locking_timeout)) {
                /* locking the file is not possible, because the file is locked by 
                * somebody else. read-only access is allowed. */
                if ((fi->flags & O_ACCMODE) == O_RDONLY) {
                    fprintf(stderr,
                        "## error: file %s is already locked. "
                        "allowing read-only (O_RDONLY) access!\n", fullpath.get());
                } else {
                    return -EACCES;
                }
            }
        }

        webdav_context_t ctx{session};  
        hook_helper_t hooker(session, &ctx);
        
        /* GET the data to the filehandle even if the file is opened O_WRONLY,
        * because the opening application could use pwrite() or use O_APPEND
        * and than the data needs to be present. */   
        std::string remotepath = canonicalize(fullpath.get(), ESCAPE);
        wdfs_dbg("getting file: [%s]\n", remotepath.c_str());        
        if (ne_get(session, remotepath.c_str(), file->fd)) {
            fprintf(stderr, "## GET error: %s\n", ne_get_error(session));
            return -ENOENT;        
        }

        wdfs_pr("   ++ File downloaded successfuly\n");
    }
    
    /* save our "struct open_file" to the fuse filehandle
        * this looks like a dirty hack too me, but it's the fuse way... */
    fi->fh = reinterpret_cast<uint64_t>(file.release());

    return 0;
}


/* reads data from the filehandle with pread() to fulfill read requests */
static int wdfs_read(
    const char *localpath, char *buf, size_t size,
    off_t offset, struct fuse_file_info *fi)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    assert(localpath && buf && fi);

    fuse_file_t *file = reinterpret_cast<fuse_file_t*>(fi->fh);

    std::cerr << "read from fd:" << file->fd << std::endl;
    
    int ret = pread(file->fd, buf, size, offset);
    if (ret < 0) wdfs_err("pread() error: %s\n", strerror(errno));

    return ret;
}


/* writes data to the filehandle with pwrite() to fulfill write requests */
static int wdfs_write(
    const char *localpath, const char *buf, size_t size,
    off_t offset, struct fuse_file_info *fi)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    assert(localpath && buf && fi);

    fuse_file_t *file = reinterpret_cast<fuse_file_t*>(fi->fh);

    int ret = pwrite(file->fd, buf, size, offset);
    if (ret < 0) wdfs_err("pwrite() error: %d\n", ret);

    /* set this flag, to indicate that data has been modified and needs to be
        * put to the webdav server. */
    file->modified = true;

    return ret;
}


/* author jens, 13.08.2005 11:28:40, location: unknown, refactored in goettingen
 * wdfs_release is called by fuse, when the last reference to the filehandle is
 * removed. this happens if the file is closed. after closing the file it's
 * time to put it to the server, but only if it was modified. */
static int wdfs_release(const char *localpath, struct fuse_file_info *fi)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    assert(localpath);

    fuse_file_t *file = reinterpret_cast<fuse_file_t*>(fi->fh);

    auto remotepath(wdfs->remotepath(localpath));
    if (!remotepath) return -ENOMEM;

    /* put the file only to the server, if it was modified. */
    if (file->modified == true) {
        
        {
            // Uploading file and updating cache to result values
            auto cached_file = cache->get(remotepath.get());
            
            webdav_context_t ctx{session};  
            hook_helper_t hooker(session, &ctx);
            
            if (ne_put(session, remotepath.get(), file->fd)) {
                fprintf(stderr, "## PUT error: %s\n", ne_get_error(session));
                return -EIO;
            }
            
            //cached_file->resource.update_from(ctx.resource); //TODO FIXME LAST
            cache->update(remotepath.get(), cached_file);
        }
        
        wdfs_dbg("%s(): PUT the file to the server\n", __func__); 

        /* attributes for this file are no longer up to date.
            * so remove it from cache. */
    //         file_cache.remove(remotepath);//TODO FIXME

        /* unlock if locking is enabled and mode is ADVANCED_LOCK, because data
            * has been read and writen and so now it's time to remove the lock. */
        if (wdfs_cfg.locking_mode == ADVANCED_LOCK) {
            if (unlockfile(remotepath.get())) {
                return -EACCES;
            }
        }
    }

    /* if locking is enabled and mode is SIMPLE_LOCK, simple unlock on close() */
    if (wdfs_cfg.locking_mode == SIMPLE_LOCK) {
        if (unlockfile(remotepath.get())) {
            return -EACCES;
        }
    }

    /* close filehandle and free memory */
    // close(file->fd);//TODO FIXME refference count
    FREE(file);
    fi->fh = 0;

    return 0;
}


/* author jens, 13.08.2005 11:32:20, location: unknown, refactored in goettingen
 * wdfs_truncate is called by fuse, when a file is opened with the O_TRUNC flag
 * or truncate() is called. according to 'man truncate' if the file previously 
 * was larger than this size, the extra data is lost. if the file previously 
 * was shorter, it is extended, and the extended part is filled with zero bytes.
 */
static int wdfs_truncate(const char *localpath, off_t size)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    wdfs_pr("   ++ at offset %li\n", (long int)size);

    assert(localpath);

    /* the truncate procedure:
        *  1. get the complete file and write into fh_in
        *  2. read size bytes from fh_in to buffer
        *  3. write size bytes from buffer to fh_out
        *  4. read from fh_out and put file to the server
        */

    auto remotepath = wdfs->remotepath(localpath);
    if (!remotepath) return -ENOMEM;

    if (auto cached_file = cache->get(remotepath.get())) {
        //TODO FIXME LAST update on server?
        if (::truncate(cache->cache_filename(remotepath.get()).c_str(), size)) {
            fprintf(stderr, "## ftruncate() error: %d\n", 123123);            
            ::remove(cache->cache_filename(remotepath.get()).c_str());
        }

        cached_file->stat().st_size = size;
        /* calculate number of 512 byte blocks */
        cached_file->stat().st_blocks = (cached_file->stat().st_size + 511) / 512;
        cache->update(remotepath.get(), cached_file);
        return 0;
    }

    int ret;
    int fh_in  = get_filehandle();
    int fh_out = get_filehandle();
    if (fh_in == -1 || fh_out == -1)
        return -EIO;

    char buffer[size];
    memset(buffer, 0, size);

    /* if truncate(0) is called, there is no need to get the data, because it
    * would not be used. */
    if (size != 0) {
        if (ne_get(session, remotepath.get(), fh_in)) {
            fprintf(stderr, "## GET error: %s\n", ne_get_error(session));
            close(fh_in);
            close(fh_out);
            return -ENOENT;
        }

        ret = pread(fh_in, buffer, size, 0);
        if (ret < 0) {
            fprintf(stderr, "## pread() error: %d\n", ret);
            close(fh_in);
            close(fh_out);
            return -EIO;
        }
    }

    ret = pwrite(fh_out, buffer, size, 0);
    if (ret < 0) {
        fprintf(stderr, "## pwrite() error: %d\n", ret);
        close(fh_in);
        close(fh_out);
        return -EIO;
    }

    if (ne_put(session, remotepath.get(), fh_out)) {
        fprintf(stderr, "## PUT error: %s\n", ne_get_error(session));
        close(fh_in);
        close(fh_out);
        return -EIO;
    }

    close(fh_in);
    close(fh_out);

    return 0;
}


/* author jens, 12.03.2006 19:44:23, location: goettingen in the winter
 * ftruncate is called on already opened files, truncate on not yet opened
 * files. ftruncate is supported since wdfs 1.2.0 and needs at least 
 * fuse 2.5.0 and linux kernel 2.6.15. */
static int wdfs_ftruncate(
    const char *localpath, off_t size, struct fuse_file_info *fi)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    assert(localpath && fi);

    auto remotepath = wdfs->remotepath(localpath);
    if (!remotepath) return -ENOMEM;

    fuse_file_t* file = reinterpret_cast<fuse_file_t*>(fi->fh);

    if (int ret = ftruncate(file->fd, size)) {
        fprintf(stderr, "## ftruncate() error: %d\n", ret);
        return -EIO;
    }

    /* set this flag, to indicate that data has been modified and needs to be
        * put to the webdav server. */
    file->modified = true;

    /* update the cache item of the ftruncate()d file */
    cache_t::item_p cached_file = cache->get(remotepath.get());
    assert(cached_file);
    cached_file->stat().st_size = size;
    /* calculate number of 512 byte blocks */
    cached_file->stat().st_blocks	= (cached_file->stat().st_size + 511) / 512;
    cache->update(remotepath.get(), cached_file);

    return 0;
}


/* author jens, 28.07.2005 18:15:12, location: noedlers garden in trubenhausen
 * this method creates a empty file using the webdav method put. */
static int wdfs_mknod(const char *localpath, mode_t mode, dev_t rdev)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    assert(localpath);

    auto remotepath = wdfs->remotepath(localpath);
    if (!remotepath) return -ENOMEM;

    int fh = get_filehandle();
    if (fh == -1) {
        return -EIO;
    }

    if (ne_put(session, remotepath.get(), fh)) {
        fprintf(stderr, "## PUT error: %s\n", ne_get_error(session));
        close(fh);
        return -EIO;
    }

    close(fh);
    return 0;
}


/* author jens, 03.08.2005 12:03:40, location: goettingen
 * this method creates a directory / collection using the webdav method mkcol. */
static int wdfs_mkdir(const char *localpath, mode_t mode)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    assert(localpath);

    auto remotepath = wdfs->remotepath(localpath);
    if (!remotepath) return -ENOMEM;

    if (ne_mkcol(session, remotepath.get())) {
        fprintf(stderr, "MKCOL error: %s\n", ne_get_error(session));
        return -ENOENT;
    }

    return 0;
}


/* author jens, 30.07.2005 13:08:11, location: heli at heinemanns
 * this methods removes a file or directory using the webdav method delete. */
static int wdfs_unlink(const char *localpath)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath); 
    assert(localpath);

    auto remotepath = wdfs->remotepath(localpath);
    if (!remotepath) return -ENOMEM;

    /* unlock the file, to be able to unlink it */
    if (wdfs_cfg.locking_mode != NO_LOCK) {
        if (unlockfile(remotepath.get())) {
            return -EACCES;
        }
    }

    int ret = ne_delete(session, remotepath.get());
    if (ret == NE_REDIRECT && wdfs_cfg.redirect == true) {
        if (handle_redirect(remotepath))
            return -ENOENT;
        ret = ne_delete(session, remotepath.get());
    }

    /* file successfully deleted! remove it also from the cache. */
    if (ret == 0) {
        cache->remove(remotepath.get());
    /* return more specific error message in case of permission problems */
    } else if (!strcmp(ne_get_error(session), "403 Forbidden")) {
        ret = -EPERM;
    } else {
        fprintf(stderr, "## DELETE error: %s\n", ne_get_error(session));
        ret = -EIO;
    }

    return ret;
}


/* author jens, 31.07.2005 19:13:39, location: heli at heinemanns
 * this methods renames a file. it uses the webdav method move to do that. */
static int wdfs_rename(const char *localpath_src, const char *localpath_dest)
{
    wdfs_dbg("%s(%s -> %s)\n", __func__, localpath_src, localpath_dest); 
    assert(localpath_src && localpath_dest);

    auto remotepath_src  = wdfs->remotepath(localpath_src);
    auto remotepath_dest = wdfs->remotepath(localpath_dest);
    if (!remotepath_src || !remotepath_dest) return -ENOMEM;

    /* unlock the source file, before renaming */
    if (wdfs_cfg.locking_mode != NO_LOCK) {
        if (unlockfile(remotepath_src.get())) {
            return -EACCES;
        }
    }

    int ret = ne_move(session, 1, remotepath_src.get(), remotepath_dest.get());
    if (ret == NE_REDIRECT && wdfs_cfg.redirect == true) {
        if (handle_redirect(remotepath_src))
            return -ENOENT;
        ret = ne_move(session, 1, remotepath_src.get(), remotepath_dest.get());
    }

    if (ret == 0) {
        /* rename was successful and the source file no longer exists.
            * hence, remove it from the cache. */
        cache->remove(remotepath_src.get());
    } else {
        fprintf(stderr, "## MOVE error: %s\n", ne_get_error(session));
        ret = -EIO;
    }

    return ret;
}


int wdfs_chmod(const char *localpath, mode_t mode)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath);
    assert(localpath);
    
    auto remotepath = wdfs->remotepath(localpath);
    const std::string mode_str = std::to_string(mode);
    const std::string exec_str = (mode & S_IXUSR || mode & S_IXGRP || mode &S_IXOTH) ? "T" : "F";
    
    const ne_proppatch_operation ops[] = {
        {
            &prop_names[EXECUTE],
            ne_propset,
            exec_str.c_str()
        },
        {
            &prop_names[PERMISSIONS],
            ne_propset,
            mode_str.c_str()
        },        
        {NULL}
    };
    
    cache_t::item_p cached_file = cache->get(remotepath.get());
    assert(cached_file);
    
    webdav_context_t ctx{session, *cached_file};  
    hook_helper_t hooker(session, &ctx);
    
    if (ne_proppatch(session, remotepath.get(), ops)) {
        fprintf(stderr, "PROPPATCH error: %s\n", ne_get_error(session));
        return -ENOENT;
    }
    
    //cached_file->resource = ctx.resource;//TODO FIXME
    cache->update(remotepath.get(), cached_file);
    
    return 0;
}


/* this is just a dummy implementation to avoid errors, when setting attributes.
 * a usefull implementation is not possible, because the webdav standard only 
 * defines a "getlastmodified" property that is read-only and just updated when
 * the file's content or properties change. */
static int wdfs_setattr(const char *localpath, struct utimbuf *buf)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath);
    assert(localpath);

    return 0;
}


/* this is a dummy implementation that pretends to have 1000 GB free space :D */
static int wdfs_statfs(const char *localpath, struct statvfs *buf)
{
    wdfs_dbg("%s(%s)\n", __func__, localpath);
    assert(localpath);

    /* taken from sshfs v1.7, thanks miklos! */
    buf->f_bsize = 512;
    buf->f_blocks = buf->f_bfree = buf->f_bavail =
        1000ULL * 1024 * 1024 * 1024 / buf->f_bsize;
    buf->f_files = buf->f_ffree = 1000000000;

    return 0;
}


/* just say hello when fuse takes over control. */
#if FUSE_VERSION >= 26
    static void* wdfs_(struct fuse_conn_info *conn)
#else
    static void* wdfs_init()
#endif
{
    wdfs_dbg("%s()\n", __func__);

    cache.reset(new cache_t(wdfs_cfg.cachedir, wdfs_cfg.webdav_remotebasedir));
    
    wdfs.reset(new wdfs_controller_t(wdfs_cfg.webdav_server, wdfs_cfg.webdav_remotebasedir));

    std::cerr << "webdav server:" << wdfs_cfg.webdav_server << std::endl;
    std::cerr << "webdav_remotebasedir:" << wdfs_cfg.webdav_remotebasedir << std::endl;
    
    try {
        std::ifstream stream((wdfs_cfg.cachedir + "cache").c_str());
        boost::archive::text_iarchive oa(stream);
        //oa >> *cache;
    }
    catch(const std::exception& e) {
        wdfs_dbg("%s(): can't load cache\n", __func__);
    }

    wdfs_dbg("%s() restored cache size: %d\n", __func__, cache->size());

    return NULL;
}


/* author jens, 04.08.2005 17:41:12, location: goettingen
 * this method is called, when the filesystems is unmounted. time to clean up! */
static void wdfs_destroy(void*)
{
    wdfs_dbg("%s()\n", __func__);

    try {
        std::ofstream stream((wdfs_cfg.cachedir + "cache").c_str());
        boost::archive::text_oarchive oa(stream);
        //oa << *cache;
        cache.reset();
    }
    catch(const std::exception& e) {
        wdfs_dbg("%s(): can't save cache\n", __func__);
    }
    
    /* free globaly used memory */
    unlock_all_files();
    ne_session_destroy(session);
    FREE(remotepath_basedir);
}

static struct fuse_operations wdfs_operations  = [] () {
    fuse_operations wo;
    
    wo.getattr    = wdfs_getattr;
    wo.readdir    = wdfs_readdir;
    wo.open       = wdfs_open;
    wo.read       = wdfs_read;
    wo.write      = wdfs_write;
    wo.release    = wdfs_release;
    wo.truncate   = wdfs_truncate;
    wo.ftruncate  = wdfs_ftruncate;
    wo.mknod      = wdfs_mknod;
    wo.mkdir      = wdfs_mkdir;
    /* webdav treats file and directory deletions equal, both use wdfs_unlink */
    wo.unlink     = wdfs_unlink;
    wo.rmdir      = wdfs_unlink;
    wo.rename     = wdfs_rename;
    wo.chmod      = wdfs_chmod;
    /* utime should be better named setattr
     * see: http://sourceforge.net/mailarchive/message.php?msg_id=11344401 */
    wo.utime      = wdfs_setattr;
    wo.statfs     = wdfs_statfs;
    wo.init       = wdfs_init;
    wo.destroy    = wdfs_destroy;
    
    return wo;
} ();


/* author jens, 26.08.2005 12:26:59, location: lystrup near aarhus 
 * this method prints help and usage information, call fuse to print its
 * help information. */
static void print_help()
{
    fprintf(stderr,
"usage: %s http[s]://server[:port][/directory/] mountpoint [options]\n\n"
"wdfs options:\n"
"    -v, --version          show version of wdfs\n"
"    -vv, --all-versions    show versions of wdfs, neon and fuse\n"
"    -h, --help             show this help page\n"
"    -D, -o wdfs_debug      enable wdfs debug output\n"
"    -o accept_sslcert      accept ssl certificate, don't prompt the user\n"
"    -o username=arg        replace arg with username of the webdav resource\n"
"    -o password=arg        replace arg with password of the webdav resource\n"
"    -o cachedir=arg        cachedir\n"
"                           username/password can also be entered interactively\n"
"    -o no_redirect         disable http redirect support\n"
"    -o svn_mode            enable subversion mode to access all revisions\n"
"    -o locking             same as -o locking=simple\n"
"    -o locking=mode        select a file locking mode:\n"
"                           0 or none:     disable file locking (default)\n"
"                           1 or simple:   from open until close\n"
"                           2 or advanced: from open until write + close\n"
"                           3 or eternity: from open until umount or timeout\n"
"    -o locking_timeout=sec timeout for a lock in seconds, -1 means infinite\n"
"                           default is 300 seconds (5 minutes)\n\n"
"wdfs backwards compatibility options: (used until wdfs 1.3.1)\n"
"    -a uri                 address of the webdav resource to mount\n"
"    -ac                    same as -o accept_sslcert\n"
"    -u arg                 same as -o username=arg\n"
"    -p arg                 same as -o password=arg\n"
"    -S                     same as -o svn_mode\n"
"    -l                     same as -o locking=simple\n"
"    -m locking_mode        same as -o locking=mode (only numerical modes)\n"
"    -t seconds             same as -o locking_timeout=sec\n\n",
    wdfs_cfg.program_name);
}


/* just a simple wrapper for fuse_main(), because the interface changed...  */
static int call_fuse_main(struct fuse_args *args)
{
#if FUSE_VERSION >= 26
    return fuse_main(args->argc, args->argv, &wdfs_operations, NULL);
#else
    return fuse_main(args->argc, args->argv, &wdfs_operations);
#endif
}

struct test_t {
    int a;
    double b;
};


/* the main method does the option parsing using fuse_opt_parse(), establishes
 * the connection to the webdav resource and finally calls main_fuse(). */
int main(int argc, char *argv[])
{
    int status_program_exec = 1;

    struct fuse_args options = FUSE_ARGS_INIT(argc, argv);
    wdfs_cfg.program_name = argv[0];

    if (fuse_opt_parse(&options, &wdfs_cfg, wdfs_opts, wdfs_opt_proc) == -1)
        exit(1);

    if (wdfs_cfg.cachedir1) {
      wdfs_cfg.cachedir = wdfs_cfg.cachedir1;
      wdfs_cfg.cachedir += "/";
      mkdir_p(wdfs_cfg.cachedir);
      char *real_path = realpath(wdfs_cfg.cachedir.c_str(), NULL);
      wdfs_cfg.cachedir = real_path;
      wdfs_cfg.cachedir += "/";
      // use real_path
      free(real_path);
    }

    if (wdfs_cfg.webdav_resource.empty()) {
        fprintf(stderr, "%s: missing webdav uri\n", wdfs_cfg.program_name);
        exit(1);
    }
    
    std::shared_ptr<ne_uri> uri(new ne_uri, ne_uri_free);
    if (ne_uri_parse(wdfs_cfg.webdav_resource.c_str(), uri.get())) {
        fprintf(stderr,
            "## ne_uri_parse() error: invalid URI '%s'.\n", wdfs_cfg.webdav_resource.c_str());
        exit(1);
    }
    
    if(char const* home = getenv("HOME")) {
        auto hasher = std::hash<std::string>();
        if (wdfs_cfg.cachedir.empty()) wdfs_cfg.cachedir = std::string(home) + "/" + ".wdfs/" + std::to_string(hasher(uri->host)) + "/";
        if (mkdir_p(wdfs_cfg.cachedir)) {
            fprintf(stderr, "%s: can't create cache folder %s\n", wdfs_cfg.program_name, wdfs_cfg.cachedir.c_str());
            exit(1);
        }
        
        auto up = parse_netrc(std::string(home) + "/.netrc", uri->host);
        if (wdfs_cfg.username.empty()) wdfs_cfg.username = up.first;
        if (wdfs_cfg.password.empty()) wdfs_cfg.password = up.second;
    }
    else {
        fprintf(stderr, "%s: can't obtain HOME variable\n", wdfs_cfg.program_name);
        exit(1);
    }

    if (wdfs_cfg.locking_timeout < -1 || wdfs_cfg.locking_timeout == 0) {
        fprintf(stderr, "## error: timeout must be bigger than 0 or -1!\n");
        exit(1);
    }

    if (wdfs_cfg.debug == true) {
        fprintf(stderr, 
            "wdfs settings:\n  program_name: %s\n  webdav_resource: %s\n"
            "  accept_certificate: %s\n  username: %s\n  password: %s\n"
            "  redirect: %s\n  locking_mode: %i\n"
            "  locking_timeout: %i\n"
            "  cache folder: %s\n  mountpoint: %s\n",
            wdfs_cfg.program_name,
            !wdfs_cfg.webdav_resource.empty() ? wdfs_cfg.webdav_resource.c_str() : "NULL",
            wdfs_cfg.accept_certificate == true ? "true" : "false",
            !wdfs_cfg.username.empty() ? wdfs_cfg.username.c_str() : "NULL",
            !wdfs_cfg.password.empty() ? "****" : "NULL",
            wdfs_cfg.redirect == true ? "true" : "false",
            wdfs_cfg.locking_mode, wdfs_cfg.locking_timeout,
            wdfs_cfg.cachedir.c_str(), wdfs_cfg.mountpoint.c_str());
    }

    /* set a nice name for /proc/mounts */
    char *fsname = ne_concat("-ofsname=wdfs (", wdfs_cfg.webdav_resource.c_str(), ")", NULL);
    fuse_opt_add_arg(&options, fsname);
    FREE(fsname);

    /* ensure that wdfs is called in single thread mode */
    fuse_opt_add_arg(&options, "-s");

    /* wdfs must not use the fuse caching of names (entries) and attributes! */
    fuse_opt_add_arg(&options, "-oentry_timeout=0");
    fuse_opt_add_arg(&options, "-oattr_timeout=0");

    /* reset parameters to avoid storing sensitive data in the process table */
    int arg_number = 1;
    for (; arg_number < argc; arg_number++)
        memset(argv[arg_number], 0, strlen(argv[arg_number]));

    /* set up webdav connection, exit on error */
    if (setup_webdav_session(*uri, wdfs_cfg.username, wdfs_cfg.password)) {
        status_program_exec = 1;
        goto cleanup;
    }
	 
    if (remotepath_basedir) {
        wdfs_cfg.webdav_remotebasedir = remotepath_basedir;
    }
//     wdfs_cfg.webdav_server = ne_uri_unparse(uri.get());
    wdfs_cfg.webdav_server = std::string(uri->scheme) + "://" + uri->host;
    //wdfs_cfg.webdav_server = wdfs_cfg.webdav_server.substr(0, wdfs_cfg.webdav_server.find(wdfs_cfg.webdav_remotebasedir));
    std::cerr << "SRV:" << wdfs_cfg.webdav_server << std::endl;

    /* finally call fuse */
    status_program_exec = call_fuse_main(&options);

    /* clean up and quit wdfs */
cleanup:
    fuse_opt_free_args(&options);

    return status_program_exec;
}
