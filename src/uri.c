/*
 * uri.c
 * Implementation of the URI parser and utilities.
 */

/*
  MIT License

  Copyright (c) 2018 Alex Elkins, Tyler Gearing, Ryan Moeller

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include "uri.h"

#include <stdio.h>
#include <string.h>


/*
 * URI
 */

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

    if (p == NULL) {
        fputs("warning: invalid uri (not an absolute-uri)\n", stderr);
        return site;
    }

    // Eat ://
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
        site.authority.port.p = (char *)"80"; // XXX: not ideal...
        site.authority.port.len = 2;
    }

    if (*p == '/') {
        // Path
        site.path_query_fragment.p = p;
        site.path_query_fragment.len = end - p;
		}
    else {
        site.path_query_fragment.p = (char *)"/"; // XXX: not ideal...
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
