CFLAGS = -g -std=c11 -Wall -Wextra -Werror -pedantic -Isrc -I/usr/local/include
LIBS = -lhttp_parser

srcs = $(wildcard src/*.c)
objs = $(srcs:.c=.o)

proxy: $(objs)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

.PHONY: clean
clean:
	rm -rf $(objs) proxy
