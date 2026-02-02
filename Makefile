# Main Makefile for OLSRT - Cross-platform build system
# Usage: make [TARGET=linux|windows|darwin|bsd] [ARCH=x86_64|i686|aarch64]

# Use command line or environment variables
TARGET ?= bsd
ARCH ?= x86_64

# Find source files
SRC_DIR := src/code/streams
SRC := $(wildcard $(SRC_DIR)/*.c)
OBJ_DIR := build/$(TARGET)/$(ARCH)/code/streams
OBJ := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC))

# Include directories
INC := -Iincludes \
       -Iincludes/code \
       -Iincludes/code/streams \
       -Iincludes/runtime

# Platform-specific settings
ifeq ($(TARGET),linux)
    CC := gcc
    CFLAGS := -fPIC -D_LINUX -D__linux__ -O3 -Wall -Wextra
    LDFLAGS := -shared -lpthread -lrt -ldl -flto
    OUTPUT := bin/$(TARGET)/$(ARCH)/libolsrt.so
    LINK_CMD := $(CC) -shared $(CFLAGS) $(LDFLAGS) $(OBJ) -o $(OUTPUT)
    
else ifeq ($(TARGET),windows)
    CC := x86_64-w64-mingw32-gcc
    CFLAGS := -D_WINDOWS -D_WIN32 -D_WIN64 -DWIN32_LEAN_AND_MEAN -O3 -Wall -Wextra
    LDFLAGS := -shared -lws2_32 -static-libgcc -s
    OUTPUT := bin/$(TARGET)/$(ARCH)/olsrt.dll
    LINK_CMD := $(CC) -shared $(CFLAGS) $(LDFLAGS) $(OBJ) -o $(OUTPUT)
    
else ifeq ($(TARGET),darwin)
    CC := clang
    CFLAGS := -fPIC -D_APPLE -D__APPLE__ -D__MACH__ -O3 -Wall -Wextra
    LDFLAGS := -dynamiclib -s
    OUTPUT := bin/$(TARGET)/$(ARCH)/libolsrt.dylib
    LINK_CMD := $(CC) -dynamiclib $(CFLAGS) $(LDFLAGS) $(OBJ) -o $(OUTPUT)
    
else ifeq ($(TARGET),bsd)
    CC := clang
    CFLAGS := -fPIC -D_BSD -D__FreeBSD__ -D__BSD__ -O3 -Wall -Wextra
    LDFLAGS := -shared -lpthread -s
    OUTPUT := bin/$(TARGET)/$(ARCH)/libolsrt.so
    LINK_CMD := $(CC) -shared $(CFLAGS) $(LDFLAGS) $(OBJ) -o $(OUTPUT)
endif

.PHONY: all linux windows darwin bsd all-platforms clean clean-all help

# Default target - BUILD EVERYTHING
all: create-dirs $(OBJ)
	@echo "========================================"
	@echo "Linking for $(TARGET)/$(ARCH)..."
	@echo "Command: $(LINK_CMD)"
	$(LINK_CMD)
	@echo ""
	@echo "✅ Build successful!"
	@echo "📂 Output: $(OUTPUT)"
	@echo "========================================"

# Create necessary directories
create-dirs:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p bin/$(TARGET)/$(ARCH)

# Rule to build object files - THIS IS THE KEY!
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "📦 Compiling: $<"
	$(CC) -c $(CFLAGS) $(INC) -o $@ $<

# Platform-specific shortcuts
linux:
	@$(MAKE) TARGET=linux ARCH=x86_64

windows:
	@$(MAKE) TARGET=windows ARCH=x86_64

darwin:
	@$(MAKE) TARGET=darwin ARCH=x86_64

bsd:
	@$(MAKE) TARGET=bsd ARCH=x86_64

# Build for all platforms
all-platforms:
	@for target in linux windows darwin bsd; do \
		for arch in x86_64 i686 aarch64; do \
			echo "=== Building for $$target/$$arch ==="; \
			$(MAKE) TARGET=$$target ARCH=$$arch clean; \
			$(MAKE) TARGET=$$target ARCH=$$arch || true; \
			echo ""; \
		done \
	done

# Clean current target
clean:
	@echo "🧹 Cleaning $(TARGET)/$(ARCH)..."
	@rm -rf build/$(TARGET)/$(ARCH) bin/$(TARGET)/$(ARCH)

# Clean everything
clean-all:
	@echo "🧹 Cleaning everything..."
	@rm -rf build bin

# Help
help:
	@echo "Usage:"
	@echo "  make [TARGET=platform] [ARCH=arch]   - Build with specified platform/arch"
	@echo "  make linux                          - Build for Linux x86_64"
	@echo "  make windows                        - Build for Windows x86_64"
	@echo "  make darwin                         - Build for macOS x86_64"
	@echo "  make bsd                            - Build for BSD x86_64"
	@echo "  make all-platforms                  - Build for all platforms"
	@echo "  make clean                          - Clean current target"
	@echo "  make clean-all                      - Clean everything"
	@echo ""
	@echo "Examples:"
	@echo "  make TARGET=windows ARCH=x86_64"
	@echo "  make TARGET=linux ARCH=aarch64"
