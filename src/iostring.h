/*
 * iostring.h
 * Definition of the shared iostring data structure.
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

#ifndef _iostring_h_
#define _iostring_h_

#include <stdlib.h>

/*
 * An iostring does not need to be nul-terminated.
 * Instead, it stores the length in a separate field.
 * It also has the same layout as struct iovec.
 */
struct iostring {
    char *p;
    size_t len;
};

#define IOSTRING_TO_IOVEC(s) { .iov_base = (s).p, .iov_len = (s).len }

#endif // _iostring_h_
