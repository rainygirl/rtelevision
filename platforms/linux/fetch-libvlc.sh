#!/bin/sh
# Vendors libVLC (headers, shared objects and the plugin tree) into
# third_party/vlc-linux-<arch> so the app runs without a system VLC install.
#
# No root needed: the .deb files are fetched with `apt-get download` and
# unpacked with `dpkg -x`.
#
# Two rules keep the bundle small:
#   1. The dependency walk stops at packages this machine already has. Recursing
#      through them drags in the whole mesa/LLVM chain for a plugin we do not
#      even ship.
#   2. Only shared objects that libvlc or a plugin actually asks for are copied,
#      resolved by running ldd until nothing is missing.
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ARCH="$(dpkg --print-architecture)"
DEST="$ROOT/third_party/vlc-linux-$ARCH"
CACHE="$ROOT/third_party/.deb-cache-$ARCH"
MULTIARCH="$(gcc -print-multiarch 2>/dev/null || echo x86_64-linux-gnu)"

if [ -f "$DEST/lib/libvlc.so" ] && [ -d "$DEST/plugins" ]; then
    echo "libVLC already vendored at $DEST"
    exit 0
fi

command -v apt-get >/dev/null 2>&1 || {
    echo "this script expects a Debian/Ubuntu system (apt-get)" >&2
    exit 1
}

WANT="libvlc-dev libvlc5 libvlccore9 vlc-plugin-base vlc-plugin-video-output vlc-data"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$CACHE"

echo "resolving packages (stopping at what is already installed)..."
printf '%s\n' $WANT > "$WORK/queue.txt"
: > "$WORK/seen.txt"
: > "$WORK/todo.txt"

while [ -s "$WORK/queue.txt" ]; do
    pkg="$(head -n 1 "$WORK/queue.txt")"
    tail -n +2 "$WORK/queue.txt" > "$WORK/queue.next" && mv "$WORK/queue.next" "$WORK/queue.txt"
    [ -n "$pkg" ] || continue
    grep -qxF "$pkg" "$WORK/seen.txt" && continue
    echo "$pkg" >> "$WORK/seen.txt"

    dpkg -s "$pkg" >/dev/null 2>&1 && continue          # already on the system
    apt-cache show "$pkg" >/dev/null 2>&1 || continue   # virtual package
    echo "$pkg" >> "$WORK/todo.txt"

    apt-cache depends --no-recommends --no-suggests --no-conflicts --no-breaks \
                      --no-replaces --no-enhances "$pkg" 2>/dev/null \
        | sed -n 's/^ *\(Pre\)\?Depends: //p' | grep -v '^<' | grep -v ':i386' \
        >> "$WORK/queue.txt" || true
done

echo "fetching $(wc -l < "$WORK/todo.txt") packages"
while read -r pkg; do
    ls "$CACHE/${pkg}_"*.deb >/dev/null 2>&1 && continue
    (cd "$CACHE" && apt-get download -qq "$pkg" >/dev/null 2>&1) || \
        echo "  warning: could not download $pkg" >&2
done < "$WORK/todo.txt"

mkdir -p "$WORK/root"
for deb in "$CACHE"/*.deb; do
    [ -f "$deb" ] || continue
    dpkg -x "$deb" "$WORK/root"
done

echo "assembling $DEST"
rm -rf "$DEST"
mkdir -p "$DEST/lib" "$DEST/include" "$DEST/plugins"

cp -a "$WORK/root/usr/lib/$MULTIARCH/"libvlc.so* "$DEST/lib/" 2>/dev/null || true
cp -a "$WORK/root/usr/lib/$MULTIARCH/"libvlccore.so* "$DEST/lib/" 2>/dev/null || true
cp -a "$WORK/root/usr/include/vlc" "$DEST/include/" 2>/dev/null || true
cp -a "$WORK/root/usr/lib/$MULTIARCH/vlc/plugins/." "$DEST/plugins/" 2>/dev/null || true
cp -a "$WORK/root/usr/share/vlc" "$DEST/share" 2>/dev/null || true

[ -e "$DEST/lib/libvlc.so" ] || ln -sf libvlc.so.5 "$DEST/lib/libvlc.so"
[ -e "$DEST/lib/libvlccore.so" ] || ln -sf libvlccore.so.9 "$DEST/lib/libvlccore.so"

echo "resolving shared libraries..."
pass=0
while [ "$pass" -lt 8 ]; do
    find "$DEST/lib" "$DEST/plugins" -name '*.so*' -type f 2>/dev/null \
        | while read -r so; do
            LD_LIBRARY_PATH="$DEST/lib" ldd "$so" 2>/dev/null \
                | sed -n 's/^\t\([^ ]*\) => not found$/\1/p'
          done | sort -u > "$WORK/missing.txt"
    [ -s "$WORK/missing.txt" ] || break

    copied=0
    while read -r soname; do
        src="$(find "$WORK/root" -name "$soname" 2>/dev/null | head -n 1)"
        [ -n "$src" ] || continue
        cp -aL "$src" "$DEST/lib/$soname" 2>/dev/null && copied=$((copied + 1))
    done < "$WORK/missing.txt"
    [ "$copied" -eq 0 ] && break
    pass=$((pass + 1))
done

# Anything still unresolved would fail to load; drop it rather than ship it.
removed=0
for plugin in "$DEST"/plugins/*/*.so; do
    [ -f "$plugin" ] || continue
    if LD_LIBRARY_PATH="$DEST/lib" ldd "$plugin" 2>/dev/null | grep -q 'not found'; then
        rm -f "$plugin"
        removed=$((removed + 1))
    fi
done
find "$DEST/plugins" -type d -empty -delete 2>/dev/null || true

echo
echo "vendored libVLC -> $DEST"
echo "  libs   : $(ls "$DEST/lib" | wc -l)"
echo "  plugins: $(find "$DEST/plugins" -name '*.so' | wc -l) kept, $removed pruned"
du -sh "$DEST"
