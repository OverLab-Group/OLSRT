# demos/Makefile
# Build OLSRT demos against the local source tree.

CC      := cc
ROOT    := ..

CFLAGS  := -std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L \
           -DOL_DISABLE_NUMA=1 -O2 -Wall -Wextra
LDFLAGS := -lpthread -lrt -ldl -lnuma

INC     := -I$(ROOT)/includes \
           -I$(ROOT)/includes/code/streams \
           -I$(ROOT)/includes/runtime

LIB_SRC := $(wildcard $(ROOT)/src/code/streams/*.c)
LIB     := libolsrt.so

DEMOS   := 01_hello_actor

.PHONY: all run clean

all: $(DEMOS)

$(LIB): $(LIB_SRC)
	$(CC) $(CFLAGS) -fPIC -shared $(INC) $(LIB_SRC) -o $(LIB) $(LDFLAGS)

%: %.c $(LIB)
	$(CC) $(CFLAGS) $(INC) $< -L. -lolsrt -Wl,-rpath,'$$ORIGIN' $(LDFLAGS) -o $@

run: 01_hello_actor
	./01_hello_actor

clean:
	rm -f $(LIB) $(DEMOS)
