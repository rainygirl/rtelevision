#!/bin/sh
# Builds and installs R Television on a Debian/Ubuntu system.
#
#   ./install.sh            vendor libVLC, build, install into ~/.local
#   ./install.sh --deps     only fetch the build dependencies
#   PREFIX=/usr/local sudo -E ./install.sh    system-wide
#
# Only the GTK3 development headers need root. libVLC itself is vendored into
# the app, so no system VLC install is required.
set -eu

cd "$(dirname "$0")"

need=""
pkg-config --exists gtk+-3.0 2>/dev/null || need="$need libgtk-3-dev"
pkg-config --exists libcurl 2>/dev/null || need="$need libcurl4-openssl-dev"
command -v g++ >/dev/null 2>&1 || need="$need build-essential"

if [ -n "$need" ]; then
    echo "build dependencies missing:$need"
    if [ "$(id -u)" = "0" ]; then
        apt-get install -y $need
    elif command -v sudo >/dev/null 2>&1; then
        echo "installing with sudo (a password may be requested)"
        sudo apt-get install -y $need
    else
        echo "run: sudo apt-get install -y$need" >&2
        exit 1
    fi
fi

# libVLC: vendored, not installed system-wide.
sh fetch-libvlc.sh

[ "${1:-}" = "--deps" ] && exit 0

make
make install
