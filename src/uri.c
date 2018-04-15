#include "uri.h"

#include <stdio.h>

struct uri
parse_uri(char *buf, size_t len)
{
    // TODO
    struct uri uri = { // dummy data for testing
        .scheme = { .p = "http", .len = 4 },
        .authority = {
            .host = { .p = "google.com", .len = 10 },
            .port = { .p = "80", .len = 2 }
        },
        .path_query_fragment = { .p = "/", .len = 1 },
        .valid = true
    };

    return uri;
}

void
debug_uri(struct uri uri)
{
    puts("TODO: debug_uri()");
}
