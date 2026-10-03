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

DEMOS   := 01_hello_actor 02_channel 03_parallel 04_timers 05_promise 06_dataflow 07_http_server \
           08_coroutines 09_supervisor 10_reactive 11_streams 12_semaphores 13_actor_supervisor 14_actor_channel \
           15_reactive_loop 16_full_stack

.PHONY: all clean run run01 run02 run03 run04 run05 run06 run07 run08 run09 run10 run11 run12 run13 run14 run15 run16

all: $(DEMOS)

$(LIB): $(LIB_SRC)
	$(CC) $(CFLAGS) -fPIC -shared $(INC) $(LIB_SRC) -o $(LIB) $(LDFLAGS)

%: %.c $(LIB)
	$(CC) $(CFLAGS) $(INC) $< -L. -lolsrt -Wl,-rpath,'$$ORIGIN' $(LDFLAGS) -o $@

run: run01 run02 run03 run04 run05 run06 run07 run08 run09 run10 run11 run12 run13 run14 run15 run16

run01: 01_hello_actor ; ./01_hello_actor
run02: 02_channel ; ./02_channel
run03: 03_parallel ; ./03_parallel
run04: 04_timers ; ./04_timers
run05: 05_promise ; ./05_promise
run06: 06_dataflow ; ./06_dataflow
run08: 08_coroutines ; ./08_coroutines
run09: 09_supervisor ; ./09_supervisor
run10: 10_reactive ; ./10_reactive
run11: 11_streams ; ./11_streams
run12: 12_semaphores ; ./12_semaphores
run13: 13_actor_supervisor ; ./13_actor_supervisor
run14: 14_actor_channel ; ./14_actor_channel
run15: 15_reactive_loop ; ./15_reactive_loop
run16: 16_full_stack ; ./16_full_stack

run07: 07_http_server
	@echo "Run './07_test.sh' from another terminal, or manually:"
	@echo "  ./07_http_server &"
	@echo "  curl -s http://127.0.0.1:8080/"
	@echo "  # repeat 3 times"

clean:
	rm -f $(LIB) $(DEMOS)
