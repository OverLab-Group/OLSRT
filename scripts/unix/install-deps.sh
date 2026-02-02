#!/bin/bash
# Install dependencies on Unix-like systems

PLATFORM=$1
ARCH=$2

echo "Installing dependencies for $PLATFORM/$ARCH..."

# Detect package manager
if command -v apt-get >/dev/null 2>&1; then
    PKG_MGR="apt-get"
elif command -v yum >/dev/null 2>&1; then
    PKG_MGR="yum"
elif command -v dnf >/dev/null 2>&1; then
    PKG_MGR="dnf"
elif command -v pacman >/dev/null 2>&1; then
    PKG_MGR="pacman"
elif command -v brew >/dev/null 2>&1; then
    PKG_MGR="brew"
fi

# Base packages
BASE_PKGS="make cmake git"

# Platform-specific packages
case $PLATFORM in
    linux)
        # Native Linux - just need base packages
        EXTRA_PKGS="gcc g++"
        ;;
    windows)
        # Cross-compile to Windows
        EXTRA_PKGS="mingw-w64"
        ;;
    darwin)
        # Cross-compile to macOS (requires osxcross)
        EXTRA_PKGS="clang llvm"
        echo "Note: For macOS cross-compilation, install osxcross manually."
        echo "See: https://github.com/tpoechtrager/osxcross"
        ;;
    bsd)
        EXTRA_PKGS="clang"
        ;;
esac

# Install packages
if [ "$PKG_MGR" = "apt-get" ]; then
    sudo apt-get update
    sudo apt-get install -y $BASE_PKGS $EXTRA_PKGS
elif [ "$PKG_MGR" = "yum" ] || [ "$PKG_MGR" = "dnf" ]; then
    sudo $PKG_MGR install -y $BASE_PKGS $EXTRA_PKGS
elif [ "$PKG_MGR" = "pacman" ]; then
    sudo pacman -Sy --noconfirm $BASE_PKGS $EXTRA_PKGS
elif [ "$PKG_MGR" = "brew" ]; then
    brew install $BASE_PKGS $EXTRA_PKGS
fi

echo "Installation complete!"