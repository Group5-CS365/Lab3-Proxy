CFLAGS = -g -std=gnu11 -Isrc -Wall -Werror -pedantic

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
	CFLAGS += -D_GNU_SOURCE
endif

srcs = $(wildcard src/*.c)
objs = $(srcs:.c=.o)

all: proxy

proxy: $(objs)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

test: proxy
	. test-env && kyua test

clean:
	rm -rf $(objs) proxy

.PHONY: all test clean
