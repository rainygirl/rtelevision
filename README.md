# <img src="resources/icon/icon_64.png" width="32" height="32" alt=""> R Television

A player for free TV streams from around the world: channels on the left,
picture on the right. The interface follows your system language (English,
Italian, Japanese or Korean).

English · [Italiano](README.it.md) · [日本語](README.ja.md) · [한국어](README.ko.md)

## What you need

| System | How to install | What else you need |
|---|---|---|
| macOS 11 or newer, Apple Silicon or Intel | download the app | nothing |
| Linux x86_64: Debian, Ubuntu, Linux Mint | build it with one command | an internet connection, and your password once |
| Linux x86_64: Fedora | build it with one command | the same, plus VLC from RPM Fusion |
| Haiku x86_64 | build it with one command | an internet connection |
| Haiku arm64 | build it with one command | FFmpeg prepared first (see [Haiku](#haiku)) |

On macOS, Haiku and Debian-based Linux, VLC does not need to be installed
anywhere: R Television brings its own. Other Linux distributions link the one
they ship, because the bundling step can only unpack `.deb` packages.

## Install

### macOS

![R Television on macOS](docs/screenshots/macos.png)

1. Download [`R-Television-1.0.0-macOS.dmg`](platforms/macos/dist/R-Television-1.0.0-macOS.dmg).
2. Open it and drag **R Television** onto **Applications**.
3. Open R Television from Applications.

The app is not signed with an Apple Developer ID, so macOS blocks it the first
time. Allow it once, in whichever way suits you:

- **macOS 15 and later:** after the warning, open **System Settings >
  Privacy & Security**, scroll down to *"R Television" was blocked* and click
  **Open Anyway**.
- **macOS 11 to 14:** Control-click the app in Applications, choose **Open**,
  then **Open** again.
- **Terminal, on any version** (this also fixes *"is damaged and can't be
  opened"*):

  ```sh
  /usr/bin/xattr -dr com.apple.quarantine "/Applications/R Television.app"
  ```

From then on it opens like any other app.

### Linux

![R Television on Linux](docs/screenshots/linux.png)

1. Download this project as a ZIP file and unpack it (or clone it with `git`).
2. Open a terminal in the unpacked folder and run:

   ```sh
   cd platforms/linux
   ./install.sh
   ```

   The script installs whatever is missing among the compiler and the GTK and
   curl development packages (it may ask for your password), then builds
   R Television and installs it into your home folder. It uses `apt-get` or
   `dnf`, whichever the system has.

   On Fedora, VLC lives in RPM Fusion rather than the stock repositories. If
   `vlc-devel` cannot be found, enable it and run the script again:

   ```sh
   sudo dnf install -y \
     "https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm"
   ```
3. Start **R Television** from the applications menu, or run `r-television`.

Tested on Linux Mint 20.3, Ubuntu 20.04 and Fedora 41.

On a Wayland session the app asks GDK for its X11 backend and runs through
XWayland, because libVLC 3 can only embed video into an X11 window. Set
`GDK_BACKEND` yourself to override that; video then opens in a window of its
own.

### Haiku

![R Television on Haiku](docs/screenshots/haiku.png)

1. Download this project as a ZIP file and unpack it.
2. Open Terminal in the unpacked folder and run:

   ```sh
   cd platforms/haiku
   ./install.sh
   ```

   Missing packages (`gcc`, `haiku_devel`) are installed with `pkgman`; nothing
   else on the system is touched.
3. Start **RTelevision** from **Deskbar > Applications**.

On **arm64** there is no VLC or FFmpeg package for Haiku, so FFmpeg has to be
built on another computer before step 2. The steps are in
[AGENTS.md](AGENTS.md#haiku-arm64-ffmpeg).

## Uninstall

macOS:

```sh
rm -rf "/Applications/R Television.app"
rm -rf ~/Library/Application\ Support/RTelevision
```

Linux (from the project folder):

```sh
cd platforms/linux && make uninstall
```

Haiku:

```sh
rm -rf ~/config/non-packaged/apps/RTelevision ~/config/settings/deskbar/menu/Applications/RTelevision
rm -rf ~/config/non-packaged/data/RTelevision ~/config/settings/RTelevision
```

The second macOS line and the Haiku `settings` folder hold the channel list
cache and your favorites.

## License

MIT, see [LICENSE](LICENSE). The bundled VLC and FFmpeg libraries keep their
own licences, listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
The channel list is taken from [iptv-org/iptv](https://github.com/iptv-org/iptv)
and follows the iptv-org licence.

## AI disclosure

This program was written with Claude.
