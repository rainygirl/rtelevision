#!/bin/sh
# Vendors libVLC into third_party/vlc-haiku-<arch> WITHOUT installing anything.
#
# Haiku packages are plain archives. pkgman is only asked what *would* be
# needed - it is answered "no" so it never acts - and the .hpkg files are then
# downloaded and unpacked with `package extract`. Nothing is activated and the
# system is left untouched, so the app carries its own libVLC the same way the
# macOS and Linux builds do.
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ARCH="$(uname -m)"
DEST="$ROOT/third_party/vlc-haiku-$ARCH"
CACHE="$ROOT/third_party/.hpkg-cache-$ARCH"

# On the x86_gcc2 hybrid the modern compiler and its libraries are the "x86"
# secondary architecture: packages are called vlc_x86, live in lib/x86 and
# develop/headers/x86, and the .hpkg files carry the primary arch suffix.
PKG_SUFFIX=""; LIBSUB=""; HPKG_ARCHES="$ARCH any"
if command -v getarch >/dev/null 2>&1; then
    PRIMARY="$(getarch -p 2>/dev/null || true)"
    SECONDARY="$(getarch -s 2>/dev/null | head -n 1 || true)"
    if [ "$PRIMARY" = x86_gcc2 ] && [ "$SECONDARY" = x86 ]; then
        PKG_SUFFIX=_x86; LIBSUB=x86
        HPKG_ARCHES="x86_gcc2 x86 any"
    elif [ -n "$PRIMARY" ]; then
        HPKG_ARCHES="$PRIMARY any"
    fi
fi
SYSLIB="/boot/system/lib${LIBSUB:+/$LIBSUB}"

if [ -f "$DEST/lib/libvlc.so" ] && [ -d "$DEST/plugins" ]; then
    echo "libVLC already vendored at $DEST"
    exit 0
fi

command -v package >/dev/null 2>&1 || { echo "this script only runs on Haiku" >&2; exit 1; }
DL=""
command -v curl >/dev/null 2>&1 && DL="curl -fsSL -o"
[ -n "$DL" ] || { command -v wget >/dev/null 2>&1 && DL="wget -q -O"; }
[ -n "$DL" ] || { echo "need curl or wget" >&2; exit 1; }

WORK="$(mktemp -d /tmp/vlcvendor.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$CACHE"

# --- where the packages live
BASE="$(pkgman list-repos | grep -A1 HaikuPorts | grep base-url | head -n 1 \
        | sed 's/.*base-url: *//')"
[ -n "$BASE" ] || { echo "could not find the HaikuPorts base URL" >&2; exit 1; }
echo "repository: $BASE"

# --- what would be installed (pkgman is told "no", so it only prints the plan)
echo no | pkgman install "vlc$PKG_SUFFIX" "vlc${PKG_SUFFIX}_devel" > "$WORK/plan.txt" 2>&1 || true
grep 'install package' "$WORK/plan.txt" | sed 's/.*install package \([^ ]*\).*/\1/' \
    | sort -u > "$WORK/pkgs.txt" || true

if [ ! -s "$WORK/pkgs.txt" ]; then
    # Everything is already on the system: vendor straight out of packagefs.
    echo "vlc is already installed; copying out of the system instead"
    mkdir -p "$DEST/lib" "$DEST/include" "$DEST/plugins"
    cp -a "/boot/system/develop/headers${LIBSUB:+/$LIBSUB}/vlc" "$DEST/include/vlc"
    cp -a "$SYSLIB"/libvlc.so* "$SYSLIB"/libvlccore.so* "$DEST/lib/" 2>/dev/null || true
    cp -a "/boot/system/develop/lib${LIBSUB:+/$LIBSUB}"/libvlc*.so "$DEST/lib/" 2>/dev/null || true
    [ -e "$DEST/lib/libvlc.so" ] || ln -sf libvlc.so.5 "$DEST/lib/libvlc.so"
    cp -a "$SYSLIB/vlc/plugins/." "$DEST/plugins/"
    echo "vendored libVLC -> $DEST"
    exit 0
fi

# The Qt interface and the GStreamer plugins are pulled in by the vlc package
# but this app uses neither, and they are by far the biggest downloads.
skip_package() {
    case "$1" in
        qt5*|qt6*|qthaiku*|phonon*|gst_*|gstreamer*) return 0 ;;
        *) return 1 ;;
    esac
}

echo "fetching packages (nothing is installed)..."
while read -r pkg; do
    [ -n "$pkg" ] || continue
    if skip_package "$pkg"; then continue; fi
    for suffix in $HPKG_ARCHES; do
        file="$pkg-$suffix.hpkg"
        if [ -f "$CACHE/$file" ]; then break; fi
        if $DL "$CACHE/$file.part" "$BASE/packages/$file" 2>/dev/null; then
            mv "$CACHE/$file.part" "$CACHE/$file"
            echo "  $file"
            break
        fi
        rm -f "$CACHE/$file.part"
    done
done < "$WORK/pkgs.txt"

mkdir -p "$WORK/root"
for hpkg in "$CACHE"/*.hpkg; do
    [ -f "$hpkg" ] || continue
    package extract -C "$WORK/root" "$hpkg" >/dev/null 2>&1 || true
done

echo "assembling $DEST"
rm -rf "$DEST"
mkdir -p "$DEST/lib" "$DEST/include" "$DEST/plugins"
RLIB="$WORK/root/lib${LIBSUB:+/$LIBSUB}"
cp -a "$WORK/root/develop/headers${LIBSUB:+/$LIBSUB}/vlc" "$DEST/include/vlc" 2>/dev/null || true
cp -a "$RLIB/"libvlc.so* "$DEST/lib/" 2>/dev/null || true
cp -a "$RLIB/"libvlccore.so* "$DEST/lib/" 2>/dev/null || true
cp -a "$RLIB/vlc/plugins/." "$DEST/plugins/" 2>/dev/null || true
# Haiku keeps the unversioned link-time symlinks under develop/lib.
cp -a "$WORK/root/develop/lib${LIBSUB:+/$LIBSUB}/"libvlc*.so "$DEST/lib/" 2>/dev/null || true
[ -e "$DEST/lib/libvlc.so" ] || ln -sf libvlc.so.5 "$DEST/lib/libvlc.so"
[ -e "$DEST/lib/libvlccore.so" ] || ln -sf libvlccore.so.9 "$DEST/lib/libvlccore.so"

[ -e "$DEST/lib/libvlc.so" ] || { echo "libvlc.so not found in the packages" >&2; exit 1; }

# --- pull in whatever the libraries and plugins still ask for
echo "resolving shared libraries..."
pass=0
while [ "$pass" -lt 8 ]; do
    : > "$WORK/missing.txt"
    find "$DEST/lib" "$DEST/plugins" -name '*.so*' -type f 2>/dev/null | while read -r so; do
        readelf -d "$so" 2>/dev/null \
            | sed -n 's/.*Shared library: \[\(.*\)\].*/\1/p' >> "$WORK/missing.txt"
    done
    sort -u "$WORK/missing.txt" -o "$WORK/missing.txt"

    copied=0
    while read -r soname; do
        [ -n "$soname" ] || continue
        [ -e "$DEST/lib/$soname" ] && continue
        [ -e "$SYSLIB/$soname" ] && continue   # part of the base system
        src="$(find "$RLIB" -name "$soname" 2>/dev/null | head -n 1)"
        [ -n "$src" ] || continue
        cp -aL "$src" "$DEST/lib/$soname" && copied=$((copied + 1))
    done < "$WORK/missing.txt"
    [ "$copied" -eq 0 ] && break
    pass=$((pass + 1))
done

# --- drop plugins that would fail to load anyway
removed=0
for plugin in "$DEST"/plugins/*/*.so; do
    [ -f "$plugin" ] || continue
    unresolved=0
    for soname in $(readelf -d "$plugin" 2>/dev/null \
                    | sed -n 's/.*Shared library: \[\(.*\)\].*/\1/p'); do
        [ -e "$DEST/lib/$soname" ] && continue
        [ -e "$SYSLIB/$soname" ] && continue
        unresolved=1
    done
    if [ "$unresolved" = 1 ]; then rm -f "$plugin"; removed=$((removed + 1)); fi
done
find "$DEST/plugins" -type d -empty -delete 2>/dev/null || true

echo
echo "vendored libVLC -> $DEST   (nothing was installed)"
echo "  libs   : $(ls "$DEST/lib" | wc -l)"
echo "  plugins: $(find "$DEST/plugins" -name '*.so' | wc -l) kept, $removed pruned"
du -sh "$DEST" 2>/dev/null || true
