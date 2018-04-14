#ifndef _http_h_
#define _http_h_

#include <stdbool.h>
#include <stdlib.h>

#include "iostring.h"

// minimum recommended supported request line length
// https://tools.ietf.org/html/rfc7230#section-3.1.1
#define REQUEST_LINE_MIN_BUFLEN 8000

struct http_request_line {
    struct iostring method, request_target, http_version;
    char *end;
    bool valid;
};

struct http_request_line parse_http_request_line(char *buf, size_t len);

void debug_http_request_line(struct http_request_line);


struct http_header_field {
    struct iostring field_name, field_value;
    char *end;
    bool valid;
};

struct http_header_field parse_http_header_field(char *buf, size_t len);

void debug_http_header_field(struct http_header_field);

#endif // _http_h_
