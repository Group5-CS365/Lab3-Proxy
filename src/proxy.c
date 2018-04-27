/*
 * proxy.c
 * The core of the proxy application.
 */

/*
  MIT License

  Copyright (c) 2018 Ryan Moeller, Tyler Gearing

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

#include "proxy.h"

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <netdb.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "http.h"
#include "iostring.h"
#include "uri.h"

enum { SUCCESS = 0, FAILURE = -1 };

#define LISTEN_BACKLOG 8 // FIXME: what is a good value for this?
#define RECV_BUFLEN (REQUEST_LINE_MIN_BUFLEN*2)

#ifdef __linux__
/*
 * splice(2) is only available on Linux.
 */
#include <fcntl.h>
#else
/*
 * Everything else has to use this shim.
 */
#define BUFLEN 4096 // FIXME: what is a good value for this?
static ssize_t
splice(int fd_in, void *_off_in,
       int fd_out, void *_off_out,
       size_t len, unsigned int _flags)
{
    char buf[BUFLEN];
    int res;

    while (len) {
        res = read(fd_in, buf, BUFLEN);
        if (res == FAILURE) {
            perror("splice(): read failed");
            return FAILURE;
        }
        res = write(fd_out, buf, res);
        if (res == FAILURE) {
            perror("splice(): write failed");
            return FAILURE;
        }
        len -= res;
    }

    return len;
}
#endif

/*
 * The proxy context object contains data commonly used by proxy methods.
 */
struct proxy {
    bool verbose;
    int listen_fd;
    int client_fd;
    int server_fd;
    struct sockaddr_in client_addr;
};


/*
 * Initialize a proxy data structure and start listening.
 */
static int
proxy_start(struct proxy *proxy, uint16_t port, bool verbose)
{
    int const option = 1;

    int fd;
    struct sockaddr_in sa;

    memset(&sa, 0, sizeof sa);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == FAILURE) {
        perror("proxy_start(): failed to create socket");
        return FAILURE;
    }

    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(port);

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof option) == FAILURE) {
        perror("setsockopt(): failed to set socket to reuse address\n");
        close(fd);
        return FAILURE;
    }

    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) == FAILURE) {
        perror("proxy_start(): failed to bind socket");
        close(fd);
        return FAILURE;
    }

    if (listen(fd, LISTEN_BACKLOG) == FAILURE) {
        perror("proxy_start(): failed to listen on socket");
        close(fd);
        return FAILURE;
    }

    if (verbose)
        printf("listening on port %d\n", port);

    proxy->listen_fd = fd;
    proxy->verbose = verbose;

    return SUCCESS;
}

static void
proxy_cleanup(struct proxy *proxy)
{
    bool const verbose = proxy->verbose;

    if (verbose)
        puts("waiting for children");

    while (wait(NULL) != FAILURE)
        ;

    if (verbose)
        puts("closing socket fds");

    close(proxy->listen_fd);
    close(proxy->client_fd);
    close(proxy->server_fd);
}

/*
 * Connect to the server specified in a request.
 * Returns FAILURE if connection failed, otherwise a connected socket FD.
 */
static int
connect_server(char *host, char *port)
{
    int fd, rval;
    struct addrinfo *aip, hint = {
        // hints will help addrinfo to populate addr in a specific way
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,   // TCP info
        .ai_flags    = AI_PASSIVE,    // use my IP address
    };

    rval = getaddrinfo(host, port, &hint, &aip);
    if (rval != SUCCESS) {
        fprintf(stderr, "connect_server: failed to get address info: %s\n",
                gai_strerror(rval));
        return FAILURE;
    }

    for (struct addrinfo *rp = aip; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family,
                    rp->ai_socktype,
                    rp->ai_protocol);
        if (fd == FAILURE)
            continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) != FAILURE)
            break; // success!
        close(fd);
        fd = FAILURE;
    }

    freeaddrinfo(aip);

    return fd;
}

/*
 / Send the parts of the new HTTP request to the server.
 /
 / Request parts:
 / * Method
 / * Request path (minus proxy-to URI component)
 / > If we found a valid Proxy-Connection header:
 / * SP + Version + CRLF & Headers before Proxy-Connection
 / * Headers after Proxy-Connection & Body
 / > Otherwise:
 / * The rest (SP + Version + CRLF & Headers & Body)
 /
 / Using iovecs we can remove the URI from the request by skipping over it,
 / while only needing to make one syscall. Likewise for Proxy-Connection.
 */
static ssize_t
send_request(int server_fd,
             struct http_request_line reqln,
             struct uri uri,
             struct http_header_field proxyconn,
             size_t len)
{
    char * const version = reqln.http_version.p - 1; // -1 for SP
    size_t const version_offset = version - reqln.method.p;
    size_t const proxyconn_offset = proxyconn.field_name.p - version;
    size_t const proxyconn_end_offset = proxyconn.end - reqln.method.p;
    size_t const rest_len =
        len - (proxyconn.valid ? proxyconn_end_offset : version_offset);
    struct iovec const parts[] = {
        { // Method
            .iov_base = reqln.method.p,
            .iov_len = reqln.method.len + 1
        },
        { // Request path (minus proxy-to URI component)
            .iov_base = uri.path_query_fragment.p,
            .iov_len = uri.path_query_fragment.len
        },
        { // SP+Version+CRLF & Headers before Proxy-Connection OR The rest
            .iov_base = version,
            .iov_len = proxyconn.valid ? proxyconn_offset : rest_len
        },
        { // Headers after Proxy-Connection & Body
            .iov_base = proxyconn.end,
            .iov_len = rest_len
        }
    };
    size_t const num_parts =
        sizeof parts / sizeof (struct iovec) - (proxyconn.valid ? 0 : 1);

    return writev(server_fd, parts, num_parts);
}

/*
 * Send an HTTP response to the client.
 * Returns FAILURE on error, otherwise the number of bytes sent.
 */
static ssize_t
proxy_send_response(struct proxy *proxy, char *buf, size_t len, size_t more)
{
    bool const verbose = proxy->verbose;

    ssize_t res;

    res = write(proxy->client_fd, buf, len);
    if (res == FAILURE) {
        if (verbose)
            perror("send_response: failed to write response buffer");
        return FAILURE;
    }

    if (more) {
        res = splice(proxy->server_fd, NULL,
                     proxy->client_fd, NULL,
                     more, 0);
        if (res == FAILURE) {
            if (verbose)
                perror("send_response: failed to splice response data");
            return FAILURE;
        }
    }

    return len + more;
}

/*
 * Handle a response from the server.
 * Returns FAILURE if the response was invalid,
 * otherwise the length of the content.
 * Exits on fatal error.
 */
static ssize_t
proxy_handle_response(struct proxy *proxy, char *buf, size_t len)
// TODO: error responses
{
    char const * const end = buf + len;
    ssize_t content_length = 0;
    struct http_status_line statline = parse_http_status_line(buf, len);
    char *p = buf;
    size_t n = len, more = 0;
    bool const verbose = proxy->verbose;

    if (verbose)
        debug_http_status_line(statline);

    if (!statline.valid) {
        if (verbose)
            fputs("malformed response (invalid status line)\n", stderr);
        return FAILURE;
    }

    n -= statline.end - p;
    p = statline.end;

    for (struct http_header_field field;
         p < end && *p != '\r';
         n -= field.end - p, p = field.end) {

        field = parse_http_header_field(p, n);

        if (!field.valid)
            continue;

        if (verbose)
            debug_http_header_field(field);

        if (strncasecmp("Content-Length",
                        field.field_name.p, field.field_name.len) == SUCCESS) {
            unsigned long long l = strtoull(field.field_value.p, NULL, 10);
            // TODO: correct HTTP error response
            if (l > INT_MAX) {
                // FIXME: We should actually support larger content lengths.
                // INT_MAX is the max size supported by splice().
                if (verbose)
                    fprintf(stderr, "Invalid Content-Length: %llu\n", l);
                return FAILURE;
            }
            content_length = l;
            more = (size_t)l;
        }
    }

    // Skip over CRLF.
    n -= 2;
    p += 2;
    if (p > end) {
        if (verbose)
            fputs("malformed response (too short)\n", stderr);
        return FAILURE;
    }

    if (more < n) {
        if (verbose)
            fputs("malformed response (extra data)\n", stderr);
        return FAILURE;
    }

    more -= n; // n is the amount of the body already in the buffer
    if (proxy_send_response(proxy, buf, len, more) == FAILURE) {
        fputs("proxy_handle_response(): failed to send response", stderr);
        // If we can't send a response, there's nothing more we can do.
        proxy_cleanup(proxy);
        exit(EXIT_FAILURE);
    }

    return content_length;
}

/*
 * Handle a request from the client.
 * Returns FAILURE if the request was invalid, otherwise SUCCESS.
 */
static ssize_t
proxy_handle_request(struct proxy *proxy, char *buf, ssize_t len, size_t buflen)
// TODO: error responses
{
    bool const verbose = proxy->verbose;
    char const * const end = buf + len;
    struct http_request_line reqline = parse_http_request_line(buf, len);
    struct http_header_field proxyconn = { .valid = false };
    char htmp, ptmp, *p = buf;
    size_t n = len;
    struct uri uri;
    struct iostring host, port;
    int fd;

    if (verbose)
        debug_http_request_line(reqline);

    if (!reqline.valid) {
        if (verbose)
            fputs("malformed request (invalid request line)\n", stderr);
        return FAILURE;
    }

    n -= reqline.end - p;
    p = reqline.end;

    for (struct http_header_field field;
         p < end && *p != '\r';
         n -= field.end - p, p = field.end) {

        field = parse_http_header_field(p, n);

        if (!field.valid)
            continue;

        if (verbose)
            debug_http_header_field(field);

        if (strncasecmp("Proxy-Connection",
                        field.field_name.p, field.field_name.len) == SUCCESS)
            proxyconn = field;
    }

    // Skip over CRLF.
    n -= 2;
    p += 2;
    if (p > end) {
        if (verbose)
            fputs("malformed request (too short)\n", stderr);
        return FAILURE;
    }

    uri = parse_uri(reqline.request_target.p, reqline.request_target.len);

    if (verbose)
        debug_uri(uri);

    if (!uri.valid) {
        if (verbose)
            fputs("malformed request (invalid URI)\n", stderr);
        return FAILURE;
    }

    host = uri.authority.host;
    port = uri.authority.port;

    // Temporarily nul-terminate the host and port strings.
    htmp = host.p[host.len];
    host.p[host.len] = '\0';
    ptmp = port.p[port.len];
    if (ptmp != '\0') // The default port is already terminated (and const).
        port.p[port.len] = '\0';

    fd = connect_server(host.p, port.p);
    if (fd == FAILURE) {
        fputs("proxy_handle_request(): failed to connect to server\n", stderr);
        return FAILURE;
    }

    proxy->server_fd = fd;

    // Restore original values.
    host.p[host.len] = htmp;
    if (ptmp != '\0')
        port.p[port.len] = ptmp;

    if (send_request(fd,
                     reqline,
                     uri,
                     proxyconn,
                     len) == FAILURE) {
        if (verbose)
            perror("failed to send request");
        return FAILURE;
    }

    return SUCCESS;
}

static int
proxy_main(struct proxy *proxy)
{
    bool const verbose = proxy->verbose;
    int const client_fd = proxy->client_fd;
    struct sockaddr_in const client_addr = proxy->client_addr;

    int res = EXIT_SUCCESS;
    char buf[RECV_BUFLEN];
    ssize_t len;

    if (verbose)
        fprintf(stderr, "proxying HTTP for client %s:%d\n",
                inet_ntoa(client_addr.sin_addr),
                ntohs(client_addr.sin_port));

    for (;;) {

        //
        // Read a request from the client.
        //
        len = read(client_fd, buf, sizeof buf);
        if (len == FAILURE) {
            if (verbose)
                perror("failed to receive request");
            res = EXIT_FAILURE;
            break;
        }
        else if (len == 0) {
            if (verbose)
                fprintf(stderr, "connection closed by client %s:%d\n",
                        inet_ntoa(client_addr.sin_addr),
                        ntohs(client_addr.sin_port));
            break;
        }

        //
        // Transform the request and send it to the server.
        //
        len = proxy_handle_request(proxy, buf, len, sizeof buf);
        if (len == FAILURE) {
            if (verbose)
                fputs("failed to handle request\n", stderr);
            res = EXIT_FAILURE;
            break;
        }

        //
        // Read a response from the server.
        //
        len = read(proxy->server_fd, buf, sizeof buf);
        if (len == FAILURE) {
            if (verbose)
                perror("failed to receive response");
            res = EXIT_FAILURE;
            break;
        }
        else if (len == 0) {
            if (verbose)
                fputs("server closed connection without response\n", stderr);
            res = EXIT_FAILURE;
            break;
        }

        //
        // Forward the response to the client.
        //
        len = proxy_handle_response(proxy, buf, len);
        if (len == FAILURE) {
            if (verbose)
                fputs("failed to handle response\n", stderr);
            res = EXIT_FAILURE;
            break;
        }

    }

    proxy_cleanup(proxy);

    return res;
}

/*
 * Accept a connection and fork a new child.
 */
static int
proxy_accept(struct proxy *proxy)
{
    int const listen_fd = proxy->listen_fd;

    int fd, res;
    socklen_t socklen = sizeof (struct sockaddr_in);

    fd = accept(listen_fd,
                (struct sockaddr *)&proxy->client_addr,
                &socklen);
    assert(socklen == sizeof (struct sockaddr_in));
    if (fd == FAILURE) {
        perror("proxy_accept(): failed to accept a connection");
        return FAILURE;
    }

    if (proxy->verbose)
        puts("accepted a connection");

    switch (fork()) {
    case -1:
        perror("proxy_accept(): failed to fork a child process");
        close(fd);
        return FAILURE;
    case 0:
        close(listen_fd);
        proxy->client_fd = fd;
        res = proxy_main(proxy);
        exit(res);
    default:
        close(fd);
        return SUCCESS;
    }
}

/*
 * Wait for a connection, with timeout.
 */
static int
proxy_select(struct proxy *proxy)
{
    int const listen_fd = proxy->listen_fd;

    struct timeval tv;
    fd_set fds;

    tv.tv_sec = 5;
    tv.tv_usec = 0;

    FD_ZERO(&fds);
    FD_SET(listen_fd, &fds);

    if (select(listen_fd+1, &fds, NULL, NULL, &tv) == FAILURE) {
        perror("proxy_select(): select() failed");
        return FAILURE;
    }

    if (FD_ISSET(listen_fd, &fds))
        return proxy_accept(proxy);

    return SUCCESS; // timeout
}

/*
 * Try to bury any dead children, but do not block waiting for them to die.
 */
static void
ward_off_zombies()
{
    int status = 0;

    while (waitpid(0, &status, WNOHANG) > 0) {
        if (WIFSIGNALED(status)) {
            // TODO: More error checks!
            switch (WTERMSIG(status)) {
            case SIGSEGV:
                fputs("child segfaulted\n", stderr);
                break;
            default:
                fputs("child terminated\n", stderr);
                break;
            }
        }
    }
}

/*
 * Public high-level interface to run a proxy.
 */
void
run_proxy(uint16_t port, bool verbose)
{
    struct proxy proxy;

    if (proxy_start(&proxy, port, verbose) == FAILURE)
        errx(EXIT_FAILURE, "fatal error");

    while (proxy_select(&proxy) == SUCCESS)
        ward_off_zombies();

    proxy_cleanup(&proxy);
}
