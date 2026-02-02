#!/bin/bash
# Setup cross-compilation environment on Unix

TARGET=$1

echo "Setting up cross-compilation for $TARGET..."

case $TARGET in
    windows)
        # Install mingw-w64 on Linux/macOS
        if [[ "$OSTYPE" == "linux-gnu"* ]]; then
            sudo apt-get update
            sudo apt-get install -y mingw-w64 mingw-w64-tools
        elif [[ "$OSTYPE" == "darwin"* ]]; then
            brew install mingw-w64
        fi
        ;;
        
    darwin)
        echo "For macOS cross-compilation, you need osxcross."
        echo "See: https://github.com/tpoechtrager/osxcross"
        ;;
        
    bsd)
        if [[ "$OSTYPE" == "linux-gnu"* ]]; then
            sudo apt-get update
            sudo apt-get install -y clang llvm
        fi
        ;;
esac

echo "Cross-compilation setup complete for $TARGET."