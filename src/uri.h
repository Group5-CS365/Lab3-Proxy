#ifndef _uri_h_

#define _uri_h_

#include <stdbool.h>
#include <stdlib.h>

struct uri {
	struct {
		char *p;
		size_t len;
	} scheme, host, port, path_query_fragment;
	char *end;
	bool valid;
};

struct uri parse_uri(char *buy, size_t len);

void debug_uri(struct uri);

#endif // _http_h_
