#!/bin/sh
# Builds the macOS app and installs it into /Applications.
#
#   ./install.sh                    host architecture
#   ARCHS="arm64 x86_64" ./install.sh    universal
#   INSTALL_DIR=~/Applications ./install.sh
set -eu

cd "$(dirname "$0")"

command -v clang++ >/dev/null 2>&1 || {
    echo "clang++ not found - install the Xcode command line tools:" >&2
    echo "  xcode-select --install" >&2
    exit 1
}

make ${ARCHS:+ARCHS="$ARCHS"} install ${INSTALL_DIR:+INSTALL_DIR="$INSTALL_DIR"}
