# libuv mirrors of the 16 OLSRT demos.
#
# Usage:
#   make              # build all 16
#   make clean
#   make CC=clang

CC      ?= cc
CFLAGS  := -std=gnu11 -O2 -g -Wall -Wextra \
           -Wno-unused-parameter -D_GNU_SOURCE \
           -I.

LDLIBS  := -luv -lpthread -lrt

TARGETS := \
    01_hello_actor \
    02_channel \
    03_parallel \
    04_timers \
    05_promise \
    06_dataflow \
    07_http_server \
    08_coroutines \
    09_supervisor \
    10_reactive \
    11_streams \
    12_semaphores \
    13_actor_supervisor \
    14_actor_channel \
    15_reactive_loop \
    16_full_stack

HEADERS := bench_util.h bench_channel.h

.PHONY: all clean
all: $(TARGETS)

%: %.c $(HEADERS)
	$(CC) $(CFLAGS) $< $(LDLIBS) -o $@

clean:
	rm -f $(TARGETS)
