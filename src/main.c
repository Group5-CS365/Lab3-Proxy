/*
 * main.c
 * The main entry point of the proxy application.
 */

/*
  MIT License

  Copyright (c) 2018 Ryan Moeller

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
    for (int i = 0; i < sizeof (long_opts) / sizeof (struct option) - 1; ++i)
        printf("\t-%c, --%s, %s\n",
               long_opts[i].val, long_opts[i].name, opts_desc[i]);

    exit(status);
}

/*
 * Main entry point.
 * Processes command-line options and arguments, then runs the proxy.
 */
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
