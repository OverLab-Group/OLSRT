# ==============================================================================
# OLSRT - Universal Build System using GNU Make
# ==============================================================================
# This is the primary build system for OLSRT, providing cross-platform compilation
# for Linux, Windows, macOS, and BSD systems.
# ==============================================================================

# ------------------------------------------------------------------------------
# CONFIGURATION VARIABLES
# ------------------------------------------------------------------------------
# These variables can be overridden from command line or environment variables
TARGET ?= linux           # Target platform: linux, windows, darwin, bsd
ARCH ?= x86_64           # Target architecture: x86_64, i686, aarch64

# ------------------------------------------------------------------------------
# BUILD DEBUGGING CONFIGURATION
# ------------------------------------------------------------------------------
# Set DEBUG=1 for verbose build output
DEBUG ?= 0
ifeq ($(DEBUG),1)
    VERBOSE = @echo
    Q =
else
    VERBOSE = @echo
    Q = @
endif

# ------------------------------------------------------------------------------
# SOURCE FILES CONFIGURATION
# ------------------------------------------------------------------------------
# Locate all C source files in the streams directory
SRC_DIR := src/code/streams
SRC := $(wildcard $(SRC_DIR)/*.c)

# Object files will be placed in platform-specific directories
OBJ_DIR := build/$(TARGET)/$(ARCH)/code/streams
OBJ := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC))

# Include directories for the project
INC := -Iincludes \
       -Iincludes/code \
       -Iincludes/code/streams \
       -Iincludes/runtime

# ------------------------------------------------------------------------------
# PLATFORM-SPECIFIC COMPILATION SETTINGS
# ------------------------------------------------------------------------------
ifeq ($(TARGET),linux)
    # Linux settings
    CC := gcc
    CFLAGS := -fPIC -D_LINUX -D__linux__ -O3 -Wall -Wextra -g
    LDFLAGS := -shared -lpthread -lrt -ldl -flto
    OUTPUT := bin/$(TARGET)/$(ARCH)/libolsrt.so
    LINK_CMD := $(CC) -shared $(CFLAGS) $(LDFLAGS) $(OBJ) -o $(OUTPUT)
    PLATFORM_NAME := Linux
    
else ifeq ($(TARGET),windows)
    # Windows settings (using MinGW cross-compiler)
    CC := x86_64-w64-mingw32-gcc
    CFLAGS := -D_WINDOWS -D_WIN32 -D_WIN64 -DWIN32_LEAN_AND_MEAN -O3 -Wall -Wextra -g
    LDFLAGS := -shared -lws2_32 -static-libgcc -s
    OUTPUT := bin/$(TARGET)/$(ARCH)/olsrt.dll
    LINK_CMD := $(CC) -shared $(CFLAGS) $(LDFLAGS) $(OBJ) -o $(OUTPUT)
    PLATFORM_NAME := Windows
    
else ifeq ($(TARGET),darwin)
    # macOS settings
    CC := clang
    CFLAGS := -fPIC -D_APPLE -D__APPLE__ -D__MACH__ -O3 -Wall -Wextra -g
    LDFLAGS := -dynamiclib -s
    OUTPUT := bin/$(TARGET)/$(ARCH)/libolsrt.dylib
    LINK_CMD := $(CC) -dynamiclib $(CFLAGS) $(LDFLAGS) $(OBJ) -o $(OUTPUT)
    PLATFORM_NAME := macOS
    
else ifeq ($(TARGET),bsd)
    # BSD systems (FreeBSD, OpenBSD, NetBSD)
    CC := clang
    CFLAGS := -fPIC -D_BSD -D__FreeBSD__ -D__BSD__ -O3 -Wall -Wextra -g
    LDFLAGS := -shared -lpthread -s
    OUTPUT := bin/$(TARGET)/$(ARCH)/libolsrt.so
    LINK_CMD := $(CC) -shared $(CFLAGS) $(LDFLAGS) $(OBJ) -o $(OUTPUT)
    PLATFORM_NAME := BSD
    
else
    $(error Unsupported target platform: $(TARGET). Use: linux, windows, darwin, bsd)
endif

# ------------------------------------------------------------------------------
# TARGET DEFINITIONS
# ------------------------------------------------------------------------------
.PHONY: all linux windows darwin bsd all-platforms clean clean-all help debug-info

# Default target - Build for current platform
all: create-dirs $(OBJ)
	$(VERBOSE) "========================================"
	$(VERBOSE) "🔨 Linking for $(PLATFORM_NAME)/$(ARCH)..."
	$(VERBOSE) "Command: $(CC) -shared [object files] -o $(OUTPUT)"
	$(Q)$(LINK_CMD)
	$(VERBOSE) ""
	$(VERBOSE) "✅ Build successful!"
	$(VERBOSE) "📁 Output: $(OUTPUT)"
	$(VERBOSE) "Size: $$(stat -c%s $(OUTPUT) 2>/dev/null || stat -f%z $(OUTPUT) 2>/dev/null || echo "unknown") bytes"
	$(VERBOSE) "========================================"

# ------------------------------------------------------------------------------
# DIRECTORY MANAGEMENT
# ------------------------------------------------------------------------------
# Create necessary build directories
create-dirs:
	$(VERBOSE) "📁 Creating build directories..."
	$(Q)mkdir -p $(OBJ_DIR)
	$(Q)mkdir -p bin/$(TARGET)/$(ARCH)

# ------------------------------------------------------------------------------
# COMPILATION RULES
# ------------------------------------------------------------------------------
# Rule to compile C source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(VERBOSE) "🔧 Compiling: $<"
	$(Q)$(CC) -c $(CFLAGS) $(INC) -o $@ $<

# ------------------------------------------------------------------------------
# PLATFORM-SPECIFIC SHORTCUT TARGETS
# ------------------------------------------------------------------------------
linux:
	$(Q)$(MAKE) TARGET=linux ARCH=x86_64

windows:
	$(Q)$(MAKE) TARGET=windows ARCH=x86_64

darwin:
	$(Q)$(MAKE) TARGET=darwin ARCH=x86_64

bsd:
	$(Q)$(MAKE) TARGET=bsd ARCH=x86_64

# ------------------------------------------------------------------------------
# ADVANCED BUILD TARGETS
# ------------------------------------------------------------------------------
# Build for all supported platforms and architectures
all-platforms:
	$(VERBOSE) "🚀 Building OLSRT for all platforms and architectures..."
	@for target in linux windows darwin bsd; do \
		for arch in x86_64 i686 aarch64; do \
			$(VERBOSE) "=== Building for $$target/$$arch ==="; \
			$(Q)$(MAKE) TARGET=$$target ARCH=$$arch clean || true; \
			$(Q)$(MAKE) TARGET=$$target ARCH=$$arch || $(VERBOSE) "⚠️  Build failed for $$target/$$arch"; \
			$(VERBOSE) ""; \
		done \
	done
	$(VERBOSE) "✅ Multi-platform build complete!"

# ------------------------------------------------------------------------------
# CLEANUP TARGETS
# ------------------------------------------------------------------------------
# Clean build artifacts for current target
clean:
	$(VERBOSE) "🧹 Cleaning $(TARGET)/$(ARCH) build artifacts..."
	$(Q)rm -rf build/$(TARGET)/$(ARCH) bin/$(TARGET)/$(ARCH)

# Clean all build artifacts
clean-all:
	$(VERBOSE) "🧹 Cleaning all build artifacts..."
	$(Q)rm -rf build bin

# ------------------------------------------------------------------------------
# DEBUG AND INFORMATION TARGETS
# ------------------------------------------------------------------------------
# Display debug information about the build configuration
debug-info:
	$(VERBOSE) "🔍 OLSRT Build Configuration Debug Info"
	$(VERBOSE) "========================================"
	$(VERBOSE) "Target Platform: $(TARGET)"
	$(VERBOSE) "Target Architecture: $(ARCH)"
	$(VERBOSE) "Compiler: $(CC)"
	$(VERBOSE) "Compiler Flags: $(CFLAGS)"
	$(VERBOSE) "Linker Flags: $(LDFLAGS)"
	$(VERBOSE) "Source Directory: $(SRC_DIR)"
	$(VERBOSE) "Source Files: $(SRC)"
	$(VERBOSE) "Object Directory: $(OBJ_DIR)"
	$(VERBOSE) "Include Paths: $(INC)"
	$(VERBOSE) "Output: $(OUTPUT)"
	$(VERBOSE) "Debug Mode: $(DEBUG)"
	$(VERBOSE) "========================================"

# ------------------------------------------------------------------------------
# HELP TARGET
# ------------------------------------------------------------------------------
# Display help information
help:
	@echo "OLSRT Build System - GNU Make"
	@echo "================================"
	@echo ""
	@echo "Usage:"
	@echo "  make [TARGET=platform] [ARCH=arch]   - Build with specified platform/arch"
	@echo "  make linux                          - Build for Linux x86_64"
	@echo "  make windows                        - Build for Windows x86_64"
	@echo "  make darwin                         - Build for macOS x86_64"
	@echo "  make bsd                            - Build for BSD x86_64"
	@echo "  make all-platforms                  - Build for all platforms"
	@echo "  make clean                          - Clean current target"
	@echo "  make clean-all                      - Clean everything"
	@echo "  make debug-info                     - Show build configuration"
	@echo "  make help                           - Show this help"
	@echo ""
	@echo "Environment Variables:"
	@echo "  TARGET=linux|windows|darwin|bsd    - Target platform"
	@echo "  ARCH=x86_64|i686|aarch64           - Target architecture"
	@echo "  DEBUG=1                            - Enable verbose output"
	@echo ""
	@echo "Examples:"
	@echo "  make TARGET=windows ARCH=x86_64"
	@echo "  make TARGET=linux ARCH=aarch64"
	@echo "  make all-platforms"
	@echo "  DEBUG=1 make linux"
	@echo ""

# ------------------------------------------------------------------------------
# TEST TARGETS
# ------------------------------------------------------------------------------
# Run basic tests on the built library
test: all
	$(VERBOSE) "🧪 Running basic tests..."
	$(Q)if [ -f "$(OUTPUT)" ]; then \
		$(VERBOSE) "✅ Library exists: $(OUTPUT)"; \
		file $(OUTPUT); \
	else \
		$(VERBOSE) "❌ Library not found!"; \
		exit 1; \
	fi

# ------------------------------------------------------------------------------
# INSTALLATION TARGET (Experimental)
# ------------------------------------------------------------------------------
# Install the library to system directories
install: all
	$(VERBOSE) "📦 Installing OLSRT library..."
	$(Q)if [ "$(TARGET)" = "linux" ] || [ "$(TARGET)" = "bsd" ]; then \
		sudo cp $(OUTPUT) /usr/local/lib/ || \
		cp $(OUTPUT) ~/.local/lib/; \
		$(VERBOSE) "✅ Library installed to system."; \
	elif [ "$(TARGET)" = "darwin" ]; then \
		cp $(OUTPUT) /usr/local/lib/ || \
		cp $(OUTPUT) ~/Library/; \
		$(VERBOSE) "✅ Library installed to system."; \
	else \
		$(VERBOSE) "⚠️  Automatic installation not supported for $(TARGET)"; \
	fi

# ==============================================================================
# END OF MAKEFILE
# ==============================================================================