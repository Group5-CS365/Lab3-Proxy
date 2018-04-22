/*
 * http.h
 * Interface to the HTTP protocol parsers and utilities.
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

#ifndef _http_h_
#define _http_h_

#include <stdbool.h>
#include <stdlib.h>

#include "iostring.h"


/*
 * Request Line
 */

// minimum recommended supported request line length
// https://tools.ietf.org/html/rfc7230#section-3.1.1
#define REQUEST_LINE_MIN_BUFLEN 8000

struct http_request_line {
    struct iostring method, request_target, http_version;
    char *end;  // Where the parser stopped
    bool valid; // If false, none of the iostring fields should be used.
};

/*
 * Parse the given memory region for an HTTP request line.
 * ! Must not be passed a NULL pointer.
 * If the memory region contains a valid HTTP request line,
 * the .valid member of the returned data structure will be true.
 * Otherwise, .valid will be false and the iostring fields should not be used.
 */
struct http_request_line parse_http_request_line(char *buf, size_t len);

/*
 * Print the contents of the given data structure to stdout.
 * If the data structure is valid, all of its iostring fields are printed.
 * Otherwise, warns that the data is not valid.
 */
void debug_http_request_line(struct http_request_line);

/*
 * Status Line
 */

#define STATUS_LINE_MIN_BUFLEN 8000

struct http_status_line {
    struct iostring http_version, status_code, reason_phrase;
    char *end;  // Where the parser stopped
    bool valid; // If false, none of the iostring fields should be used.
};

/*
 * Parse the given memory region for an HTTP status line.
 * ! Must not be passed a NULL pointer.
 * If the memory region contains a valid HTTP status line,
 * the .valid member of the returned data structure will be true.
 * Otherwise, .valid will be false and the iostring fields should not be used.
 */
struct http_status_line parse_http_status_line(char *buf, size_t len);

/*
 * Print the contents of the given data structure to stdout.
 * If the data structure is valid, all of its iostring fields are printed.
 * Otherwise, warns that the data is not valid.
 */
void debug_http_status_line(struct http_status_line);

/*
 * Header Field
 */

struct http_header_field {
    struct iostring field_name, field_value;
    char *end;  // Where the parser stopped
    bool valid; // If false, none of the iostring fields should be used.
};

/*
 * Parse the given memory region for an HTTP header field.
 * ! Must not be passed a NULL pointer.
 * If the memory region contains a valid HTTP header field,
 * the .valid member of the returned data structure will be true.
 * Otherwise, .valid will be false and the iostring fields should not be used.
 */
struct http_header_field parse_http_header_field(char *buf, size_t len);

/*
 * Print the contents of the given data structure to stdout.
 * If the data structure is valid, all of its iostring fields are printed.
 * Otherwise, warns that the data is not valid.
 */
void debug_http_header_field(struct http_header_field);

#endif // _http_h_
