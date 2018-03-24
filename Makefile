CFLAGS = -g -std=c11 -Isrc -Wall -Werror -pedantic

srcs = $(wildcard src/*.c)
objs = $(srcs:.c=.o)

all: proxy

proxy: $(objs)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

clean:
	rm -rf $(objs) proxy

.PHONY: all clean
