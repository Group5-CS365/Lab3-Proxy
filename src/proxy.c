#include "proxy.h"

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "http.h"

enum { SUCCESS = 0, FAILURE = -1 };

#define LISTEN_BACKLOG 8 // FIXME: what is a good value for this?
#define RECV_BUFLEN (REQUEST_LINE_MIN_BUFLEN*2)

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

static int
proxy_server_request(struct proxy *proxy, char *buf, size_t len) {
    int cfd, rval;
    struct addrinfo hint, *aip, *rp;

    //dummy initialization for testing
    static char const * const dummy =
      "GET /index.html HTTP/1.1\r\n"
      "Host: google.com\r\n"
      "Accept: */*\r\n"
      "\r\n";
    size_t dummylen = strlen(dummy);
    strcpy (buf, dummy); 
    len = dummylen;
    
    // initialize variables
    //pid = getpid();
    memset((void *) &hint, 0, sizeof(hint));
  
    // hints will help addrinfo to populate addresses in a certain way
    hint.ai_family = AF_UNSPEC;       /* Allow for IPv4 or IPv6 */
    hint.ai_socktype = SOCK_STREAM;   /* TCP socket */
    hint.ai_flags = AI_PASSIVE;       /* For wildcard IP address */
    hint.ai_protocol = 0;             /* Any protocol */

    rval = getaddrinfo("www.google.com", "80", &hint, &aip);
    if(rval != 0) {
      fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rval));
      return FAILURE;
    }

    for (rp = aip; rp != NULL; rp = rp->ai_next) {
        cfd = socket(rp->ai_family,
                 rp->ai_socktype,
                 rp->ai_protocol);
        if (cfd == -1)
            continue;
        if (connect(cfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break; // success!
        close(cfd);
    }

    if(rp == NULL) {
      perror("getaddrinfo(): could not connect");
      return FAILURE;
    }

    freeaddrinfo(aip);

    // write to server
    len = write(cfd, buf, len);
    if (len < 0) {
      perror("error writing to socket");
      return FAILURE;
    }

    //    bzero(buf, sizeof(buf));

    // read from server
    len = read(cfd, buf, RECV_BUFLEN);
    if(len < 1) {
      perror("read");
      return FAILURE;
    }
    else {
      // wait for response
      fprintf(stderr, "Response: %.*s\n", (int)len, buf); 
    }
    
return SUCCESS;
}

/*
 * Handle a request from the client.
 * Returns true to indicate proxy_recv_request() should be called again,
 * or false to indicate the connection has been closed.
 * Exits on error.
 */
static bool
proxy_handle_request(struct proxy *proxy, char *buf, size_t len)
{
    char const * const end = buf + len;
    struct http_request_line reqline = parse_http_request_line(buf, len);

    debug_http_request_line(reqline);

    if (!reqline.valid)
        return false;

    len -= buf - reqline.end;
    buf = reqline.end;

    for (struct http_header_field field = parse_http_header_field(buf, len);
         buf != end && field.valid;
         field = parse_http_header_field(buf, len)) {
        debug_http_header_field(field);
        len -= buf - field.end;
        buf = field.end;
    }

    // TODO: proxy HTTP traffic to the server specified in the Host header
    // OPTIONAL: set socket options according to headers (keepalive, etc)?
    proxy_server_request(proxy, buf, len);

    return true;
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
        return proxy_handle_request(proxy, buf, len);
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
