# Third-party components

R Television's own code is MIT (see [LICENSE](LICENSE)). It bundles or links
the components below, and several of them ask for a notice in return. This file
is that notice; the full licence texts it refers to are in [licenses/](licenses).

## libVLC 3.0.23 - macOS, Linux, Haiku x86_64

The playback backend on every platform that has a build of it.

- **Where it comes from.** macOS: the official universal disk image from
  <https://www.videolan.org/vlc/>, unpacked into `third_party/vlc-macos/`, which
  is committed to this repository. Linux: the distribution's own `libvlc5`,
  `libvlccore9` and plugin packages, downloaded and unpacked by
  `platforms/linux/fetch-libvlc.sh`. Haiku: the HaikuPorts `.hpkg` files,
  extracted without being installed by `platforms/haiku/fetch-libvlc.sh`.
- **Licence.** `libvlc` and `libvlccore` are **LGPL-2.1-or-later**. The plugin
  tree that ships with an official VLC build also contains GPL-2.0-or-later
  modules, so the bundled media stack as a whole should be treated as
  **GPL-2.0-or-later**. Full texts: [licenses/LGPL-2.1.txt](licenses/LGPL-2.1.txt),
  [licenses/GPL-2.0.txt](licenses/GPL-2.0.txt).
- **Modifications.** None. The libraries and plugins are copied verbatim and
  loaded dynamically, so they can be swapped for your own build: replace the
  files under the application's `lib/` and `plugins/` directories, or point
  `VLC_PLUGIN_PATH` elsewhere. Nothing is statically linked.
- **Source.** VideoLAN publishes the matching sources at
  <https://www.videolan.org/vlc/download-sources.html>; the Linux and Haiku
  builds come from the distribution packages, whose sources the distribution
  carries (`apt-get source vlc`, HaikuPorts' recipes).

## FFmpeg 6.1.2 - Haiku arm64

The playback backend where no libVLC exists, decoded through
`shared/core/FFmpegMediaPlayer.cpp`.

- **Where it comes from.** Cross-built by `scripts/build-ffmpeg-haiku.sh` from
  the unmodified release tarball at
  <https://ffmpeg.org/releases/ffmpeg-6.1.2.tar.xz>, into
  `third_party/ffmpeg-haiku-arm64/`.
- **Licence.** **LGPL-2.1-or-later**. The build passes neither `--enable-gpl`
  nor `--enable-nonfree`, so no GPL-only component (postproc, x264, and the
  rest) is compiled in. The exact configure line is in the build script and is
  the only thing needed to reproduce the binaries. Full text:
  [licenses/LGPL-2.1.txt](licenses/LGPL-2.1.txt).
- **Modifications.** None; only the configure flags select a smaller feature
  set. The result is five shared objects, linked dynamically and replaceable.
- **OpenSSL.** The Haiku build uses `--enable-openssl` for HTTPS and therefore
  links Haiku's own `libssl.so.3` / `libcrypto.so.3`. OpenSSL 3 is Apache-2.0
  and is not shipped by this project.

## GMA500 hardware H.264 module - Haiku x86

`libmsvdx_plugin.so`, a libVLC decoder module built from
`platforms/haiku/msvdx/` and installed into `vlc/plugins/codec`. It links
dynamically against libVLC's `libvlccore`, like every other plugin there.

- **Chromium's H.264 parser and decoder** (`msvdx/chromium/`): unmodified
  copies of `media/video/h264_*` and `media/gpu/h264_*` from Chromium
  114.0.5735.199. **BSD-3-Clause**, full text in
  [platforms/haiku/msvdx/chromium/LICENSE](platforms/haiku/msvdx/chromium/LICENSE).
- **psb_video** (`msvdx/hw/psb/`, `msvdx/hw/hwdefs/`): the H.264 command
  builder of Intel's GMA500 VA-API driver, from its first open release (2010).
  **MIT**; the notice is at the top of each file. One line changed in
  `psb_H264.c`, marked `HAIKU`.
- **The userland driver and glue** (`msvdx/hw/msvdx.c`, `msvdx_decode.c`,
  `msvdx/*.c*`, `msvdx/shim/`): this project's own, MIT.
- **Firmware.** The decoder needs Intel's `msvdx_fw.bin`, whose licence does
  not allow redistribution. It is **not** in this repository or in any
  package; the module looks for it in
  `~/config/non-packaged/data/firmware/` and stays out of the way when it is
  not there.

## libcurl - macOS, Linux

HTTP client for the playlist (`shared/core/CurlHttpClient.cpp`). Linked against
the system's libcurl; no copy is shipped. libcurl is under the curl licence, an
MIT/X-style permissive licence: <https://curl.se/docs/copyright.html>.

## Platform frameworks

Linked, never shipped: Cocoa and the macOS SDK (Apple's own terms); GTK3 and
GLib on Linux, both LGPL-2.1-or-later; the Haiku Be API (`libbe`), MIT, with
the playlist fetched through Haiku's network services on that platform.

## Channel list - iptv-org

`resources/seed-playlist.m3u` is a snapshot of
<https://iptv-org.github.io/iptv/index.m3u>, and the app refreshes from that
same URL. The iptv-org repository is released into the public domain under The
Unlicense: <https://github.com/iptv-org/iptv>.

The list holds addresses of publicly announced streams. The streams themselves
belong to the broadcasters that operate them; this project neither hosts nor
redistributes their content.

## If you redistribute a build

Keep this file and the `licenses/` directory alongside the binaries, and be
ready to hand over the corresponding sources for libVLC or FFmpeg - which is
what the links above are for. R Television's own sources stay MIT either way.
