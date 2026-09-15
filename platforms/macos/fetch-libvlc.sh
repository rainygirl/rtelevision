#!/bin/sh
# Vendors the libVLC SDK (headers + dylibs + plugins) for macOS into third_party/vlc-macos.
# The universal (arm64 + x86_64) build is used so the same tree serves both slices.
set -eu

VLC_VERSION="${VLC_VERSION:-3.0.23}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="$ROOT/third_party/vlc-macos"
DMG="$ROOT/third_party/vlc-$VLC_VERSION-universal.dmg"
URL="https://get.videolan.org/vlc/$VLC_VERSION/macosx/vlc-$VLC_VERSION-universal.dmg"

if [ -f "$DEST/lib/libvlc.dylib" ]; then
    echo "libVLC SDK already vendored at $DEST"
    exit 0
fi

if [ ! -f "$DMG" ]; then
    echo "Downloading $URL"
    curl -# -L -o "$DMG.part" "$URL"
    mv "$DMG.part" "$DMG"
fi

MNT="$(mktemp -d /tmp/vlcmnt.XXXXXX)"
hdiutil attach -nobrowse -readonly -mountpoint "$MNT" "$DMG" >/dev/null
trap 'hdiutil detach "$MNT" >/dev/null 2>&1 || true' EXIT

SRC="$MNT/VLC.app/Contents/MacOS"
mkdir -p "$DEST"
rm -rf "$DEST/include" "$DEST/lib" "$DEST/plugins" "$DEST/share"
cp -R "$SRC/include" "$DEST/include"
cp -R "$SRC/lib"     "$DEST/lib"
cp -R "$SRC/plugins" "$DEST/plugins"
[ -d "$SRC/share" ] && cp -R "$SRC/share" "$DEST/share"
echo "$VLC_VERSION" > "$DEST/VERSION"

echo "Vendored libVLC $VLC_VERSION -> $DEST"
lipo -info "$DEST/lib/libvlc.dylib" 2>/dev/null || true
