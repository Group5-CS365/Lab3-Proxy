#ifndef _uri_h_
#define _uri_h_

#include <stdbool.h>
#include <stdlib.h>

#include "iostring.h"

// This is getting ridiculous. We need a `struct string`-like type.
struct uri {
    struct iostring scheme, path_query_fragment;
    struct {
        struct iostring host, port;
    } authority;
    char *end;
    bool valid;
};

struct uri parse_uri(char *buf, size_t len);

void debug_uri(struct uri);

#endif // _uri_h_
