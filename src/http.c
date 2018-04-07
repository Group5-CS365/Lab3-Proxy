#include "http.h"

#include <stdio.h>
#include <string.h>

struct http_request_line
parse_http_request_line(char *buf, size_t len)
{
    static char const * const crlf = "\r\n";
    static size_t const crlflen = 2;
    static char const * const ws = " \t\r\v\f";
    static size_t const wslen = 5;

    char *p = buf, *end = buf + len;
    struct http_request_line line = { .end = end };

    // Consume leading CRLFs.
    // https://tools.ietf.org/html/rfc7230#section-3.5
    while (p != end && memchr(crlf, *p, crlflen) != NULL)
        ++p;
    len -= p - buf;

    if (p == end) {
        fputs("warning: invalid request line (empty)\n", stderr);
        return line;
    }

    // Method
    line.method.p = p;
    while (p != end && memchr(ws, *p, wslen) == NULL)
        ++p;
    line.method.len = p - line.method.p;

    // Consume whitespace.
    while (p != end && memchr(ws, *p, wslen) != NULL)
        ++p;
    len -= p - line.method.p;

    if (p == end) {
        fputs("warning: invalid request line (after method)\n", stderr);
        fprintf(stderr, "METHOD: %.*s\n", (int)line.method.len, line.method.p);
        return line;
    }

    // Request target
    line.request_target.p = p;
    while (p != end && memchr(ws, *p, wslen) == NULL)
        ++p;
    line.request_target.len = p - line.request_target.p;

    // Consume whitespace.
    while (p != end && memchr(ws, *p, wslen) != NULL)
        ++p;
    len -= p - line.request_target.p;

    if (p == end) {
        fputs("warning: invalid request line (after request target)\n", stderr);
        fprintf(stderr, "METHOD: %.*s\n", (int)line.method.len, line.method.p);
        fprintf(stderr, "REQUEST TARGET: %.*s\n", (int)line.request_target.len, line.request_target.p);
        return line;
    }

    // HTTP version
    line.http_version.p = p;
    while (p != end && memchr(ws, *p, wslen) == NULL)
        ++p;
    line.http_version.len = p - line.http_version.p;

    // Consume whitespace.
    while (p != end && memchr(ws, *p, wslen) != NULL)
        ++p;
    len -= p - line.http_version.p;

    if (p == end) {
        fputs("warning: invalid request line (after http version)\n", stderr);
        fprintf(stderr, "METHOD: %.*s\n", (int)line.method.len, line.method.p);
        fprintf(stderr, "REQUEST TARGET: %.*s\n", (int)line.request_target.len, line.request_target.p);
        fprintf(stderr, "HTTP VERSION: %.*s\n", (int)line.http_version.len, line.http_version.p);
        return line;
    }

    // LF (already consumed CR)
    if (*p != '\n') {
        fputs("warning: invalid request line (missing LF)\n", stderr);
        fprintf(stderr, "METHOD: %.*s\n", (int)line.method.len, line.method.p);
        fprintf(stderr, "REQUEST TARGET: %.*s\n", (int)line.request_target.len, line.request_target.p);
        fprintf(stderr, "HTTP VERSION: %.*s\n", (int)line.http_version.len, line.http_version.p);
        fprintf(stderr, "got instead of LF: %.*s\n", (int)len, p);
        return line;
    }

    line.valid = true;
    line.end = p + 1;

    return line;
}

void
debug_http_request_line(struct http_request_line reqline)
{
    if (reqline.valid)
        printf("valid HTTP request line:\n"
               "\tMETHOD: %.*s\n"
               "\tREQUEST TARGET: %.*s\n"
               "\tHTTP VERSION: %.*s\n",
               (int)reqline.method.len, reqline.method.p,
               (int)reqline.request_target.len, reqline.request_target.p,
               (int)reqline.http_version.len, reqline.http_version.p);
    else
        printf("not a valid HTTP request line\n");
}
