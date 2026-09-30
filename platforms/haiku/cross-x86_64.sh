#!/bin/sh
# Cross-compile the Haiku x86_64 front end on a workstation, into dist/x86_64/,
# and put the libVLC it needs beside it. The package that comes out of this
# requires nothing but "haiku": HaikuPorts does publish vlc for x86_64, but
# depending on it drags in qt5 and the GStreamer stack - several hundred
# megabytes, and more than a live or minimal image has room for. Carrying a
# pruned libVLC instead costs about 55 MB unpacked and keeps the package
# self-contained, the way the x86_gcc2 one already is.
#
#   RTV_HAIKU_SYSROOT=/Volumes/HaikuX64/sysroot sh platforms/haiku/cross-x86_64.sh
#
# The sysroot is these Haiku packages unpacked on top of each other into
# <sysroot>/boot/system with Haiku's own `package extract`:
#
#   from .../haiku/master/x86_64/current/packages/
#       haiku, haiku_devel
#   from .../haikuports/master/x86_64/current/packages/
#       gcc, gcc_syslibs, gcc_syslibs_devel, vlc, vlc_devel
#       and everything vlc's "requires: lib:..." names except qt5 - 43 packages,
#       a52dec through zlib; ffmpeg is the big one at 9.5 MB
#
# <sysroot>/base-libs.txt lists the shared libraries the haiku and gcc_syslibs
# packages already provide, one soname per line. Anything else a plugin asks
# for has to travel in the package, and this script works out which.
#
# It has to sit on a CASE-SENSITIVE filesystem. Haiku ships both <string.h> and
# the BString header <String.h>, and on a case-insensitive volume - which is
# what macOS formats by default - the first include of <string.h> picks up
# BString instead and nothing compiles. `hdiutil create -fs "Case-sensitive
# APFS"` makes a volume that works.
#
# clang, not the Haiku cross gcc: it has an x86_64-unknown-haiku target of its
# own, so no toolchain has to be built. Only the C runtime startup files come
# from gcc, through -B.
set -eu

S="${RTV_HAIKU_SYSROOT:?set RTV_HAIKU_SYSROOT to the unpacked Haiku x86_64 sysroot}"
H="$S/boot/system/develop/headers"
SYSLIB="$S/boot/system/lib"
GCC_LIB="$S/boot/system/develop/tools/lib/gcc/x86_64-unknown-haiku/13.3.0"
BASE_LIBS="$S/base-libs.txt"
CXX="${CXX:-/opt/homebrew/opt/llvm/bin/clang++}"
LLD_BIN="${LLD_BIN:-/opt/homebrew/opt/lld/bin}"
READELF="${READELF:-/opt/homebrew/opt/llvm/bin/llvm-readelf}"

[ -f "$H/c++/string" ] || { echo "no C++ headers under $H/c++" >&2; exit 1; }
[ -f "$GCC_LIB/crtbeginS.o" ] || { echo "no crtbeginS.o under $GCC_LIB" >&2; exit 1; }
[ -f "$BASE_LIBS" ] || { echo "no base-libs.txt at $BASE_LIBS" >&2; exit 1; }
[ -d "$SYSLIB/vlc/plugins" ] || { echo "no VLC plugins under $SYSLIB" >&2; exit 1; }
[ -f "$H/os/support/String.h" ] && [ ! -f "$H/os/support/string.h" ] \
	|| { echo "$S is not on a case-sensitive filesystem" >&2; exit 1; }

cd "$(dirname "$0")/../.."
OUT=dist/x86_64
WORK=$OUT/obj
rm -rf "$OUT"; mkdir -p "$WORK" "$OUT/lib" "$OUT/vlc"

TARGET="--target=x86_64-unknown-haiku --sysroot=$S"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
	-I$H/c++ -I$H/c++/x86_64-unknown-haiku -I$H/private/netservices
	-Ishared -Iplatforms/haiku"

# The same files platforms/haiku/Makefile compiles with CORE_SRC_VLC and
# CORE_SRC_HTTP_HAIKU: libVLC for playback, Haiku's own network services for
# HTTP, so the package needs no libcurl either.
SRC="shared/core/StringUtil.cpp shared/core/Aes128.cpp shared/core/Country.cpp
shared/core/Strings.cpp shared/core/M3UParser.cpp shared/core/Paths.cpp
shared/core/PlaylistStore.cpp shared/core/ChannelIndex.cpp shared/core/Favorites.cpp
shared/core/MediaPlayer.cpp shared/core/HlsRelay.cpp shared/core/RelayedMediaPlayer.cpp
shared/core/AppController.cpp shared/core/VlcMediaPlayer.cpp shared/core/HaikuHttpClient.cpp
platforms/haiku/main.cpp platforms/haiku/MainWindow.cpp platforms/haiku/VideoView.cpp
platforms/haiku/Icons.cpp platforms/haiku/AudioOutput.cpp platforms/haiku/ResolverGuard.cpp
platforms/haiku/SocketGuard.cpp"

for f in $SRC; do
	echo "  CXX $f"
	$CXX $TARGET $CXXFLAGS -c "$f" -o "$WORK/$(basename "$f" .cpp).o"
done

# $ORIGIN/lib, as on x86_gcc2: Haiku's runtime loader does not search the
# binary's own directory, but it does search <binary's directory>/lib, and that
# is also the only place the VLC plugins - which carry no rpath - find the
# libraries they need.
echo "  LD  $OUT/RTelevision"
$CXX $TARGET -fuse-ld=lld -B"$LLD_BIN" -B"$GCC_LIB" \
	"$WORK"/*.o -o "$OUT/RTelevision" \
	-L"$S/boot/system/develop/lib" \
	-lbe -lmedia -lnetwork -lnetservices -lshared -lbnetapi -lvlc -lvlccore \
	-Wl,-rpath,'$ORIGIN/lib'
rm -rf "$WORK"

# ---------------------------------------------------------------- the bundle
echo "  bundling libVLC"
cp -L "$SYSLIB"/libvlc.so.5* "$SYSLIB"/libvlccore.so.9* "$OUT/lib/"
cp -R "$SYSLIB/vlc/plugins" "$OUT/vlc/plugins"

needed() { $READELF -d "$1" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\].*/\1/p'; }
have() {   # $1 = soname: already in the bundle, or part of the base system?
	[ -e "$OUT/lib/$1" ] && return 0
	grep -qxF "$1" "$BASE_LIBS"
}

# Pull in what the libraries and plugins still ask for, until nothing new shows
# up. A library dragged in this way can need more of its own, hence the loop.
pass=0
while [ "$pass" -lt 8 ]; do
	copied=0
	for so in $(find "$OUT/lib" "$OUT/vlc/plugins" -name '*.so*' -type f); do
		for name in $(needed "$so"); do
			have "$name" && continue
			[ -e "$SYSLIB/$name" ] || continue
			cp -L "$SYSLIB/$name" "$OUT/lib/$name"
			copied=$((copied + 1))
		done
	done
	[ "$copied" -eq 0 ] && break
	pass=$((pass + 1))
done

# Whatever still cannot be resolved would fail to load anyway. That is how the
# qt5 interface goes: nothing here provides libQt5Widgets, so those plugins are
# dropped rather than dragging Qt in.
removed=0
for p in "$OUT"/vlc/plugins/*/*.so; do
	[ -f "$p" ] || continue
	for name in $(needed "$p"); do
		have "$name" && continue
		rm -f "$p"; removed=$((removed + 1)); break
	done
done
find "$OUT/vlc/plugins" -type d -empty -delete 2>/dev/null || true

echo "built: $OUT/RTelevision"
echo "  libraries: $(ls "$OUT/lib" | wc -l | tr -d ' ')"
echo "  plugins  : $(find "$OUT/vlc/plugins" -name '*.so' | wc -l | tr -d ' ') kept, $removed pruned"
du -sh "$OUT"
