/*
 * URI.h
 * Interface to the URI parser and utilities.
 */

/*
  MIT License

  Copyright (c) 2018 Ryan Moeller, Tyler Gearing, Alex Elkins

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

#ifndef _uri_h_
#define _uri_h_

#include <stdbool.h>
#include <stdlib.h>

#include "iostring.h"


/*
 * URI
 */

struct uri {
    struct iostring scheme, path_query_fragment;
    struct {
        struct iostring host, port;
    } authority;
    char *end;  // Where the parser stopped
    bool valid; // If false, none of the iostring fields should be used.
};

/*
 * Parse the given memory region for an HTTP request line.
 * ! Assumes the format scheme://host[:port][path?query#fragment]
 * ! The scheme://host portion must be present.
 * ! If the port is not specified, the string constant "80" is used.
 * ! If the path_query_fragment is empty, the string constant "/" is used.
 * ! Assumes the path, query, and fragment portion of the URL to be the tail
 *   of the URI after the optional port.
 * ! The URI is assumed to be the full len provided.
 * ! Must not be passed a NULL pointer.
 * If the contents of the memory is not a valid HTTP request line,
 * the .valid member of the returned data structure will be false.
 */
struct uri parse_uri(char *buf, size_t len);

/*
 * Print the contents of the given data structure to stdout.
 * If the data structure is valid, all of its iostring fields are printed.
 * Otherwise, warns that the data is not valid.
 */
void debug_uri(struct uri);

#endif // _uri_h_
