# AGENTS.md

Notes for anyone - person or coding agent - working on the code. End-user
installation instructions live in `README.md` (and its Italian, Japanese and
Korean translations); keep those free of developer detail. Everything else
belongs here.

## What the project is

R Television plays free TV streams from around the world (the
[iptv-org](https://github.com/iptv-org/iptv) playlist): a channel tree on the
left, video on the right.

```
+---------------------------+----------------------------------+
| search                    |                                  |
| [ category | country ]    |              video               |
| v General            2733 |                                  |
|   v News              985 |                                  |
|     KR Arirang TV         +----------------------------------+
|   v Sports            429 | ❚❚  ■  ★   Arirang TV   🔊──  ⤢  |
+---------------------------+----------------------------------+
 11,035 channels · offline cache · 2026-09-12 10:06
```

One portable C++17 core, one native front end per platform:

| Platform | Front end | Media backend | Bundled |
|---|---|---|---|
| macOS arm64 / x86_64 | Cocoa (Objective-C++) | libVLC 3.0.23 | yes, 343 plugins |
| Linux x86_64 | GTK3 | libVLC 3.0.9 from the distribution | yes, 353 plugins |
| Haiku x86_64 | BeAPI | libVLC 3.0.23 | yes, pruned from HaikuPorts |
| Haiku x86 (x86_gcc2 hybrid) | BeAPI | libVLC 3.0.23 | yes, unpacked from HaikuPorts |
| Haiku arm64 | BeAPI | FFmpeg 6.1.2 | yes, cross-built |
| Mac OS X 10.4 PowerPC | Cocoa (Objective-C) | libVLC 0.9.10 | yes, from the PowerPC VLC |

VLC never has to be installed by the user; every build carries its own media
library.

## Repository layout

```
shared/core/          portable C++17: no UI, no platform SDK calls
  Channel.h           the channel model
  M3UParser           #EXTINF and #EXTVLCOPT parsing
  PlaylistStore       downloading and the offline cache policy
  ChannelIndex        category tree, country grouping, search
  Favorites           favorites, persisted
  AppController       ties store, index, favorites and player together
  MediaPlayer         backend interface, VideoFrameSink, AudioSink
    VlcMediaPlayer      libVLC
    VlcLegacyMediaPlayer  libVLC 0.9, for Mac OS X 10.4 PowerPC
    FFmpegMediaPlayer   FFmpeg, where there is no libVLC (Haiku arm64)
    NullMediaPlayer     no playback; everything else still works
  HlsRelay            local relay for live streams slower than their bitrate,
                      and HlsTuner, which serves channels as plain MPEG-TS
  RelayedMediaPlayer  MediaPlayer decorator that routes eligible HLS through it
  Aes128              AES-128-CBC decryption, for #EXT-X-KEY segments
  HttpClient          HTTP interface
    CurlHttpClient      libcurl (macOS, Linux)
    HaikuHttpClient     Haiku network services
  Paths, Country      data directories, flags, country names
  Strings             the interface text in four languages
shared/tools/         rtv-cli, the headless driver
shared/common.mk      core source lists and flags for every platform Makefile
platforms/macos/      Cocoa front end, Makefile, install.sh, fetch-libvlc.sh
platforms/linux/      GTK3 front end, Makefile, install.sh, fetch-libvlc.sh
platforms/haiku/      BeAPI front end (BSoundPlayer audio), Makefile, install.sh
platforms/macos-ppc/  Mac OS X 10.4 PowerPC: Cocoa front end and UiBridge, the C
                      boundary that lets Apple GCC 4.0.1 build the UI while
                      GCC 4.7 builds the C++11 core
resources/            seed playlist, app icon and the script that draws it
scripts/              build-ffmpeg-haiku.sh
third_party/          vendored libVLC / FFmpeg trees (fetched or built)
docs/PORTING.md       porting history and measurements (Korean)
licenses/             full LGPL-2.1 and GPL-2.0 texts
```

## Architecture rules

- `shared/` has no UI code, no `#import`, no platform SDK calls. POSIX (files,
  sockets, threads) is fine; it is common to all three targets.
- A platform picks its media and HTTP backends by naming one source file each
  in its Makefile (`CORE_SRC_VLC` / `CORE_SRC_FFMPEG` / `CORE_SRC_NULL`,
  `CORE_SRC_HTTP_CURL` / `CORE_SRC_HTTP_HAIKU` in `shared/common.mk`). No
  `#ifdef` in the core for backend selection.
- Core callbacks (`MediaPlayer::StateCallback`, `AppController::RefreshCallback`,
  relay progress) arrive on worker threads. Front ends hop to their UI thread
  first: `dispatch_async(main_queue)` on macOS, `g_idle_add` on Linux,
  `BMessenger::SendMessage` on Haiku.
- All Makefiles build with `-MMD -MP` and include the `.d` files. Without it a
  changed header silently leaves stale objects with the wrong struct layout.

| Boundary | File | macOS | Linux | Haiku |
|---|---|---|---|---|
| Data directory | `core/Paths.cpp` | `~/Library/Application Support/RTelevision` | `$XDG_DATA_HOME/RTelevision` | `~/config/settings/RTelevision` |
| Video output | `core/MediaPlayer.h` | `attachVideoView(NSView*)` | `attachVideoView(XID)` | `attachVideoSink(VideoFrameSink*)` |
| HTTP | `core/HttpClient.h` | `CurlHttpClient` | `CurlHttpClient` | `HaikuHttpClient` |
| Display language | `core/Strings.h` | `[NSLocale preferredLanguages]` | `LANG` / `LC_ALL` | `BLocaleRoster` |

## Channel list and offline cache

The list comes from `https://iptv-org.github.io/iptv/index.m3u` (override with
`RTV_PLAYLIST_URL`) and is kept in three places so the app survives that URL
going away:

1. the cache, `playlist.m3u`
2. the last copy known to be good, `playlist.bak.m3u`
3. a snapshot shipped inside the app, `resources/seed-playlist.m3u`

The window opens from the cache and refreshes in the background, so it works
with no network. Downloads are conditional on `ETag` and `Last-Modified`. A
reply that is not an M3U, or that parses to fewer than 16 channels, is thrown
away rather than written over a working cache. Every write goes to a temporary
file and is moved into place with `rename()`.

## HLS relay

`HlsRelay` serves live single-rendition HLS streams to the player from
127.0.0.1. It fetches each segment over several parallel HTTP Range requests
(pieces of about 768 KB, at least one per connection, stragglers raced by a
duplicate on an idle connection) and lists only segments it already holds. The
player's first playlist request is held until 20 s of video is ready (at most
40 s), unless the server delivers at twice real time or more. Adaptive
(multi-variant) streams and VOD go to the player untouched.

- Backends see the relay through the channel option `rtv-relay`: VLC adds
  `:adaptive-livedelay`, FFmpeg sets `live_start_index=0`, a 60 s
  `rw_timeout` and `http_persistent=0`.
- Knobs: `RTV_NO_HLS_RELAY=1`, `RTV_RELAY_CONNECTIONS` (default 4),
  `RTV_RELAY_BUFFER` (seconds, default 20). `RTV_VERBOSE` logs every segment
  and every request the relay serves.
- `rtv-cli relay <url> [seconds]` runs the relay alone and prints its address.

### Tuner: serving players that have no HLS at all

`HlsTuner` wraps the relay for players that cannot read an HLS playlist: it
writes the segments out back to back as one continuous MPEG-TS body, so the
player only ever sees a plain stream over HTTP.

```sh
rtv-cli serve [port]                      # default 8090, every interface
rtv-cli playlist http://host:8090 [sd]    # an M3U pointing at that tuner
```

```
GET /tune?u=<percent-encoded channel URL>   ->  video/mp2t
```

One player at a time; a new request ends the session before it. Where a channel
offers several renditions the smallest is taken, which is what the machines
needing this can decode. Unlike the ordinary relay path, the tuner decrypts
AES-128 segments itself (`Aes128.cpp`): the player never sees the key tag, so it
cannot do it. Fragmented MP4 streams are refused - concatenating them as MPEG-TS
would mean nothing - and so are channels whose playlist cannot be read.

`playlist ... sd` leaves out the channels whose name announces HD, which the
machines this exists for cannot decode anyway. The resolution is only a hint in
the channel name, so it is not exact.

This is what makes R Television usable on a PowerPC Mac: see
`docs/PORTING.md`.

## FFmpeg backend pacing

Audio is the master clock. Video packets wait in a queue, still compressed,
and are decoded only when within 0.5 s of the clock; audio is decoded and
queued the moment it is read. Waiting for a video frame inline would block the
only thread that reads the audio that moves the clock; on a stream muxed with
video ahead of audio (ABC, by 0.5-1.2 s) playback crawled. Gaps in the audio
timestamps are filled with silence so the clock keeps stream time. Details and
measurements are in `docs/PORTING.md`.

## Localization

`shared/core/Strings.cpp` holds one table row per `Str` id with English,
Italian, Japanese and Korean text; the row order must match the enum (checked by
an assertion). To add a string: add the id to `Strings.h`, add the row in the
same position, then use `str(Str::Id)` / `format()` in the front ends. Front ends
call `setLanguage()` once at start-up; unsupported languages fall back to
English.

## Building

### macOS

Requires macOS 11+ and the Xcode command line tools. `make` fetches the libVLC
SDK (`fetch-libvlc.sh`, from VideoLAN's universal disk image) into
`third_party/vlc-macos/`.

```sh
cd platforms/macos
make                         # this machine's architecture
make ARCHS=x86_64            # Intel
make ARCHS="arm64 x86_64"    # universal
make run                     # build and launch
make cli                     # rtv-cli, no libVLC needed
make install                 # copy to /Applications (INSTALL_DIR to change)
make dist                    # universal dist/R-Television-<version>-macOS.dmg
```

The bundle is ad-hoc signed; there is no Developer ID, so it cannot be
notarized. The READMEs explain how users allow the first launch.

### Linux

Debian/Ubuntu/Mint, x86_64. `install.sh` installs any missing
`build-essential`, `libgtk-3-dev`, `libcurl4-openssl-dev` with `sudo apt-get`,
vendors libVLC with `apt-get download` + `dpkg -x` (no root; the dependency walk
stops at packages already installed, about 15 MB), builds, and installs into
`~/.local`.

```sh
cd platforms/linux
make                         # build only
make run
make cli                     # no GTK needed
make VLC_SYSTEM=1            # link against the distribution's libvlc instead
PREFIX=/usr/local sudo -E ./install.sh
```

Without root, headers can come from an extracted "devroot" via
`PKG_CONFIG_PATH`/`PKG_CONFIG_SYSROOT_DIR`.

### Mac OS X 10.4, PowerPC

The core is written to C++11 and only compiled as C++17, so this platform needs
GCC 4.7 - the last GCC written in C, which Tiger's own GCC 4.0.1 can therefore
build - rather than a modern cross toolchain. `shared/common.mk` takes
`CORE_CXXSTD` so only this platform drops to `c++11`.

libVLC 0.9.10 is the last that runs on 10.4 and has no HLS demuxer, so this
platform sets `RelaySettings::plainTransportStream`: the relay hands the backend
one continuous MPEG-TS body and decrypts AES-128 itself, because a player fed a
plain stream never sees the key tag.

The picture comes back through `VideoFrameSink`, as on Haiku: libVLC's OpenGL
output checks `CGDisplayUsesOpenGLAcceleration` and refuses to start on a Mac
without Quartz Extreme, so the backend uses VLC's `vmem` output and the Cocoa
view draws the buffer. The list has an SD-only switch beside the search field,
on by default wherever `hw.cpusubtype` is not the G5: a G4 cannot decode 720p.

```sh
cd platforms/macos-ppc
make toolchain          # what to build and where it goes
make cli                # rtv-cli, the first thing to bring up
make                    # the application bundle
```

### Haiku

`install.sh` installs any missing `gcc` / `haiku_devel` with `pkgman`, finds or
fetches the backend, builds, and installs into `~/config/non-packaged`. `make`
is optional: without it, `install.sh` compiles the sources directly (minimal
arm64 images have no make).

```sh
cd platforms/haiku
make                         # build only
make backend                 # report which backends were found
make cli                     # no BeAPI UI needed
./install.sh --deps          # stop after the packages
./install.sh --build         # stop after the build
```

On x86_64 `fetch-libvlc.sh` asks `pkgman` what it would install (answering no)
and unpacks the `.hpkg` files with `package extract` instead.

#### The .hpkg for pkgman.rainygirl.com

The published packages are built by `recipes/rtelevision.recipe` in the
`pkgman-repo` project, the same harness as the other R\* applications: it
writes the `.PackageInfo`, runs `package create`, and `scripts/make-repo.sh`
and `scripts/deploy.sh` index and publish the result. This repository holds
only what the recipe starts from - the ordinary `make` build, and the two
cross-build scripts - so there is one description of the package rather than
two that can drift apart.

Each architecture starts from a different place, because what HaikuPorts
publishes for it differs:

| Repository | Package | Built | Media library |
|---|---|---|---|
| `x86_gcc2` | `rtelevision_x86`, requires `haiku_x86` | on the machine, `setarch x86 make` | bundled, from `third_party/vlc-haiku-BePC` |
| `x86_64` | `rtelevision`, requires `haiku` | cross, `platforms/haiku/cross-x86_64.sh` | bundled, pruned from the HaikuPorts `vlc` and its dependencies |
| `arm64` | `rtelevision`, requires `haiku` and `lib:libssl` | cross, `pkgman-repo` `scripts/cross-arm64.sh` | bundled, from `third_party/ffmpeg-haiku-arm64` |

The `_x86` name on the hybrid is the HaikuPorts convention for a package built
against the secondary architecture, the same as `vlc_x86`; the core is C++17,
which the gcc2 primary compiler cannot build. Every architecture carries its
own media library, and the layout is the one `make install` produces - `lib/` beside the binary, because the runtime
loader looks there and the VLC plugins, which carry no rpath, find
`libdvbpsi` and the rest the same way.

```
apps/RTelevision/{RTelevision, lib/, vlc/plugins/, seed-playlist.m3u}
data/deskbar/menu/Applications/R Television -> ../../../../apps/...
documentation/packages/<package name>/{LICENSE, THIRD-PARTY-NOTICES.md, READMEs}
.PackageInfo
```

```sh
scripts/build.sh rtelevision              # on a Haiku machine of that architecture
scripts/build.sh --arch x86_64 rtelevision  # a cross-built dist/ only needs packaging
scripts/make-repo.sh x86_gcc2             # repo, repo.info, repo.sha256, packages.txt
scripts/deploy.sh x86_gcc2                # from the workstation, to the web server
```

#### Haiku x86_64: cross-compiling with clang

No Haiku x86_64 machine has to exist to publish for it. `cross-x86_64.sh`
builds `dist/x86_64/` on the workstation with clang, whose
`x86_64-unknown-haiku` target means no cross toolchain has to be built; only
the C runtime startup files come from Haiku's gcc, through `-B`.

```sh
RTV_HAIKU_SYSROOT=/Volumes/HaikuX64/sysroot sh platforms/haiku/cross-x86_64.sh
```

HaikuPorts does publish `vlc` for x86_64, so this could have been a 0.8 MB
package that requires `lib:libvlc`. It is not, because that pulls in qt5 and
the GStreamer stack: measured, `pkgman install rtelevision` then downloads
several hundred megabytes, and on a live image it fails outright with "no
space left on device". The script bundles a pruned libVLC instead - 41
libraries and 293 plugins, about 56 MB unpacked and 20 MB packaged. It reads
every `NEEDED` entry of the libraries and plugins, copies in whatever the
sysroot can supply that `base-libs.txt` does not already list as part of the
base system, repeats until nothing new turns up, and then deletes the plugins
whose dependencies still cannot be met. That last step is what drops the Qt
interface rather than dragging Qt in.

The sysroot is these packages unpacked on top of each other into
`<sysroot>/boot/system` with Haiku's own `package extract`: `haiku` and
`haiku_devel` from the Haiku repository, and from HaikuPorts `gcc`,
`gcc_syslibs`, `gcc_syslibs_devel`, `vlc`, `vlc_devel` plus everything
`vlc`'s `requires: lib:...` names except qt5 - 43 packages, `a52dec` through
`zlib`, of which `ffmpeg` is the big one at 9.5 MB. `<sysroot>/base-libs.txt`
holds one soname per line for the libraries `haiku` and `gcc_syslibs` already
provide.

It has to sit on a **case-sensitive** filesystem: Haiku ships both
`<string.h>` and the BString header `<String.h>`, and on a case-insensitive
volume - which is what macOS formats by default - the first `#include
<string.h>` picks up BString and nothing compiles. `hdiutil create -fs
"Case-sensitive APFS"` makes a volume that works.

#### Haiku arm64: FFmpeg

HaikuPorts publishes neither `vlc` nor `ffmpeg` for arm64, so FFmpeg is
cross-built on Linux or macOS with the Haiku cross toolchain:

```sh
CROSS_TOOLS=/path/to/cross-tools-arm64 sh scripts/build-ffmpeg-haiku.sh
```

That writes `third_party/ffmpeg-haiku-arm64/` (about 4.7 MB, LGPL build: no
`--enable-gpl`, no `--enable-nonfree`). Copy the project to the Haiku machine
and run `./install.sh`; it picks the FFmpeg backend up by itself.

## GMA500 hardware H.264 on the x86 build (2026-09-30)

`platforms/haiku/msvdx/` builds `libmsvdx_plugin.so`, a libVLC decoder
module that decodes H.264 on the VAIO P's GMA500 video decoder instead of in
libavcodec. The Haiku Makefile builds it and copies it into
`vlc/plugins/codec` when `ARCH` is `BePC` and the vendored VLC has plugin
headers. Its README has the layout, the licences and the conditions under
which it steps aside for libavcodec.

Measured with a libVLC harness that plays into a 640x360 RV32 buffer the way
`VideoView` does: on three test streams (Baseline, Main CABAC at 176x144 and
at 848x480) every delivered frame hashes identical to libavcodec's with the
loop filter on, and the 480p stream costs 17% of the machine against 28%.

Two things learned on the way, both now fixed in the shared `hw/msvdx.c`:

- Decoder buffers must not be physically contiguous allocations. An hour
  after boot no 1 MB contiguous run is left even with 1.5 GB free, and a 720p
  surface is 1.4 MB. The decoder has its own MMU; buffers are now mapped
  page by page.
- Every decoder area must be `B_SHARED_AREA`. libVLC forks for every HTTP
  stream (posix_spawn of the libproxy helper), and fork makes a non-shared
  area copy-on-write: the register page turns read-only and the next write
  to it panics the kernel in `X86VMTranslationMapPAE::Map`. It did, twice.
  `share_area()` in `hw/msvdx.c` clones each area once, which sets the flag.

Measured in the app on NHK General (960x540 HLS): decoded by `msvdx`, 25
fps, 22% of the machine against 53% with libavcodec over the same 60 s.

## Build outputs and install locations

| Platform | Built | Installed | User data |
|---|---|---|---|
| macOS | `platforms/macos/build/<archs>/R Television.app` (about 193 MB, mostly VLC plugins) | `/Applications/R Television.app` | `~/Library/Application Support/RTelevision/` |
| Linux | `platforms/linux/build/` | `~/.local/lib/RTelevision/`, `~/.local/bin/r-television`, `.desktop` entry and icon under `~/.local/share` | `~/.local/share/RTelevision/` |
| Mac OS X 10.4 PowerPC | `platforms/macos-ppc/build/` | `/Applications/R Television.app` (about 45 MB) | `~/Library/Application Support/RTelevision/` |
| Haiku | `platforms/haiku/build/` | from `pkgman`: `/boot/system/apps/RTelevision/`. From source: `~/config/non-packaged/apps/RTelevision/` holding the binary, `lib/` with the bundled `.so` files and `vlc/plugins`; snapshot in `~/config/non-packaged/data/RTelevision/`, Deskbar link | `~/config/settings/RTelevision/` |

Every bundle also carries `LICENSE`, `THIRD-PARTY-NOTICES.md` and `licenses/`.
`~/config/apps` on Haiku is read-only packagefs, hence `non-packaged`.

## Command line tool

The core without a window; the first thing to bring up on a new platform.

```sh
./build/arm64/rtv-cli where          # where the cache lives
./build/arm64/rtv-cli refresh        # download and update the cache
./build/arm64/rtv-cli list           # the category tree
./build/arm64/rtv-cli list country   # grouped by country
./build/arm64/rtv-cli search KBS
./build/arm64/rtv-cli relay <url> 60 # run the HLS relay alone
```

## Environment variables

| Variable | Effect |
|---|---|
| `RTV_PLAYLIST_URL` | playlist source instead of iptv-org |
| `RTV_VERBOSE` | verbose libVLC and relay logging |
| `RTV_VLC_ARGS` | extra libVLC arguments, e.g. `--vout=xcb_x11`, `--no-audio` |
| `RTV_AUTOPLAY` | macOS/Linux: filter by this text and play the first match at start-up |
| `RTV_NO_HLS_RELAY` | play every stream directly |
| `RTV_RELAY_CONNECTIONS` | parallel connections per segment (default 4) |
| `RTV_RELAY_BUFFER` | seconds of video held back before starting (default 20) |

## Known issues

- Ubuntu 20.04's libVLC 3.0.9 latches onto the subtitle rendition of some HLS
  master playlists and never fetches video; the same channels play with 3.0.23.
- On Linux the default XVideo overlay is not captured by screenshots; use
  `RTV_VLC_ARGS="--vout=xcb_x11"` when taking them.
- Haiku's runtime loader does not search the binary's directory, but it does
  search `<binary's directory>/lib`. The bundled libraries live there: the app
  finds libvlc through `-Wl,-rpath,'$ORIGIN/lib'`, and the VLC plugins, which
  have no rpath, find their own dependencies (libdvbpsi for the TS demuxer)
  through the loader's default path. Libraries placed next to the binary were
  invisible to the plugins, so TS-based HLS never played. `-lvlccore` is on the
  link line because an rpath does not carry over to a dependency's dependencies.
- Haiku without a sound device: `media_addon_server` may quit; VLC builds can
  run with `RTV_VLC_ARGS="--no-audio"`.
- Haiku's `nsdispatch()` crashes after a thread is cancelled inside a name
  lookup, which libVLC does when a stream is stopped while still connecting.
  `platforms/haiku/ResolverGuard.cpp` interposes the resolver functions and
  runs them with cancellation disabled.
- Haiku's `socket()` ignores `SOCK_NONBLOCK` for the socket itself (fcntl
  reports the flag, `recv()` still blocks), which left libVLC's interruptible
  read loops stuck and `libvlc_media_player_stop()` waiting forever after a
  stream had played. `platforms/haiku/SocketGuard.cpp` interposes `socket()`
  and `accept4()` and sets the flag with `fcntl()` instead.
- Haiku's `BHttpRequest` does not follow a 307 even with `SetFollowLocation`,
  so `HaikuHttpClient` follows redirects itself and reports the final URL. The
  relay passes that URL to the backend when it declines a channel: libVLC on
  Haiku ends its input silently on such a redirect. `VlcMediaPlayer` also runs
  a watchdog that turns an input that ended without an event into an error.
- Haiku `make uninstall` removes the app directory (binary, bundled `.so`
  files, `vlc/plugins`), the Deskbar link and the snapshot folder, but not the
  cache in `~/config/settings/RTelevision`.
- The copied VLC plugin tree has a stale `plugins.dat` (copying changes mtimes),
  so libVLC rescans its plugins at start-up.
- Some streams (ABC among them) carry damaged transport streams; decoders report
  continuity errors and occasional corrupt frames whatever the player.
- A Command Line Tools SDK newer than the installed linker makes every link fail
  with `tapi error: malformed file ... unknown architecture arm64e.x1`. Build
  against Xcode's own SDK instead:
  `make cli CXX="xcrun clang++ -isysroot $(xcrun --sdk macosx --show-sdk-path)"`.

## Documentation rules

- `README.md`, `README.it.md`, `README.ja.md`, `README.ko.md` are for end users
  only - download, install, start, uninstall, troubleshooting - and must say the
  same thing in all four languages. The per-platform READMEs only point to the
  matching section.
- Developer information goes in this file, in English.
- Plain ASCII punctuation in prose; no em dashes.

## Licensing

The project is MIT (`LICENSE`). Bundled libVLC is LGPL-2.1+ with GPL-2.0+
plugins, FFmpeg is built LGPL-2.1+; their notices are in
`THIRD-PARTY-NOTICES.md` and must travel with every distributed build (the
Makefiles and install scripts copy them). The channel list from iptv-org is
public domain (The Unlicense).
