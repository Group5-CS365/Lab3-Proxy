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
        puts("not a valid HTTP request line");
}

struct http_header_field
parse_http_header_field(char *buf, size_t len)
{
    static char const * const ws = " \t\r\v\f";
    static size_t const wslen = 5;
    static char const * const nws = "\r\v\f";
    static size_t const nwslen = 3;

    char *p = buf, *end = buf + len;
    struct http_header_field head = { .end = end };

    if (p == end) {
        fputs("warning: invalid header field (empty)\n", stderr);
        return head;
    }

    // Field name
    head.field_name.p = p;
    p = memchr(p, ':', len);
    head.field_name.len = p - head.field_name.p;

    ++p; // :

    // Eat white space.
    while (p != end && memchr(ws, *p, wslen) != NULL)
        ++p;
    len -= p - head.field_name.p;

    if (p == end) {
        fputs("warning: invalid header field (after field name)\n", stderr);
        fprintf(stderr, "FIELD NAME: %.*s\n", (int)head.field_name.len, head.field_name.p);
        return head;
    }

    // Field value
    head.field_value.p = p;
    while (p != end && memchr(nws, *p, nwslen) == NULL)
        ++p;
    head.field_value.len = p - head.field_value.p;

    // Consume whitespace.
    while (p != end && memchr(ws, *p, wslen) != NULL)
        ++p;
    len -= p - head.field_value.p;

    if (p == end) {
        fputs("warning: invalid header field (after field value)\n", stderr);
        fprintf(stderr, "FIELD NAME: %.*s\n", (int)head.field_name.len, head.field_name.p);
        fprintf(stderr, "FIELD VALUE: %.*s\n", (int)head.field_value.len, head.field_value.p);
        return head;
    }

    if (*p != '\n') {
        fputs("warning: invalid header field (missing LF)\n", stderr);
        fprintf(stderr, "FIELD NAME: %.*s\n", (int)head.field_name.len, head.field_name.p);
        fprintf(stderr, "FIELD VALUE: %.*s\n", (int)head.field_value.len, head.field_value.p);
        fprintf(stderr, "got instead of LF: %.*s\n", (int)len, p);
        return head;
    }

    head.valid = true;
    head.end = p + 1;

    return head;
}

void
debug_http_header_field(struct http_header_field reqhead)
{
    if (reqhead.valid)
        printf("valid HTTP header field:\n"
               "\tFIELD NAME: %.*s\n"
               "\tFIELD VALUE: %.*s\n",
               (int)reqhead.field_name.len, reqhead.field_name.p,
               (int)reqhead.field_value.len, reqhead.field_value.p);
    else
        puts("not a valid HTTP header field");
}
