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
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>


enum { SUCCESS = 0, FAILURE = -1 };

#define LISTEN_BACKLOG 8 // FIXME: what is a good value for this?
#define RECV_BUFLEN 2048 // FIXME: ^

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
    struct sockaddr_in sa = {};

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == FAILURE) {
        perror("proxy_start(): failed to create socket");
        return FAILURE;
    }

    // TODO: set socket options (keepalive, ndelay, etc)?

    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(port);

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
 * Process a client request.
 * Returns true to indicate proxy_request() should be called again,
 * or false to indicate the connection has been closed.
 * Exits on error.
 */
static bool
proxy_request(struct proxy *proxy)
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
        // TODO: proxy HTTP traffic to the server specified in the Host header
        // TODO: set socket options according to headers (keepalive, etc)?
        printf("%*s", (int)len, buf);

        return true;
    }
}

static int
proxy_main(struct proxy *proxy)
{
    if (proxy->verbose)
        printf("proxying HTTP for client %s:%d\n",
               inet_ntoa(proxy->client.sin_addr),
               ntohs(proxy->client.sin_port));


    while (proxy_request(proxy))
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
