
#include <assert.h>
#include <string.h>
#include <stdarg.h>

#include <memory>

#include <glib.h>
#include <ne_uri.h>

#include "common.h"


char* remove_ending_slashes(const char* path)
{
    char *new_path = strdup(path);
    int pos = strlen(path) - 1;

    while(pos >= 0  &&  new_path[pos] == '/')
        new_path[pos--] = '\0';

    return new_path;
}

char* unify_path(const char* path_in, int mode)
{
    assert(path_in);
    char *path_out = NULL;
    

    std::unique_ptr<char> path_tmp(strdup(path_in));
    if (!path_tmp.get())
        return NULL;

    /* some servers send the complete URI not only the path.
     * hence remove the server part and use the path only.
     * example1:  before: "https://server.com/path/to/hell/"
     *            after:  "/path/to/hell/"
     * example2:  before: "http://server.com"
     *            after:  ""                    */
    if (g_str_has_prefix(path_tmp.get(), "http")) {
        std::unique_ptr<char> tmp0(strdup(path_in));
        /* jump to the 1st '/' of http[s]:// */
        char *tmp1 = strchr(tmp0.get(), '/');
        /* jump behind '//' and get the next '/'. voila: the path! */
        char *tmp2 = strchr(tmp1 + 2, '/');

        if (tmp2 == NULL)
            path_tmp.reset(strdup(""));
        else
            path_tmp.reset(strdup(tmp2));
    }

    if (mode & LEAVESLASH) {
        mode &= ~LEAVESLASH;
    } else {
        path_tmp.reset(remove_ending_slashes(path_tmp.get()));
    }
    
    if (!path_tmp.get())
        return NULL;

    switch (mode) {
        case ESCAPE:
            path_out = ne_path_escape(path_tmp.get());
            break;
        case UNESCAPE:
            path_out = ne_path_unescape(path_tmp.get());
            break;
        default:
            fprintf(stderr, "## fatal error: unknown mode in %s()\n", __func__);
            exit(1);
    }

    if (path_out == NULL)
        return NULL;

    return path_out;
}

void free_chars(char **arg, ...)
{
    va_list ap;
    va_start(ap, arg);
    while (arg) {
        if (*arg != NULL)
            free(*arg);
        *arg = NULL;
        /* get the next parameter */
        arg = va_arg(ap, char **);
    }
    va_end(ap);
}



