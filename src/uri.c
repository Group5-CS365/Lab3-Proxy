#include "uri.h"

#include <stdio.h>
#include <string.h>

struct uri
parse_uri(char *buf, size_t len)
{
//	static char const * const crlf = "\r\n";
//	static size_t const crlflen = 2;
	static char const * const fslash = "/:";
	static size_t const wslen = 5;

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
	while (p != end && memchr(fslash, *p, wslen) != NULL)
		++p;
	len -= p - site.scheme.p;

	if (p == end) {
		fputs("warning: invalid uri (after scheme)\n", stderr);
		fprintf(stderr, "SCHEME: %.*s\n", (int)site.scheme.len, site.scheme.p);
		return site;
	}

	// Host
	site.host.p = p;
	while (p != end && memchr(fslash, *p, wslen) == NULL)
		++p;
	site.host.len = p - site.host.p;

	// Eat colon or forwardslash
	while (p != end && memchr(fslash, *p, wslen) != NULL)
		++p;
	len -= p - site.host.p;

	if (p == end) {
		fputs("warning: invalid uri (after host)\n", stderr);
		fprintf(stderr, "SCHEME: %.*s\n", (int)site.scheme.len, site.scheme.p);
		fprintf(stderr, "HOST: %.*s\n", (int)site.host.len, site.host.p);
		return site;
	}
	++p;
	
	// Port
	site.port.p = p;
	while (p != end && memchr(fslash, *p, wslen) == NULL)
		++p;
	site.port.len = p - site.port.p;

	// Eat colon or forwardslash
	while (p != end && memchr(fslash, *p, wslen) != NULL)
		++p;
	len -= p - site.port.p;

	if (p == end) {
		fputs("warning: invalid uri (after port)\n", stderr);
		fprintf(stderr, "SCHEME: %.*s\n", (int)site.scheme.len, site.scheme.p);
		fprintf(stderr, "HOST: %.*s\n", (int)site.host.len, site.host.p);
		fprintf(stderr, "PORT: %.*s\n", (int)site.port.len, site.port.p);
		return site;
	}
	
	// Path
	site.path_query_fragment.p = p;
	while (p != end)
		++p;
	site.path_query_fragment.len = p - site.path_query_fragment.p;

	if (p == end) {
		fputs("warning: invalid uri (after port)\n", stderr);
                fprintf(stderr, "SCHEME: %.*s\n", (int)site.scheme.len, site.scheme.p);
                fprintf(stderr, "HOST: %.*s\n", (int)site.host.len, site.host.p);
                fprintf(stderr, "PORT: %.*s\n", (int)site.port.len, site.port.p);
		fprintf(stderr, "PATH QUERY FRAGMENT: %.*s\n", (int)site.path_query_fragment.len, site.path_query_fragment.p);
		return site;
	}

	site.valid = true;
	site.end = p + 1;

	return site;
}

void
debug_uri(struct uri reqhead)
{
	if (reqhead.valid)
		printf("valid URI:\n"
			"\tSCHEME: %.*s\n"
			"\tHOST: %.*s\n"
			"\tPORT: %.*s\n"
			"\tPATH QUERY FRAGMENT: %.*s\n",
			(int)reqhead.scheme.len, reqhead.scheme.p,
			(int)reqhead.host.len, reqhead.host.p,
			(int)reqhead.port.len, reqhead.port.p,
			(int)reqhead.path_query_fragment.len, reqhead.path_query_fragment.p);
	else
		printf("not a valid URI\n");
}
