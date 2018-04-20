#include "uri.h"

#include <stdio.h>
#include <string.h>

struct uri
parse_uri(char *buf, size_t len)
{
    static char const * const delims = "/:";
    static size_t const delimslen = 2;

    char *p = buf, *end = buf + len;
    struct uri site = { .end = end };

    if (p == end) {
        fputs("warning: invalid uri (empty)\n", stderr);
        return site;
    }

    // Scheme
    site.scheme.p = p;
    p = memchr(p, ':', len);
    site.scheme.len = p - site.scheme.p;

    ++p; // :

    // Eat Forward Slashes
    while (p != end && memchr(delims, *p, delimslen) != NULL)
        ++p;
    len -= p - site.scheme.p;

    if (p == end) {
        fputs("warning: invalid uri (after scheme)\n", stderr);
        fprintf(stderr, "SCHEME: %.*s\n", (int)site.scheme.len, site.scheme.p);
        return site;
    }

    // Host
    site.authority.host.p = p;
    while (p != end && memchr(delims, *p, delimslen) == NULL)
        ++p;
    site.authority.host.len = p - site.authority.host.p;

    if (p == end) {
        fputs("warning: invalid uri (after host)\n", stderr);
        fprintf(stderr, "SCHEME: %.*s\n", (int)site.scheme.len, site.scheme.p);
        fprintf(stderr, "HOST: %.*s\n", (int)site.authority.host.len, site.authority.host.p);
        return site;
    }

    if (*p == ':') {
        // :
        if (++p == end || *p == '/') {
            fputs("warning: invalid uri (empty port)\n", stderr);
            fprintf(stderr, "SCHEME: %.*s\n", (int)site.scheme.len, site.scheme.p);
            fprintf(stderr, "HOST: %.*s\n", (int)site.authority.host.len, site.authority.host.p);
            return site;
        }

        // Port
        site.authority.port.p = p;
        while (p != end && memchr(delims, *p, delimslen) == NULL)
            ++p;
        site.authority.port.len = p - site.authority.port.p;

        if (p == end) {
            fputs("warning: invalid uri (after port)\n", stderr);
            fprintf(stderr, "SCHEME: %.*s\n", (int)site.scheme.len, site.scheme.p);
            fprintf(stderr, "HOST: %.*s\n", (int)site.authority.host.len, site.authority.host.p);
            fprintf(stderr, "PORT: %.*s\n", (int)site.authority.port.len, site.authority.port.p);
            return site;
        }
    }
    else {
        site.authority.port.p = (char *)"80"; // FIXME: not ideal...
        site.authority.port.len = 2;
    }

    if (*p == '/') {
        // Path
        site.path_query_fragment.p = p;
        site.path_query_fragment.len = end - p;
		}
    else {
        site.path_query_fragment.p = (char *)"/"; // FIXME: not ideal...
        site.path_query_fragment.len = 1;
    }

    site.valid = true;

    return site;
}

void
debug_uri(struct uri uri)
{
    if (uri.valid)
        printf("valid URI:\n"
               "\tSCHEME: %.*s\n"
               "\tHOST: %.*s\n"
               "\tPORT: %.*s\n"
               "\tPATH QUERY FRAGMENT: %.*s\n",
               (int)uri.scheme.len, uri.scheme.p,
               (int)uri.authority.host.len, uri.authority.host.p,
               (int)uri.authority.port.len, uri.authority.port.p,
               (int)uri.path_query_fragment.len, uri.path_query_fragment.p);
    else
        puts("not a valid URI");
}
