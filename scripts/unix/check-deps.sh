#!/bin/bash
# Check dependencies for Unix-like systems

PLATFORM=$1
ARCH=$2

echo "Checking dependencies for $PLATFORM/$ARCH..."

# Check for basic tools
for tool in make gcc clang; do
    if command -v $tool >/dev/null 2>&1; then
        echo "✅ $tool: $(which $tool)"
    else
        echo "❌ $tool: Not found"
    fi
done

# Platform-specific checks
case $PLATFORM in
    linux)
        echo "✅ Native Linux compilation supported"
        ;;
    windows)
        if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
            echo "✅ mingw-w64: Installed"
        else
            echo "❌ mingw-w64: Not installed"
            echo "   Install with: sudo apt-get install mingw-w64"
        fi
        ;;
    darwin)
        # Check for osxcross on Linux
        if [ -d "/opt/osxcross" ]; then
            echo "✅ osxcross: Installed"
        else
            echo "⚠ osxcross: Not installed (required for macOS cross from Linux)"
        fi
        ;;
    bsd)
        echo "✅ BSD compilation supported"
        ;;
esac

# Check for CMake
if command -v cmake >/dev/null 2>&1; then
    echo "✅ CMake: $(cmake --version | head -1)"
else
    echo "❌ CMake: Not found"
fi

echo "Dependency check complete!"