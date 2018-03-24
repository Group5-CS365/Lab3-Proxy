#include "proxy_request.h"

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>

#include <arpa/inet.h>

#include <http_parser.h>

#include "proxy_priv.h"
#include "bytes.h"
#include "byte_string.h"


enum state {
    s_init, s_url, s_header_field, s_header_value, s_body, s_error
};

struct header {
    STAILQ_ENTRY(header) link;
    byte_string field;
    byte_string value;
};

struct context {
    enum state state;
    byte_string chunks;
    bytes url;
    struct http_parser_url url_fields;
    STAILQ_HEAD(,header) headers;
};

static void
context_parse_url(struct context *ctx)
{
    struct http_parser_url url = {};
    bytes buf = bytes_of_byte_string(ctx->chunks);

    if (http_parser_parse_url(buf.iov_base, buf.iov_len, false, &url) != SUCCESS)
        fprintf(stderr, "error: parsing url failed\n");

    ctx->url = buf;
    ctx->url_fields = url;

    byte_string_free(&ctx->chunks);
}

static inline void
context_save_header_value(struct context *ctx)
{
    STAILQ_FIRST(&ctx->headers)->value = ctx->chunks;
    byte_string_init(&ctx->chunks);
}

static void
context_save_header_field(struct context *ctx)
{
    struct header *h = (struct header *)malloc(sizeof (struct header));
    h->field = ctx->chunks;
    byte_string_init(&ctx->chunks);
    STAILQ_INSERT_HEAD(&ctx->headers, h, link);
}

static inline void
context_handle_header_state(struct context *ctx)
{
    switch (ctx->state) {
    case s_url:
        context_parse_url(ctx);
        break;
    case s_header_field:
        context_save_header_field(ctx);
        break;
    case s_header_value:
        context_save_header_value(ctx);
        break;
    default:
        break;
    }
}

static char const *
name_of_url_field(enum http_parser_url_fields field)
{
    switch (field) {
#define $(x) case x: return #x
        $(UF_SCHEMA);
        $(UF_HOST);
        $(UF_PORT);
        $(UF_PATH);
        $(UF_QUERY);
        $(UF_FRAGMENT);
        $(UF_USERINFO);
        $(UF_MAX);
#undef $
    default: return "(unknown)";
    }
}

static void
context_debug(struct context *ctx)
{
    struct header *header;
    byte_string msg = {};
    enum { BUFLEN = 8 };
    char buf[BUFLEN];
    int len;

    byte_string_append_string(&msg, "URL: ");
    byte_string_append(&msg, ctx->url);
    byte_string_append_string(&msg, "\n");

    byte_string_append_string(&msg, "URL PORT: ");
    len = snprintf(buf, BUFLEN, "%u", ctx->url_fields.port);
    if (len >= BUFLEN) len = BUFLEN - 1;
    else if (len < 0) len = 0;
    byte_string_append(&msg, bytes_of_chars(buf, (size_t)len));
    byte_string_append_string(&msg, "\n");

    byte_string_append_string(&msg, "URL FIELDS: \n");
    for (enum http_parser_url_fields field = UF_SCHEMA; field < UF_MAX; ++field) {
        byte_string_append_string(&msg, "\t");
        byte_string_append_string(&msg, (char *)name_of_url_field(field));
        byte_string_append_string(&msg, ": ");

        if ((ctx->url_fields.field_set & (1 << field)) == 0) {
            byte_string_append_string(&msg, "unset");
        }
        else {
            bytes part;
            part.iov_base = ctx->url.iov_base + ctx->url_fields.field_data[field].off;
            part.iov_len = ctx->url_fields.field_data[field].len;
            byte_string_append(&msg, part);
        }

        byte_string_append_string(&msg, "\n");
    }

    STAILQ_FOREACH(header, &ctx->headers, link) {
        byte_string_append_string(&msg, "HEADER: ");
        byte_string_concat(&msg, header->field);
        byte_string_append_string(&msg, ": ");
        byte_string_concat(&msg, header->value);
        byte_string_append_string(&msg, "\n");
    }

    write_byte_string(STDOUT_FILENO, msg);

    byte_string_free(&msg);
}

static void
context_free(struct context *ctx)
{
    struct header *header;

    bytes_free(&ctx->url);
    byte_string_free(&ctx->chunks);
    STAILQ_FOREACH(header, &ctx->headers, link) {
        byte_string_free(&header->field);
        byte_string_free(&header->value);
        free(header);
    }
}

static int
on_url(http_parser *parser, char const *at, size_t len)
{
    struct context *ctx = parser->data;
    bytes chunk = bytes_of_chars((char *)at, len);

    byte_string_append(&ctx->chunks, chunk);

    ctx->state = s_url;

    return 0;
}

static int
on_header_field(http_parser *parser, char const *at, size_t len)
{
    struct context *ctx = parser->data;
    bytes chunk = bytes_of_chars((char *)at, len);

    context_handle_header_state(ctx);

    byte_string_append(&ctx->chunks, chunk);

    ctx->state = s_header_field;

    return 0;
}

static int
on_header_value(http_parser *parser, char const *at, size_t len)
{
    struct context *ctx = parser->data;
    bytes chunk = bytes_of_chars((char *)at, len);

    context_handle_header_state(ctx);

    byte_string_append(&ctx->chunks, chunk);

    ctx->state = s_header_value;

    return 0;
}

static int
on_headers_complete(http_parser *parser)
{
    struct context *ctx = parser->data;

    context_handle_header_state(ctx);

    context_debug(ctx);

    ctx->state = s_body;

    return 0;
}

bool
proxy_request(struct proxy *proxy)
{
    char buf[RECV_BUFLEN], *p = buf;
    ssize_t len;
    size_t nparsed;
    http_parser parser;
    http_parser_settings settings = {};
    struct context ctx = {};

    http_parser_init(&parser, HTTP_REQUEST);
    parser.data = &ctx;

    settings.on_url = on_url;
    settings.on_header_field = on_header_field;
    settings.on_header_value = on_header_value;
    settings.on_headers_complete = on_headers_complete;

    ctx.state = s_init;

    len = recv(proxy->sockfd, buf, RECV_BUFLEN, 0);
    switch (len) {
    case -1:
        perror("proxy_request(): recv() from client failed");
        close(proxy->sockfd);
        exit(EXIT_FAILURE);
    case 0:
        if (proxy->verbose)
            printf("connection closed by client %s:%d\n",
                   inet_ntoa(proxy->client.sin_addr),
                   ntohs(proxy->client.sin_port));
        return false;
    default:
        // TODO: proxy HTTP traffic to the server specified in the Host header
        // TODO: set socket options according to headers (keepalive, etc)?

        nparsed = http_parser_execute(&parser, &settings, p, len);

        context_free(&ctx);

        return nparsed == (size_t)len;
    }
}
