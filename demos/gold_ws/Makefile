# demos/gold_ws/Makefile
#
# Build the gold_ws web server against the local OLSRT source tree.

CC      := cc
ROOT    := ../..

CFLAGS  := -std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L \
           -DOL_DISABLE_NUMA=1 -O2 -Wall -Wextra
LDFLAGS := -lpthread -lrt -ldl -lnuma

INC     := -I$(ROOT)/includes \
           -I$(ROOT)/includes/code \
           -I$(ROOT)/includes/code/streams \
           -I$(ROOT)/includes/code/network \
           -I$(ROOT)/includes/runtime \
           -I.

LIB_SRC := $(wildcard $(ROOT)/src/code/streams/*.c)
LIB_SRC += $(ROOT)/src/code/network/ol_tcp.c
LIB     := libolsrt.so

SRCS    := main.c http.c router.c static.c fpm.c
OBJS    := $(SRCS:.c=.o)

BIN     := gold_ws

.PHONY: all clean run

all: $(BIN)

$(LIB): $(LIB_SRC)
	$(CC) $(CFLAGS) -fPIC -shared $(INC) $(LIB_SRC) -o $(LIB) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

$(BIN): $(OBJS) $(LIB)
	$(CC) $(CFLAGS) $(INC) $(OBJS) -L. -lolsrt -Wl,-rpath,'$$ORIGIN' $(LDFLAGS) -o $@

run: $(BIN)
	@echo "Starting gold_ws on 0.0.0.0:8080 ..."
	@echo "Point your browser to http://localhost:8080/"
	./$(BIN)

clean:
	rm -f $(BIN) $(OBJS) $(LIB)
