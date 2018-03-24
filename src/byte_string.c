#include "byte_string.h"

#include <sys/types.h>
#include <sys/uio.h>

#include <stdlib.h>
#include <string.h>


#define DEFAULT_CAPACITY 32

static void
byte_string_resize(byte_string *bs, size_t capacity)
{
    bs->data = (bytes *)realloc(bs->data, capacity * sizeof (bytes));
    bs->capacity = capacity;
}

byte_string
byte_string_of_bytes(bytes b)
{
    byte_string bs;
    byte_string_init(&bs);
    byte_string_append(&bs, b);
    return bs;
}

void
byte_string_append(byte_string *bs, bytes b)
{
    if (bs->capacity == bs->length)
        byte_string_resize(bs, bs->capacity ? bs->capacity * 2 : DEFAULT_CAPACITY);

    bs->data[bs->length++] = b;
}

void
byte_string_append_string(byte_string *bs, char *s)
{
    byte_string_append(bs, bytes_of_string(s));
}

void
byte_string_concat(byte_string *a, byte_string b)
{
    if (a->capacity < a->length + b.length) {
        size_t blocks = (a->length + b.length) / DEFAULT_CAPACITY + 1;
        byte_string_resize(a, blocks * DEFAULT_CAPACITY);
    }
    memmove(a->data + a->length, b.data, b.length * sizeof (bytes));
    a->length += b.length;
}

size_t
byte_string_length(byte_string bs)
{
    size_t len = 0;

    for (size_t i = 0; i < bs.length; ++i)
        len += bs.data[i].iov_len;

    return len;
}

bytes
bytes_of_byte_string(byte_string bs)
{
    bytes buf;
    char *p;

    p = buf.iov_base = malloc(byte_string_length(bs));

    for (size_t i = 0; i < bs.length; ++i) {
        bytes b = bs.data[i];
        memmove(p, b.iov_base, b.iov_len);
        p += b.iov_len;
    }

    buf.iov_len = p - (char *)buf.iov_base;

    return buf;
}

ssize_t
write_byte_string(int fd, byte_string bs)
{
    return writev(fd, bs.data, bs.length);
}

void
byte_string_init(byte_string *bs)
{
    memset(bs, 0, sizeof (byte_string));
}

void
byte_string_free(byte_string *bs)
{
    free(bs->data);
    byte_string_init(bs);
}
