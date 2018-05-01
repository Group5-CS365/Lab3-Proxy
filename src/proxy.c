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

#ifdef __linux__
/* splice(2) is only available on Linux. */
#include <fcntl.h>
#else
/* for PIPE_SIZE */
#include <sys/pipe.h>
#endif

enum { SUCCESS = 0, FAILURE = -1 };

#define LISTEN_BACKLOG 8 // FIXME: what is a good value for this?
#define RECV_BUFLEN (REQUEST_LINE_MIN_BUFLEN*2)

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
	fprintf(stderr, "listening on port %d\n", port);

    proxy->listen_fd = fd;

    // Does this force verbosity for debugging?
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
 * Send an error response with a given status and reason on the socket fd.
 */
static ssize_t
send_error(int client_fd, enum http_status_code status)
{
    struct iovec parts[] = {
        { // Version
            .iov_base = "HTTP/1.0 ",
            .iov_len = 9
        },
        // Status
        IOSTRING_TO_IOVEC(http_errors[status].status),

        { // ws
            .iov_base = " ",
            .iov_len = 1
        },
        // Reason
        IOSTRING_TO_IOVEC(http_errors[status].reason),

        { // Content Type and Content Length name
            .iov_base = "\r\nContent-Type: text/plain\r\nContent-Length: ",
            .iov_len = 44
        },
        // Content-Length
        IOSTRING_TO_IOVEC (http_errors[status].content_length),

        { // Carriage return and Newline
            .iov_base = "\r\n\r\n",
            .iov_len = 4
        },
        // Body
        IOSTRING_TO_IOVEC(http_errors[status].body)
    };

    return writev(client_fd, parts, sizeof parts / sizeof (struct iovec));
}

enum {
    PIPE_FAIL      = -2,
    SPLICE_RX_FAIL = -3,
    SPLICE_TX_FAIL = -4,
    READ_FAIL      = -5,
    WRITE_FAIL     = -6,
    RX_SHORT       = -7
};

/*
 * Transfer len bytes from rx_fd to tx_fd.
 *
 * On Linux, this uses splice(2) to avoid copying the data.
 * On other platforms, splice(2) is not available so the data must be buffered
 * in userspace.
 */
static ssize_t
splice_loop(int rx_fd, int tx_fd, size_t len)
{
    ssize_t n, res, remaining = len;
#ifdef __linux__
    //
    // splice(2) is only available on Linux.
    //

    // splice(2) uses a pipe as an in-kernel "buffer" for zero-copy
    // transfer between sockets.
    // http://yarchive.net/comp/linux/splice.html
    int pipefd[2];

    if (pipe(pipefd) == FAILURE)
        return PIPE_FAIL;

    while (remaining > 0) {
        // Move a chunk of data from the rx socket to the pipe.
        // We want to read as much as possible without blocking.
        // NB: INT_MAX is the maximum size allowed by splice(2).
        res = splice(rx_fd, NULL,
                     pipefd[1], NULL,
                     INT_MAX, SPLICE_F_NONBLOCK);
        if (res == 0) {
            // XXX: should we retry?
            res = RX_SHORT;
            break;
        }
        if (res == FAILURE) {
            // TODO: Check for EAGAIN to retry?
            res = SPLICE_RX_FAIL;
            break;
        }

        n = res;

        // Move a chunk of data from the pipe to the tx socket.
        // We won't necessarily get to write the full chunk in one go,
        // so this loops until the pipe has been completely drained.
        do {
            ssize_t const res1 = splice(pipefd[0], NULL,
                                        tx_fd, NULL,
                                        n, 0);
            if (res1 == 0)
                // XXX: should we retry?
                break;
            if (res1 == FAILURE) {
                // TODO: Check for EAGAIN to retry?
                res = SPLICE_TX_FAIL;
                break;
            }

            n -= res1;
        } while (n);

        if (res < SUCCESS)
            break; // The inner loop failed, break out of the outer loop.

        remaining -= res;
    }

    close(pipefd[0]);
    close(pipefd[1]);

    if (res < SUCCESS)
        return res;
#else
    //
    // Everything else has to copy to and from userspace.
    //
    char buf[PIPE_SIZE];

    while (remaining > 0) {
        // Buffer a chunk of data from the rx socket.
        // We want to read as much as possible without blocking.
        res = recv(rx_fd, buf, PIPE_SIZE, MSG_DONTWAIT);
        if (res == 0)
            break; // peer closed connection
        if (res == FAILURE) {
            if (errno == EAGAIN)
                continue;
            return READ_FAIL;
        }

        n = res;

        // Write the buffer to the tx socket.
        // We won't necessarily get to write the full chunk in one go,
        // so this loops until the buffer has been completely drained.
        do {
            ssize_t const res1 = write(tx_fd, buf, n);
            if (res1 == 0)
                break; // XXX: should we retry?
            if (res1 == FAILURE)
                return WRITE_FAIL;
            n -= res1;
        } while (n);

        remaining -= res;
    }
#endif

    return len;
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
 /
 / If more data is expected than what was in the buffer, the remaining data is
 / forwarded to the server in chunks. On Linux, this takes advantage of
 / splice(2) for zero-copy transfers. On everything else, it buffers PIPE_SIZE
 / bytes at a time.
 */
static ssize_t
proxy_send_request(struct proxy *proxy,
                   struct http_request_line reqln,
                   struct uri uri,
                   struct http_header_field proxyconn,
                   size_t len,
                   size_t more)
{
    bool const verbose = proxy->verbose;
    int const client_fd = proxy->client_fd;
    int const server_fd = proxy->server_fd;

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
        // Request path (minus proxy-to URI component)
        IOSTRING_TO_IOVEC(uri.path_query_fragment),
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

    if (writev(server_fd, parts, num_parts) == FAILURE) {
        if (verbose)
            perror("proxy_send_request: failed to write request buffer");
        return FAILURE;
    }

    if (more) {
        switch(splice_loop(client_fd, server_fd, more)) {
        case PIPE_FAIL:
            perror("proxy_send_request: failed to create a pipe");
            return FAILURE;
        case SPLICE_RX_FAIL:
            perror("proxy_send_request: failed to splice from server socket");
            return FAILURE;
        case SPLICE_TX_FAIL:
            perror("proxy_send_request: failed to splice to client socket");
            return FAILURE;
        case READ_FAIL:
            perror("proxy_send_request: failed to read from server socket");
            return FAILURE;
        case WRITE_FAIL:
            perror("proxy_send_request: failed to write to client socket");
            return FAILURE;
        case RX_SHORT:
            fputs("proxy_send_request: expected more data\n", stderr);
            return FAILURE;
        default:
            break;
        }
    }

    return len + more;
}

/*
 * Send an HTTP response to the client.
 * Returns FAILURE on error, otherwise the number of bytes sent.
 */
static ssize_t
proxy_send_response(struct proxy *proxy, char *buf, size_t len, size_t more)
{
    bool const verbose = proxy->verbose;
    int const client_fd = proxy->client_fd;
    int const server_fd = proxy->server_fd;

    if (write(client_fd, buf, len) == FAILURE) {
        if (verbose)
            perror("proxy_send_response: failed to write response buffer");
        return FAILURE;
    }

    if (more) {
        switch(splice_loop(server_fd, client_fd, more)) {
        case PIPE_FAIL:
            perror("proxy_send_response: failed to create a pipe");
            return FAILURE;
        case SPLICE_RX_FAIL:
            perror("proxy_send_response: failed to splice from server socket");
            return FAILURE;
        case SPLICE_TX_FAIL:
            perror("proxy_send_response: failed to splice to client socket");
            return FAILURE;
        case READ_FAIL:
            perror("proxy_send_response: failed to read from server socket");
            return FAILURE;
        case WRITE_FAIL:
            perror("proxy_send_response: failed to write to client socket");
            return FAILURE;
        case RX_SHORT:
            perror("proxy_send_response: expected more data");
            return FAILURE;
        default:
            break;
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
{
    bool const verbose = proxy->verbose;
    int const client_fd = proxy->client_fd;

    char const * const end = buf + len;
    ssize_t content_length = 0;
    struct http_status_line statline = parse_http_status_line(buf, len);
    char *p = buf;
    size_t n = len, more = 0;

    if (verbose)
        debug_http_status_line(statline);

    if (!statline.valid) {
        if (verbose)
            fputs("malformed response (invalid status line)\n", stderr);
        send_error(client_fd, BAD_GATEWAY);
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
                        field.field_name.p, field.field_name.len) == SUCCESS)
            content_length = strtoll(field.field_value.p, NULL, 10);
    }

    // Skip over CRLF.
    n -= 2;
    p += 2;

    if (p > end) {
        if (verbose)
            fputs("malformed response (too short)\n", stderr);
        send_error(client_fd, BAD_GATEWAY);
        return FAILURE;
    }

    if (content_length < n) {
        if (verbose)
            fputs("malformed response (extra data)\n", stderr);
        send_error(client_fd, BAD_GATEWAY);
        return FAILURE;
    }

    // n is the amount of the body already in the buffer.
    more = content_length - n;

    if (proxy_send_response(proxy, buf, len, more) == FAILURE) {
        fputs("proxy_handle_response(): failed to send response\n", stderr);
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
{
    bool const verbose = proxy->verbose;
    int const client_fd = proxy->client_fd;

    char const * const end = buf + len;
    ssize_t content_length = 0;
    struct http_request_line reqline = parse_http_request_line(buf, len);
    struct http_header_field proxyconn = { .valid = false };
    char htmp, ptmp, *p = buf;
    size_t n = len, more = 0;
    struct uri uri;
    struct iostring host, port;
    int fd;

    if (verbose)
        debug_http_request_line(reqline);

    if (!reqline.valid) {
        if(verbose)
            fputs("malformed request (invalid request line)\n", stderr);
        send_error(client_fd, BAD_REQUEST);
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
        else if (strncasecmp("Content-Length",
                             field.field_name.p, field.field_name.len) == SUCCESS)
            content_length = strtoll(field.field_value.p, NULL, 10);
    }

    // Skip over CRLF.
    n -= 2;
    p += 2;
    // TODO: Develop test to trigger this error
    if (p > end) {
        if (verbose)
            fputs("malformed request (too short)\n", stderr);
        send_error(client_fd, BAD_REQUEST);
        return FAILURE;
    }

    if (content_length < n) {
        if (verbose)
            fputs("malformed request (extra data)\n", stderr);
        send_error(client_fd, BAD_REQUEST);
        return FAILURE;
    }

    // n is the amount of the body already in the buffer.
    more = content_length - n;

    uri = parse_uri(reqline.request_target.p, reqline.request_target.len);

    if (verbose)
        debug_uri(uri);

    if (!uri.valid) {
        if (verbose)
            fputs("malformed request (invalid URI)\n", stderr);
        send_error(client_fd, BAD_REQUEST);
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
        if (verbose)
            fputs("proxy_handle_request(): failed to connect to server\n",
                  stderr);
        send_error(client_fd, INTERNAL_ERROR);
        return FAILURE;
    }

    proxy->server_fd = fd;

    // Restore original values.
    host.p[host.len] = htmp;
    if (ptmp != '\0')
        port.p[port.len] = ptmp;

    if (proxy_send_request(proxy,
                           reqline,
                           uri,
                           proxyconn,
                           len,
                           more) == FAILURE) {
        if (verbose)
            perror("failed to send request");
        send_error(client_fd, INTERNAL_ERROR);
        return FAILURE;
    }

    return content_length;
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
            // TODO: Add 500 Internal Error
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
            // TODO: Add 500 Internal Error
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
