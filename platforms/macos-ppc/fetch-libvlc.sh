#!/bin/sh
# Vendors the libVLC SDK for Mac OS X 10.4 PowerPC into third_party/vlc-ppc.
#
# 0.9.10 is the last VLC that runs on Tiger: 1.1.12 and 2.0.10 have PowerPC
# builds too, but both declare LSMinimumSystemVersion 10.5. It ships its headers
# and dylibs inside the application bundle, so there is no separate SDK to get.
#
# Run this on any Mac that can mount a disk image; the result is copied to the
# PowerPC machine along with the sources. Nothing here has to run on Tiger.
set -eu

VLC_VERSION="${VLC_VERSION:-0.9.10}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="$ROOT/third_party/vlc-ppc"
DMG="$ROOT/third_party/vlc-$VLC_VERSION-powerpc.dmg"
URL="https://download.videolan.org/pub/videolan/vlc/$VLC_VERSION/macosx/vlc-$VLC_VERSION-powerpc.dmg"

if [ -f "$DEST/lib/libvlc.dylib" ]; then
    echo "libVLC SDK already vendored at $DEST"
    exit 0
fi

if [ ! -f "$DMG" ]; then
    echo "Downloading $URL"
    curl -# -L -o "$DMG.part" "$URL"
    mv "$DMG.part" "$DMG"
fi

MNT="$(mktemp -d /tmp/vlcppc.XXXXXX)"
hdiutil attach -nobrowse -readonly -mountpoint "$MNT" "$DMG" >/dev/null
trap 'hdiutil detach "$MNT" >/dev/null 2>&1 || true' EXIT

# 0.9 calls the plugin tree "modules"; 1.1 onwards call it "plugins".
SRC="$MNT/VLC.app/Contents/MacOS"
mkdir -p "$DEST"
rm -rf "$DEST/include" "$DEST/lib" "$DEST/modules"
cp -R "$SRC/include" "$DEST/include"
cp -R "$SRC/lib"     "$DEST/lib"
cp -R "$SRC/modules" "$DEST/modules"
echo "$VLC_VERSION" > "$DEST/VERSION"

echo "Vendored libVLC $VLC_VERSION -> $DEST"
echo "  modules: $(ls "$DEST/modules" | wc -l | tr -d ' ')"
file "$DEST/lib/libvlc.dylib" 2>/dev/null | head -1 || true
