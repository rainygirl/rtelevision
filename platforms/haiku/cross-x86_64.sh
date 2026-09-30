#!/bin/sh
# Cross-compile the Haiku x86_64 front end on a workstation, into dist/x86_64/.
# HaikuPorts publishes libVLC for x86_64, so this build links against the
# system one rather than carrying its own: the package requires lib:libvlc and
# pkgman installs it.
#
#   RTV_HAIKU_SYSROOT=/Volumes/HaikuX64/sysroot sh platforms/haiku/cross-x86_64.sh
#
# The sysroot is the contents of these Haiku packages, unpacked on top of each
# other into <sysroot>/boot/system with Haiku's own `package extract`:
#
#   from .../haiku/master/x86_64/current/packages/
#       haiku, haiku_devel
#   from .../haikuports/master/x86_64/current/packages/
#       gcc, gcc_syslibs, gcc_syslibs_devel, vlc, vlc_devel
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
GCC_LIB="$S/boot/system/develop/tools/lib/gcc/x86_64-unknown-haiku/13.3.0"
CXX="${CXX:-/opt/homebrew/opt/llvm/bin/clang++}"
LLD_BIN="${LLD_BIN:-/opt/homebrew/opt/lld/bin}"

[ -f "$H/c++/string" ] || { echo "no C++ headers under $H/c++" >&2; exit 1; }
[ -f "$GCC_LIB/crtbeginS.o" ] || { echo "no crtbeginS.o under $GCC_LIB" >&2; exit 1; }
[ -f "$H/os/support/String.h" ] && [ ! -f "$H/os/support/string.h" ] \
	|| { echo "$S is not on a case-sensitive filesystem" >&2; exit 1; }

cd "$(dirname "$0")/../.."
OUT=dist/x86_64
WORK=$OUT/obj
rm -rf "$WORK"; mkdir -p "$WORK"

TARGET="--target=x86_64-unknown-haiku --sysroot=$S"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
	-I$H/c++ -I$H/c++/x86_64-unknown-haiku -I$H/private/netservices
	-Ishared -Iplatforms/haiku"

# The same files platforms/haiku/Makefile compiles with CORE_SRC_VLC and
# CORE_SRC_HTTP_HAIKU: libVLC for playback, Haiku's own network services for
# HTTP, so the package needs no libcurl.
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

echo "  LD  $OUT/RTelevision"
$CXX $TARGET -fuse-ld=lld -B"$LLD_BIN" -B"$GCC_LIB" \
	"$WORK"/*.o -o "$OUT/RTelevision" \
	-L"$S/boot/system/develop/lib" \
	-lbe -lmedia -lnetwork -lnetservices -lshared -lbnetapi -lvlc -lvlccore
rm -rf "$WORK"
echo "built: $OUT/RTelevision"
