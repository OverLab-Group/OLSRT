SRC := $(shell find src/code/streams -name '*.c')
OBJ := $(SRC:src/code/streams/%.c=build/code/streams/%.o)

INC := -Iincludes \
       -Iincludes/code/streams \
       -Iincludes/runtime

CC_LINUX   = gcc
CC_WINDOWS = x86_64-w64-mingw32-gcc
CC_BSD     = clang
CC_APPLE   = clang

OUT_LINUX   = bin/libolsrt.so
OUT_WINDOWS = bin/olsrt.dll
OUT_BSD     = bin/bsd/libolsrt.so
OUT_APPLE   = bin/libolsrt.dylib

.PHONY: all linux windows bsd apple all-platforms clean

all:
	@echo "Usage: make linux | make windows | make bsd | make apple | make all-platforms"

linux: $(OBJ)
	@echo "Building for Linux in process..."
	$(CC_LINUX) -shared -fPIC -O3 -w -flto -D_LINUX $(OBJ) -o $(OUT_LINUX)
	@echo "Build success."

windows: $(OBJ)
	@echo "Building for Windows in process..."
	$(CC_WINDOWS) -shared -O3 -w -s $(OBJ) -o $(OUT_WINDOWS)
	@echo "Build success."

# BSD
bsd: $(OBJ)
	@echo "Building for BSD in process..."
	$(CC_BSD) -shared -fPIC -O3 -w -s $(OBJ) -o $(OUT_BSD)
	@echo "Build success."

apple: $(OBJ)
	@echo "Building for Mac & Apple in process..."
	$(CC_APPLE) -dynamiclib -fPIC -O3 -w -s $(OBJ) -o $(OUT_APPLE)
	@echo "Build success."

build/code/streams/%.o: src/code/streams/%.c
	@mkdir -p $(dir $@)
	$(CC_LINUX) -c -fPIC -O3 -Wall $(INC) -o $@ $<

all-platforms: linux windows bsd apple

clean:
	rm -rf build $(OUT_LINUX) $(OUT_WINDOWS) $(OUT_BSD) $(OUT_APPLE)
