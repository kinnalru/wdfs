#ifndef WDFSMAIN_H_
#define WDFSMAIN_H_

#ifdef HAVE_CONFIG_H
	#include <config.h>
#endif

#define FUSE_USE_VERSION 25

#include <memory>
#include <string>

#include <fuse.h>
#include <ne_basic.h>

/* build the neon version, which is not directly exported by the neon library */
#if defined(NE_FEATURE_TS_SSL)	/* true for neon 0.26+  */
	#define NEON_VERSION 26
#elif defined(NE_FEATURE_SSL)	/* true for neon 0.25+  */
	#define NEON_VERSION 25
#else							/* neon 0.24 is the minimal requirement */
	#define NEON_VERSION 24
#endif
/* 	it's also possible to replace the above with the following: 
	(file configure.ac, after the PKG_CHECK_MODULES call)

	case `pkg-config --modversion neon` in
		0.24*) AC_DEFINE(NEON_VERSION, 24,
				[The minor version number of the neon library]) ;;
		0.25*) AC_DEFINE(NEON_VERSION, 25) ;;
		*)     AC_DEFINE(NEON_VERSION, 26) ;;
	esac
*/

struct wdfs_conf {
    /* the name of the wdfs executable */
    char *program_name;
    /* if set to "true" wdfs specific debug output is generated */
    bool debug;
    /* if set to "true" every certificate is accepted without asking the user */
    bool accept_certificate;
    /* username of the webdav resource */
    char *username;
    /* password of the webdav resource */
    char *password;
    /* if set to "true" enables http redirect support */
    bool redirect;
    /* locking mode of files */
    int locking_mode;
    /* timeout for a lock in seconds */
    int locking_timeout;
    /* address of the webdav resource we are connecting to */
    char *webdav_resource;
    /* path to folder where cache(attrs AND files) located */
    std::string cache_folder;
};

extern struct wdfs_conf wdfs;

/* look at wdfs-main.c for comments on these extern variables */
extern const char *project_name;
extern char *remotepath_basedir;

/* used by wdfs_readdir() and by svn.h/svn.c to add files to requested 
 * directories using fuse's filler() method. */
struct dir_item {
	void *buf;
	fuse_fill_dir_t filler;
	std::unique_ptr<char> remotepath;
};

#endif /*WDFSMAIN_H_*/
