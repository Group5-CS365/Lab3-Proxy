#include "proxy.h"

#include <sys/socket.h>
#include <sys/types.h>
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

#include <arpa/inet.h>
#include <netinet/in.h>

#include "proxy_priv.h"
#include "proxy_request.h"


/*
 * Initialize a proxy data structure and start listening.
 */
static int
proxy_start(struct proxy *proxy, uint16_t port, bool verbose)
{
    int fd, val;
    struct sockaddr_in sa;

    memset(&sa, 0, sizeof sa);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == FAILURE) {
        perror("proxy_start(): failed to create socket");
        return FAILURE;
    }

    val = true;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) == FAILURE) {
        perror("proxy_start(): failed to set socket option SO_REUSEADDR");
        return FAILURE;
    }
    // TODO: set other socket options (keepalive, ndelay, etc)?

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

    while (proxy_accept(&proxy) == SUCCESS)
        ward_off_zombies();

    proxy_cleanup(&proxy);
}
