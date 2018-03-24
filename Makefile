CFLAGS = -g -std=c11 -Wall -Wextra -Werror -pedantic -Isrc -I/usr/local/include
LIBS = -lhttp_parser

srcs = $(wildcard src/*.c)
objs = $(srcs:.c=.o)

all: proxy

proxy: $(objs)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -rf $(objs) proxy

.PHONY: all clean
