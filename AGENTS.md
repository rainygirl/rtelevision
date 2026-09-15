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
| Haiku x86_64 / x86 | BeAPI | libVLC 3.0.23 | yes, unpacked from HaikuPorts |
| Haiku arm64 | BeAPI | FFmpeg 6.1.2 | yes, cross-built |

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
    FFmpegMediaPlayer   FFmpeg, where there is no libVLC (Haiku arm64)
    NullMediaPlayer     no playback; everything else still works
  HlsRelay            local relay for live streams slower than their bitrate
  RelayedMediaPlayer  MediaPlayer decorator that routes eligible HLS through it
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

#### Haiku arm64: FFmpeg

HaikuPorts publishes neither `vlc` nor `ffmpeg` for arm64, so FFmpeg is
cross-built on Linux or macOS with the Haiku cross toolchain:

```sh
CROSS_TOOLS=/path/to/cross-tools-arm64 sh scripts/build-ffmpeg-haiku.sh
```

That writes `third_party/ffmpeg-haiku-arm64/` (about 4.7 MB, LGPL build: no
`--enable-gpl`, no `--enable-nonfree`). Copy the project to the Haiku machine
and run `./install.sh`; it picks the FFmpeg backend up by itself.

## Build outputs and install locations

| Platform | Built | Installed | User data |
|---|---|---|---|
| macOS | `platforms/macos/build/<archs>/R Television.app` (about 193 MB, mostly VLC plugins) | `/Applications/R Television.app` | `~/Library/Application Support/RTelevision/` |
| Linux | `platforms/linux/build/` | `~/.local/lib/RTelevision/`, `~/.local/bin/r-television`, `.desktop` entry and icon under `~/.local/share` | `~/.local/share/RTelevision/` |
| Haiku | `platforms/haiku/build/` | `~/config/non-packaged/apps/RTelevision` plus bundled `.so` files (and `vlc/plugins` on x86_64), snapshot in `~/config/non-packaged/data/RTelevision/`, Deskbar link | `~/config/settings/RTelevision/` |

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
- Haiku's runtime loader does not search the binary's directory, so builds link
  with `-Wl,-rpath,'$ORIGIN'`, and `-lvlccore` is on the link line because an
  rpath does not carry over to a dependency's dependencies.
- Haiku without a sound device: `media_addon_server` may quit; VLC builds can
  run with `RTV_VLC_ARGS="--no-audio"`.
- Haiku `make uninstall` removes the binary, the Deskbar link and the snapshot
  folder, but not the bundled `.so` files, `vlc/plugins` or the cache in
  `~/config/settings/RTelevision`.
- The copied VLC plugin tree has a stale `plugins.dat` (copying changes mtimes),
  so libVLC rescans its plugins at start-up.
- Some streams (ABC among them) carry damaged transport streams; decoders report
  continuity errors and occasional corrupt frames whatever the player.

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
