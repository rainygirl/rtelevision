#!/bin/sh
# Builds and installs R Television on Linux.
#
#   ./install.sh            build and install into ~/.local
#   ./install.sh --deps     only fetch the build dependencies
#   PREFIX=/usr/local sudo -E ./install.sh    system-wide
#
# Debian and Ubuntu get a vendored libVLC: fetch-libvlc.sh unpacks .deb files,
# so the app runs without a system VLC install. Other distributions link the
# distribution's libVLC instead, because that script has no way to fetch
# packages for them.
set -eu

cd "$(dirname "$0")"

# ---------------------------------------------------------------- distribution
if command -v apt-get >/dev/null 2>&1; then
    family=debian
elif command -v dnf >/dev/null 2>&1; then
    family=fedora
else
    family=unknown
fi

case "$family" in
debian)
    pkg_gtk=libgtk-3-dev
    pkg_curl=libcurl4-openssl-dev
    pkg_cc=build-essential
    pkg_vlc=                       # vendored instead, see fetch-libvlc.sh
    installer="apt-get install -y"
    ;;
fedora)
    pkg_gtk=gtk3-devel
    pkg_curl=libcurl-devel
    pkg_cc="gcc-c++ make"
    pkg_vlc=vlc-devel              # RPM Fusion (free), not in stock Fedora
    installer="dnf install -y"
    ;;
*)
    pkg_gtk=; pkg_curl=; pkg_cc=; pkg_vlc=; installer=
    ;;
esac

# Only Debian can vendor libVLC; everywhere else we link the system one.
if [ "$family" = debian ]; then
    vlc_system=0
else
    vlc_system=1
fi

# ---------------------------------------------------------------- dependencies
need=""
pkg-config --exists gtk+-3.0 2>/dev/null || need="$need $pkg_gtk"
pkg-config --exists libcurl  2>/dev/null || need="$need $pkg_curl"
command -v g++ >/dev/null 2>&1           || need="$need $pkg_cc"
[ "$vlc_system" = 1 ] && { pkg-config --exists libvlc 2>/dev/null || need="$need $pkg_vlc"; }
need=$(echo $need)                 # collapse the padding added above

if [ -n "$need" ]; then
    if [ -z "$installer" ]; then
        echo "build dependencies are missing." >&2
        echo "This script can only install them with apt-get or dnf." >&2
        echo "Install these by hand and run it again:" >&2
        echo "  a C++ compiler, make, and the GTK3, libcurl and libVLC development headers" >&2
        exit 1
    fi
    echo "build dependencies missing: $need"
    if [ "$(id -u)" = 0 ]; then
        $installer $need
    elif command -v sudo >/dev/null 2>&1; then
        echo "installing with sudo (a password may be requested)"
        sudo $installer $need
    else
        echo "run: sudo $installer $need" >&2
        exit 1
    fi
fi

# Fedora ships no VLC of its own, so the package above may simply not exist.
if [ "$vlc_system" = 1 ] && ! pkg-config --exists libvlc 2>/dev/null; then
    echo "libVLC development files are still missing." >&2
    if [ "$family" = fedora ]; then
        cat >&2 <<'EOF'
On Fedora, VLC comes from RPM Fusion. Enable it, then run this script again:

  sudo dnf install -y \
    "https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm"
  sudo dnf install -y vlc-devel
EOF
    fi
    exit 1
fi

if [ "$vlc_system" = 0 ]; then
    # libVLC: vendored, not installed system-wide.
    sh fetch-libvlc.sh
fi

[ "${1:-}" = "--deps" ] && exit 0

make VLC_SYSTEM=$vlc_system
make VLC_SYSTEM=$vlc_system install
