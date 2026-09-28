# demos/Makefile
# Build all OLSRT demos against the local source tree.

CC      := cc
ROOT    := ..

CFLAGS  := -std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L \
           -DOL_DISABLE_NUMA=1 -O2 -Wall -Wextra
LDFLAGS := -lpthread -lrt -ldl -lnuma

INC     := -I$(ROOT)/includes \
           -I$(ROOT)/includes/code \
           -I$(ROOT)/includes/code/streams \
           -I$(ROOT)/includes/runtime

LIB_SRC := $(wildcard $(ROOT)/src/code/streams/*.c)
LIB_SRC += $(ROOT)/src/code/network/ol_tcp.c
LIB     := libolsrt.so

DEMOS   := 01_hello_actor 02_channel 03_parallel \
           04_timers 05_promise 06_dataflow 07_http_server

.PHONY: all clean run run01 run02 run03 run04 run05 run06 run07

all: $(DEMOS)

$(LIB): $(LIB_SRC)
	$(CC) $(CFLAGS) -fPIC -shared $(INC) $(LIB_SRC) -o $(LIB) $(LDFLAGS)

%: %.c $(LIB)
	$(CC) $(CFLAGS) $(INC) $< -L. -lolsrt -Wl,-rpath,'$$ORIGIN' $(LDFLAGS) -o $@

run: run01 run02 run03 run04 run05 run06

run01: 01_hello_actor ; ./01_hello_actor
run02: 02_channel ; ./02_channel
run03: 03_parallel ; ./03_parallel
run04: 04_timers ; ./04_timers
run05: 05_promise ; ./05_promise
run06: 06_dataflow ; ./06_dataflow

run07: 07_http_server
	@echo "Run './07_test.sh' from another terminal, or manually:"
	@echo "  ./07_http_server &"
	@echo "  curl -s http://127.0.0.1:8080/"
	@echo "  # repeat 3 times"

clean:
	rm -f $(LIB) $(DEMOS)
