#ifndef WDFSMAIN_H_
#define WDFSMAIN_H_

#include <memory>
#include <string>


struct wdfs_conf {
    /* the name of the wdfs executable */
    char *program_name;
    /* if set to "true" wdfs specific debug output is generated */
    bool debug;
    /* if set to "true" every certificate is accepted without asking the user */
    bool accept_certificate;
    /* username of the webdav resource */
    std::string username;
    /* password of the webdav resource */
    std::string password;
    /* if set to "true" enables http redirect support */
    bool redirect;
    /* locking mode of files */
    int locking_mode;
    /* timeout for a lock in seconds */
    int locking_timeout;
    /* address of the webdav resource we are connecting to */
    std::string webdav_resource;

    std::string webdav_remotebasedir;
    std::string webdav_server;
    /* path to mountpoint */
    std::string mountpoint;
    /* path to folder where cache(attrs AND files) located */
    std::string cachedir;
    char* cachedir1;
};

extern struct wdfs_conf wdfs_cfg;

/* look at wdfs-main.c for comments on these extern variables */
extern const char *project_name;
extern char *remotepath_basedir;

#endif /*WDFSMAIN_H_*/
