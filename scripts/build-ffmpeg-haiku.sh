#!/bin/sh
# Cross-builds a minimal FFmpeg for Haiku into third_party/ffmpeg-haiku-<arch>.
#
# Why: HaikuPorts publishes no vlc and no ffmpeg for arm64, so there is nothing
# to vendor there. FFmpeg cross-compiles cleanly for Haiku, and the app has an
# FFmpeg media backend (shared/core/FFmpegMediaPlayer.cpp) for exactly this case.
#
# Needs a Haiku cross toolchain. Point CROSS_TOOLS at it, e.g. the one a Haiku
# build produces in generated/cross-tools-arm64:
#
#   CROSS_TOOLS=/path/to/cross-tools-arm64 sh scripts/build-ffmpeg-haiku.sh
#
# Run it on Linux/macOS (not on Haiku itself - the arm64 image has no make).
set -eu

ARCH="${ARCH:-arm64}"
FFMPEG_VERSION="${FFMPEG_VERSION:-6.1.2}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/third_party/ffmpeg-haiku-$ARCH"
WORK="${WORK:-/tmp/ffmpeg-haiku-$ARCH}"

: "${CROSS_TOOLS:?set CROSS_TOOLS to a Haiku cross toolchain directory}"
PREFIX_TRIPLE="${PREFIX_TRIPLE:-aarch64-unknown-haiku-}"
[ -x "$CROSS_TOOLS/bin/${PREFIX_TRIPLE}gcc" ] || {
    echo "no ${PREFIX_TRIPLE}gcc under $CROSS_TOOLS/bin" >&2
    exit 1
}

mkdir -p "$WORK"
cd "$WORK"
if [ ! -d "ffmpeg-$FFMPEG_VERSION" ]; then
    [ -f "ffmpeg-$FFMPEG_VERSION.tar.xz" ] || \
        curl -fL -o "ffmpeg-$FFMPEG_VERSION.tar.xz" \
             "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"
    tar xf "ffmpeg-$FFMPEG_VERSION.tar.xz"
fi
cd "ffmpeg-$FFMPEG_VERSION"

# Only what a TV stream player actually opens: HTTP(S) transport, HLS/TS/MP4
# containers, and the handful of codecs those streams use. Keeps the result
# around 5 MB instead of 80.
./configure \
    --prefix="$WORK/install" \
    --enable-cross-compile --target-os=haiku --arch=aarch64 \
    --cross-prefix="$CROSS_TOOLS/bin/$PREFIX_TRIPLE" \
    --sysroot="$CROSS_TOOLS/sysroot" \
    --disable-programs --disable-doc --disable-debug --disable-autodetect \
    --disable-avdevice --disable-avfilter --disable-postproc \
    --enable-shared --disable-static --enable-small \
    --disable-everything \
    --enable-protocol=file,http,https,tcp,tls,crypto,httpproxy \
    --enable-demuxer=hls,mpegts,mov,matroska,flv,aac,mp3,h264,hevc,mpegvideo \
    --enable-decoder=h264,hevc,mpeg2video,mpeg4,aac,aac_latm,mp3,ac3,eac3 \
    --enable-parser=h264,hevc,aac,aac_latm,mpegaudio,ac3,mpeg4video,mpegvideo \
    --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc,extract_extradata \
    --enable-openssl

make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
make install

rm -rf "$DEST"
mkdir -p "$DEST"
cp -a "$WORK/install/include" "$DEST/include"
cp -a "$WORK/install/lib" "$DEST/lib"

echo
echo "built FFmpeg $FFMPEG_VERSION for Haiku/$ARCH -> $DEST"
du -sh "$DEST"
