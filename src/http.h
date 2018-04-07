#ifndef _http_h_
#define _http_h_

#include <stdbool.h>
#include <stdlib.h>

// minimum recommended supported request line length
// https://tools.ietf.org/html/rfc7230#section-3.1.1
#define REQUEST_LINE_MIN_BUFLEN 8000

struct http_request_line {
    struct {
        char *p;
        size_t len;
    } method, request_target, http_version;
    char *end;
    bool valid;
};

struct http_request_line parse_http_request_line(char *buf, size_t len);

void debug_http_request_line(struct http_request_line);

#endif // _http_h_
