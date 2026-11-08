# Builds all benchmark binaries: olsrt/* and libuv/*.
# Run `make -C demos` first to produce demos/libolsrt.so.

CC := cc
CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -I.

OLSRT_INC := -I../../includes -I../../includes/code -I../../includes/code/streams \
             -I../../includes/code/network -I../../includes/runtime
OLSRT_LIBS := -L../../demos -lolsrt "-Wl,-rpath,$(CURDIR)/../../demos" \
              -lpthread -lrt -ldl -lnuma
LIBUV_LIBS := -luv -lpthread -lrt

NAMES := 01_hello_actor 02_channel 03_parallel 04_timers 05_promise \
         06_dataflow 07_http_server 08_coroutines 09_supervisor 10_reactive \
         11_streams 12_semaphores 13_actor_supervisor 14_actor_channel \
         15_reactive_loop 16_full_stack

OLSRT_BINS := $(addprefix olsrt/,$(NAMES))
LIBUV_BINS := $(addprefix libuv/,$(NAMES))

.PHONY: all olsrt libuv clean

all: olsrt libuv

olsrt: $(OLSRT_BINS)
libuv: $(LIBUV_BINS)

olsrt/%: olsrt/%.c bench_util.h
	$(CC) $(CFLAGS) $(OLSRT_INC) $< $(OLSRT_LIBS) -o $@

libuv/%: libuv/%.c bench_util.h bench_channel.h
	$(CC) $(CFLAGS) $< $(LIBUV_LIBS) -o $@

clean:
	rm -f $(OLSRT_BINS) $(LIBUV_BINS)
