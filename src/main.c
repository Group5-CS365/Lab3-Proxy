#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdbool.h>

#include "proxy.h"


static struct option const long_opts[] = {
    {"help", no_argument, NULL, 'h'},
    {"verbose", no_argument, NULL, 'v'},
    {NULL, 0, NULL, 0}
};

static void usage(char const * const progname, int status)
{
    static char const * const opts_desc[] = {
        "to display this usage message",
        "for verbose output",
    };

    printf("usage: %s [OPTIONS] PORT, where\n", progname);
    printf("  OPTIONS:\n");
    for (size_t i = 0; i < sizeof (long_opts) / sizeof (struct option) - 1; ++i)
        printf("\t-%c, --%s, %s\n",
               long_opts[i].val, long_opts[i].name, opts_desc[i]);

    exit(status);
}

int main(int argc, char * const argv[])
{
    int opt;
    uint16_t port;
    bool verbose = false;

    while (-1 != (opt = getopt_long(argc, argv, "hv", long_opts, NULL))) {
        switch (opt) {
        case 'h':
            usage(argv[0], EXIT_SUCCESS);
        case 'v':
            verbose = true;
            break;
        default:
            fprintf(stderr, "invalid option: %c\n", opt);
            usage(argv[0], EXIT_FAILURE);
        }
    }

    if (argc - optind != 1)
        usage(argv[0], EXIT_FAILURE);

    port = (uint16_t)atoi(argv[optind]);
    if (port == 0) { // atoi() returns 0 and sets errno on error
        fprintf(stderr, "invalid port: %s\n", argv[optind]);
        usage(argv[0], EXIT_FAILURE);
    }

    run_proxy(port, verbose);

    return EXIT_SUCCESS;
}
