#!/bin/sh
# Builds and installs RTelevision on Haiku.
#
#   ./install.sh          install dependencies (if any), build, install
#   ./install.sh --deps   only install the packages
#   ./install.sh --build  only build
#
# Works on a minimal image with no `make`: if make is missing the sources are
# compiled directly with the same flags the Makefile uses.
set -eu

cd "$(dirname "$0")"
SHARED=../../shared
BUILD=build
APP=RTelevision

# ~/config/apps is packagefs and read-only; user binaries go here. The app has a
# directory of its own because the runtime loader looks for a program's
# libraries in <its directory>/lib - the only place the VLC plugins, which carry
# no rpath, find the bundled libraries they need.
APPS_DIR=${APPS_DIR:-$HOME/config/non-packaged/apps/$APP}
DATA_DIR=${DATA_DIR:-$HOME/config/non-packaged/data/RTelevision}
DESKBAR_DIR=$HOME/config/settings/deskbar/menu/Applications

have() { command -v "$1" >/dev/null 2>&1; }

# Copies the resources (signature, version, icon) into the attributes Tracker
# and the Deskbar read. resattr does it directly, so it also works where the
# registrar fails to sniff the ELF and mimeset would leave nothing behind.
set_attrs() {
    resattr -o "$1" "$BUILD/$APP.rsrc"
    settype -t application/x-vnd.be-elfexecutable "$1"
}

# ---------------------------------------------------------------- dependencies
missing=""
have c++ || have g++ || missing="$missing gcc"
[ -e /boot/system/develop/headers/os/BeBuild.h ] || missing="$missing haiku_devel"

if [ -n "$missing" ]; then
    echo "installing:$missing"
    pkgman install -y $missing
fi

# libVLC is vendored into third_party, not installed: fetch-libvlc.sh downloads
# the .hpkg files and unpacks them, leaving the system untouched. A system
# vlc_devel is used if one happens to be there.
ARCH=$(uname -m)
VLC_VENDOR=../../third_party/vlc-haiku-$ARCH
if [ ! -e "$VLC_VENDOR/include/vlc/vlc.h" ] && [ ! -e /boot/system/develop/headers/vlc/vlc.h ]; then
    sh fetch-libvlc.sh || echo "note: could not vendor libVLC; building without playback"
fi

# FFmpeg is the fallback where VLC does not exist for the architecture at all
# (arm64 today); scripts/build-ffmpeg-haiku.sh cross-builds it.
FF_VENDOR=../../third_party/ffmpeg-haiku-$ARCH

VLC=0
[ -e "$VLC_VENDOR/include/vlc/vlc.h" ] && VLC=1
[ -e /boot/system/develop/headers/vlc/vlc.h ] && VLC=1
FFMPEG=0
[ $VLC = 0 ] && [ -e "$FF_VENDOR/include/libavcodec/avcodec.h" ] && FFMPEG=1
CURL=0
[ -e /boot/system/develop/headers/curl/curl.h ] && CURL=1

if [ $VLC = 1 ]; then backend=libVLC
elif [ $FFMPEG = 1 ]; then backend=FFmpeg
else backend="none"; fi
echo "media backend: $backend"
echo "http backend : $([ $CURL = 1 ] && echo libcurl || echo 'Haiku netservices')"

[ "${1:-}" = "--deps" ] && exit 0

# ----------------------------------------------------------------------- build
if have make; then
    make VLC=$VLC FFMPEG=$FFMPEG CURL=$CURL
else
    echo "make not found; compiling directly"
    CXX=${CXX:-$(have g++ && echo g++ || echo c++)}
    CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -I$SHARED -I."
    LIBS="-lbe -lmedia -lnetwork"
    if [ $CURL = 1 ]; then
        LIBS="$LIBS -lcurl"
    else
        CXXFLAGS="$CXXFLAGS -I/boot/system/develop/headers/private/netservices"
        # libnetservices.a pulls BPrivate::HashString out of libshared.a.
        LIBS="$LIBS -lnetservices -lshared -lbnetapi"
    fi
    if [ $FFMPEG = 1 ]; then
        CXXFLAGS="$CXXFLAGS -I$FF_VENDOR/include"
        LIBS="$LIBS -L$FF_VENDOR/lib -lavformat -lavcodec -lswscale -lswresample -lavutil"
        LIBS="$LIBS -Wl,-rpath,\$ORIGIN/lib"
    fi
    if [ $VLC = 1 ]; then
        if [ -e "$VLC_VENDOR/include/vlc/vlc.h" ]; then
            CXXFLAGS="$CXXFLAGS -I$VLC_VENDOR/include"
            # $ORIGIN: the runtime loader does not search the binary's own
            # directory by itself.
            LIBS="$LIBS -L$VLC_VENDOR/lib -lvlc -lvlccore -Wl,-rpath,\$ORIGIN/lib"
        else
            LIBS="$LIBS -lvlc"
        fi
    fi

    mkdir -p $BUILD/obj
    OBJS=""
    for src in "$SHARED"/core/*.cpp *.cpp; do
        name=$(basename "$src" .cpp)
        case "$name" in
            VlcMediaPlayer)    [ $VLC = 1 ] || continue ;;
            FFmpegMediaPlayer) [ $FFMPEG = 1 ] || continue ;;
            NullMediaPlayer)   [ $VLC = 0 ] && [ $FFMPEG = 0 ] || continue ;;
            CurlHttpClient)  [ $CURL = 1 ] || continue ;;
            HaikuHttpClient) [ $CURL = 0 ] || continue ;;
        esac
        echo "  CXX $src"
        $CXX $CXXFLAGS -c "$src" -o "$BUILD/obj/$name.o"
        OBJS="$OBJS $BUILD/obj/$name.o"
    done
    echo "  LD  $BUILD/$APP"
    $CXX $OBJS $LIBS -o "$BUILD/$APP"
    # signature, version and icon
    rc -o "$BUILD/$APP.rsrc" "$APP.rdef"
    xres -o "$BUILD/$APP" "$BUILD/$APP.rsrc"
    set_attrs "$BUILD/$APP"
fi

[ "${1:-}" = "--build" ] && exit 0

# --------------------------------------------------------------------- install
# Earlier versions put the binary and the libraries straight into apps/.
PARENT=$(dirname "$APPS_DIR")
if [ -f "$PARENT/$APP" ]; then
    echo "removing the old flat installation in $PARENT"
    rm -rf "$PARENT/$APP" "$PARENT/vlc" "$PARENT/seed-playlist.m3u"
    for lib in "$VLC_VENDOR"/lib/*.so* "$FF_VENDOR"/lib/*.so*; do
        [ -e "$lib" ] && rm -f "$PARENT/$(basename "$lib")"
    done
fi
mkdir -p "$APPS_DIR" "$DATA_DIR" "$DESKBAR_DIR"
cp "$BUILD/$APP" "$APPS_DIR/$APP"
set_attrs "$APPS_DIR/$APP"
cp ../../resources/seed-playlist.m3u "$DATA_DIR/seed-playlist.m3u"
# The bundled libVLC/FFmpeg are LGPL/GPL: their notices go with them.
cp ../../LICENSE ../../THIRD-PARTY-NOTICES.md "$DATA_DIR/"
rm -rf "$DATA_DIR/licenses"
cp -a ../../licenses "$DATA_DIR/licenses"
if [ $FFMPEG = 1 ]; then
    mkdir -p "$APPS_DIR/lib"
    cp -a "$FF_VENDOR/lib/"*.so* "$APPS_DIR/lib/" 2>/dev/null || true
    cp ../../resources/seed-playlist.m3u "$APPS_DIR/seed-playlist.m3u"
fi
if [ -d "$VLC_VENDOR/plugins" ]; then
    mkdir -p "$APPS_DIR/lib"
    cp -a "$VLC_VENDOR/lib/"*.so* "$APPS_DIR/lib/" 2>/dev/null || true
    rm -rf "$APPS_DIR/vlc"
    mkdir -p "$APPS_DIR/vlc"
    cp -a "$VLC_VENDOR/plugins" "$APPS_DIR/vlc/plugins"
    cp ../../resources/seed-playlist.m3u "$APPS_DIR/seed-playlist.m3u"
fi
ln -sfn "$APPS_DIR/$APP" "$DESKBAR_DIR/$APP"

echo
echo "installed: $APPS_DIR/$APP"
echo "seed list: $DATA_DIR/seed-playlist.m3u"
echo "cache dir: $HOME/config/settings/RTelevision"
echo "Deskbar  : Applications > $APP"
