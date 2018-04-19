#include "proxy.h"

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
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
#include <fcntl.h>
#else
#define BUFLEN 4096
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

struct proxy {
    int sockfd;
    bool verbose;
    struct sockaddr_in client;
};

/*
 * Initialize a proxy data structure and start listening.
 */
static int
proxy_start(struct proxy *proxy, uint16_t port, bool verbose)
{
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

    const int option = 1;

    //setsockopt to reuse address
    if(setsockopt(fd, SOL_SOCKET,SO_REUSEADDR, (char*)&option, sizeof(option)) == FAILURE) {
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

    if (proxy->verbose)
        printf("listening on port %d\n", port);

    proxy->sockfd = fd;
    proxy->verbose = verbose;

    return SUCCESS;
}

static void
proxy_cleanup(struct proxy *proxy)
{
    if (proxy->verbose)
        puts("waiting for children");

    while (wait(NULL) != FAILURE)
        ;

    if (proxy->verbose)
        puts("closing the listening socket");

    close(proxy->sockfd);
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
 * Send the parts of the new HTTP request to the server.
 */
static ssize_t
send_request(int fd, struct http_request_line reqln, struct uri uri, size_t len)
{
    // FIXME: temporary hack to make this work with dummy uri data.
    size_t version_offset = reqln.http_version.p - reqln.method.p - 1;
    size_t rest_len = len - version_offset;
    // Request parts:
    // * Method
    // * Request path
    // * The rest (SP+version & headers & body)
    struct iovec parts[] = {
        { // Method
            .iov_base = reqln.method.p,
            .iov_len = reqln.method.len + 1
        },
        { // Request path
            .iov_base = uri.path_query_fragment.p,
            .iov_len = uri.path_query_fragment.len
        },
        { // The rest
            .iov_base = reqln.http_version.p - 1,
            .iov_len = rest_len
        },
    };

    return writev(fd, parts, sizeof parts / sizeof (struct iovec));
}

/*
 * Receive an HTTP response from the server.
 */
static ssize_t
recv_response(int fd, char *buf, size_t len)
{
    // TODO: more parts of proxy_server_request() go in here?
    return read(fd, buf, len);
}

/*
 * Send an HTTP response to the client.
 */
static ssize_t
send_response(int server_fd, int client_fd, char *buf, size_t len, size_t more)
{
    int res;

    res = write(client_fd, buf, len);
    if (res == FAILURE) {
        perror("send_response(): failed to write response buffer");
        return FAILURE;
    }

    if (more) {
        res = splice(server_fd, NULL, client_fd, NULL, more, 0);
        if (res == FAILURE) {
            perror("send_response(): failed to splice response data");
            return FAILURE;
        }
    }

    return len + more;
}

/*
 * Handle a response from the server.
 * Returns true to indicate proxy_recv_request() should be called again,
 * or false to indicate the connection has been closed.
 * Exits on error.
 */
static bool
proxy_handle_response(struct proxy *proxy, int server_fd, char *buf, size_t len)
{
    char const * const end = buf + len;
    struct http_status_line statline = parse_http_status_line(buf, len);
    char *p = buf;
    size_t n = len;

    if (proxy->verbose)
        debug_http_status_line(statline);

    if (!statline.valid) {
        if (proxy->verbose)
            fputs("malformed response (invalid status line)\n", stderr);
        close(server_fd);
        return false;
    }

    if (proxy->verbose)
        fprintf(stderr, "*** begin response ***\n%.*s\n*** end response ***\n", (int)len, buf);

    n -= p - statline.end;
    p = statline.end;

    for (struct http_header_field field = parse_http_header_field(p, n);
         p < end && *p != '\r' && field.valid;
         field = parse_http_header_field(p, n)) {

        if (proxy->verbose)
            debug_http_header_field(field);

        n -= p - field.end;
        p = field.end;
    }

    // Skip over CRLF.
    n -= 2;
    p += 2;
    if (p > end) {
        if (proxy->verbose)
            fputs("malformed response (too short)\n", stderr);
        close(server_fd);
        return false;
    }

    if (send_response(server_fd, proxy->sockfd, buf, len, 0) == FAILURE) {
        fputs("proxy_handle_response(): failed to send response", stderr);
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    close(server_fd);

    return true;
}

/*
 * Handle a request from the client.
 * Returns true to indicate proxy_recv_request() should be called again,
 * or false to indicate the connection has been closed.
 * Exits on error.
 */
static bool
proxy_handle_request(struct proxy *proxy, char *buf, ssize_t len, size_t buflen)
{
    char const * const end = buf + len;
    struct http_request_line reqline = parse_http_request_line(buf, len);
    char htmp, ptmp, *p = buf;
    size_t n = len;
    struct uri uri;
    struct iostring host, port;
    int fd;

    if (proxy->verbose)
        debug_http_request_line(reqline);

    if (!reqline.valid) {
        if (proxy->verbose)
            fputs("malformed request (invalid request line)\n", stderr);
        return false;
    }

    n -= p - reqline.end;
    p = reqline.end;

    for (struct http_header_field field = parse_http_header_field(p, n);
         p < end && *p != '\r' && field.valid;
         field = parse_http_header_field(p, n)) {

        if (proxy->verbose)
            debug_http_header_field(field);

        n -= p - field.end;
        p = field.end;
    }

    // Skip over CRLF.
    n -= 2;
    p += 2;
    if (p > end) {
        if (proxy->verbose)
            fputs("malformed request (too short)\n", stderr);
        return false;
    }

    uri = parse_uri(reqline.request_target.p, reqline.request_target.len);

    if (proxy->verbose)
        debug_uri(uri);

    if (!uri.valid) {
        if (proxy->verbose)
            fputs("malformed request (invalid URI)\n", stderr);
        return false;
    }

    host = uri.authority.host;
    port = uri.authority.port;

    // Temporarily nul-terminate the host and port strings.
    htmp = host.p[host.len];
    if (htmp != '\0') // FIXME: `if` is a temporary hack to test with dummy URI
        host.p[host.len] = '\0';
    ptmp = port.p[port.len];
    if (ptmp != '\0') // The default port is already terminated.
        port.p[port.len] = '\0';

    fd = connect_server(host.p, port.p);
    if (fd == FAILURE) {
        fputs("proxy_handle_request(): failed to connect to server\n", stderr);
        exit(EXIT_FAILURE);
    }

    // Restore original values.
    if (htmp != '\0') // FIXME: `if` is a temporary hack to test with dummy URI
        host.p[host.len] = htmp;
    if (ptmp != '\0')
        port.p[port.len] = ptmp;

    if (send_request(fd, reqline, uri, len) == FAILURE) {
        fputs("failed to send request\n", stderr);
        close(fd);
        exit(EXIT_FAILURE);
    }

    len = recv_response(fd, buf, buflen);
    if (len == FAILURE) {
        fputs("failed to receive response\n", stderr);
        close(fd);
        exit(EXIT_FAILURE);
    }

    return proxy_handle_response(proxy, fd, buf, len);
}

/*
 * Receive a request from the client.
 * Returns true to indicate proxy_recv_request() should be called again,
 * or false to indicate the connection has been closed.
 * Exits on error.
 */
static bool
proxy_recv_request(struct proxy *proxy)
{
    char buf[RECV_BUFLEN];
    ssize_t len;

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
        return proxy_handle_request(proxy, buf, len, sizeof buf);
    }
}

static int
proxy_main(struct proxy *proxy)
{
    if (proxy->verbose)
        printf("proxying HTTP for client %s:%d\n",
               inet_ntoa(proxy->client.sin_addr),
               ntohs(proxy->client.sin_port));


    while (proxy_recv_request(proxy))
        ;

    close(proxy->sockfd);

    return EXIT_SUCCESS;
}

/*
 * Accept a connection and fork a new child.
 */
static int
proxy_accept(struct proxy *proxy)
{
    int fd;
    socklen_t socklen = sizeof (struct sockaddr_in);

    fd = accept(proxy->sockfd, (struct sockaddr *)&proxy->client, &socklen);
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
        close(proxy->sockfd);
        proxy->sockfd = fd;
        exit(proxy_main(proxy));
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
    struct timeval tv;
    fd_set fds;

    tv.tv_sec = 5;
    tv.tv_usec = 0;

    FD_ZERO(&fds);
    FD_SET(proxy->sockfd, &fds);

    if (select(proxy->sockfd+1, &fds, NULL, NULL, &tv) == FAILURE) {
        perror("proxy_select(): select() failed");
        return FAILURE;
    }

    if (FD_ISSET(proxy->sockfd, &fds))
        return proxy_accept(proxy);

    return SUCCESS; // timeout
}

/*
 * Try to bury any dead children, but do not block waiting for them to die.
 */
static void
ward_off_zombies()
{
    while (waitpid(0, NULL, WNOHANG) > 0)
        ; // TODO: error handling?
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
