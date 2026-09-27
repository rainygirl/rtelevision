# R Television - Mac OS X 10.4 PowerPC
#
#   make cli        build rtv-cli, the headless driver (no Cocoa needed)
#   make            build the application bundle
#   make install    copy it to /Applications
#
# This platform needs a compiler the system does not ship: Tiger's GCC 4.0.1 is
# C++98 only. `make toolchain` explains how to get GCC 4.7, which is the last
# GCC written in C - so Tiger's own compiler can build it - and the first with
# enough C++11 for the core.
#
# The media backend is libVLC 0.9.10, the last that runs on 10.4. It has no HLS
# demuxer, so the relay is put in plain MPEG-TS mode and feeds it a stream it
# can read. See docs/PORTING.md.

SHARED_DIR := ../../shared
CORE_CXXSTD := c++11
include $(SHARED_DIR)/common.mk

APP        := RTelevision
APP_NAME   := R Television
BUILD      := build
GCC_PREFIX ?= $(HOME)/gcc47
CXX        := $(GCC_PREFIX)/bin/g++
CC         := $(GCC_PREFIX)/bin/gcc
# The Cocoa front end is plain Objective-C compiled by Apple's own GCC, which
# knows this system's headers; it meets the C++ core across a C boundary
# (UiBridge.h), so the two compilers never share a C++ ABI.
OBJC       := /usr/bin/gcc-4.0

VLC_SDK     := ../../third_party/vlc-ppc
CURL_PREFIX ?= $(HOME)/curl-ppc
SSL_PREFIX  ?= $(HOME)/ssl-ppc

CXXFLAGS := $(CORE_CXXFLAGS) -I. -I$(VLC_SDK)/include -I$(CURL_PREFIX)/include
# libstdc++ and libgcc go in statically: GCC 4.7 lives in a home directory, and
# an application bundle must not need DYLD_LIBRARY_PATH to start. curl and
# OpenSSL are static for the same reason; only libVLC stays a dylib, and that
# one travels inside the bundle.
STATIC_RUNTIME := -static-libstdc++ -static-libgcc
TLS_LIBS := $(CURL_PREFIX)/lib/libcurl.a $(SSL_PREFIX)/lib/libssl.a \
            $(SSL_PREFIX)/lib/libcrypto.a -lz
# libvlc's undefined symbols are recorded as coming from
# @loader_path/../lib/libvlccore.0.dylib, a path the linker cannot follow.
# -dylib_file maps that install name to the copy in the SDK, which is what it is
# for; at run time the bundle layout makes the same path resolve on its own.
VLCCORE_NAME := @loader_path/../lib/libvlccore.0.dylib
LDFLAGS  := $(STATIC_RUNTIME) -L$(VLC_SDK)/lib -lvlc \
            -Wl,-dylib_file,$(VLCCORE_NAME):$(VLC_SDK)/lib/libvlccore.0.dylib \
            $(TLS_LIBS)

MEDIA_SRC := $(CORE_SRC_VLC_LEGACY)
HTTP_SRC  := $(CORE_SRC_HTTP_CURL)
CXX_SRC   := $(CORE_SRC) $(MEDIA_SRC) $(HTTP_SRC) UiBridge.cpp
OBJ       := $(patsubst %.cpp,$(BUILD)/obj/%.o,$(notdir $(CXX_SRC))) $(BUILD)/obj/main.o

VERSION   := 1.0.0
BUNDLE_ID := org.rainygirl.rtelevision
APP_DIR   := $(BUILD)/$(APP_NAME).app
MACOS_DIR := $(APP_DIR)/Contents/MacOS
RES_DIR   := $(APP_DIR)/Contents/Resources
SEED      := ../../resources/seed-playlist.m3u
NOTICES   := ../../LICENSE ../../THIRD-PARTY-NOTICES.md
# Tiger has no certificate store libcurl can read, so the bundle carries one.
# Any current Mozilla bundle will do; OpenSSL's install leaves one here.
CA_BUNDLE ?= $(SSL_PREFIX)/ssl/cert.pem

VPATH := $(SHARED_DIR)/core:$(SHARED_DIR)/tools:.

.PHONY: all app cli install clean toolchain

all: app

$(BUILD)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# The front end is the one file Apple's own compiler builds: it is the one that
# knows Tiger's Cocoa headers, and UiBridge.h is plain C so the two never meet
# in C++.
$(BUILD)/obj/main.o: main.m UiBridge.h
	@mkdir -p $(dir $@)
	$(OBJC) -x objective-c -O2 -Wall -I. -c $< -o $@

# libvlc's install name is @loader_path/lib/libvlc.dylib and its modules ask for
# @loader_path/../lib, so laying the bundle out the way VLC.app does makes every
# path resolve on its own - no install_name_tool anywhere.
app: $(BUILD)/.bundle-stamp

# A stamp rather than the bundle itself: "R Television.app" has a space in it,
# and a make target cannot.
$(BUILD)/.bundle-stamp: $(OBJ) $(SEED) Info.plist.in
	@mkdir -p "$(MACOS_DIR)" "$(RES_DIR)"
	$(CXX) $(OBJ) $(LDFLAGS) -framework Cocoa -o "$(MACOS_DIR)/$(APP_NAME)"
	rm -rf "$(MACOS_DIR)/lib" "$(MACOS_DIR)/modules"
	cp -R $(VLC_SDK)/lib "$(MACOS_DIR)/lib"
	cp -R $(VLC_SDK)/modules "$(MACOS_DIR)/modules"
	@# The PowerPC VLC's libzvbi was built against libiconv 7 and Tiger ships
	@# libiconv 5, so the teletext plugin can never load here - it fails in
	@# VLC's own bundle too. Dropping it keeps dyld from tripping over it.
	rm -f "$(MACOS_DIR)/modules/libzvbi_plugin.dylib" "$(MACOS_DIR)/lib/libzvbi.0.dylib"
	@# VLC's own Mac interface, which this app never shows. Merely loading it
	@# registers an EyeTV distributed-notification observer whose handler
	@# dereferences a null object the moment any such notification arrives, and
	@# the whole process goes down with it. minimal_macosx is the video output
	@# and is kept.
	rm -f "$(MACOS_DIR)/modules/libmacosx_plugin.dylib" \
	      "$(MACOS_DIR)/modules/libaccess_eyetv_plugin.dylib"
	cp $(SEED) "$(RES_DIR)/seed-playlist.m3u"
	cp $(CA_BUNDLE) "$(RES_DIR)/cacert.pem"
	cp $(NOTICES) "$(RES_DIR)/"
	rm -rf "$(RES_DIR)/licenses" && cp -R ../../licenses "$(RES_DIR)/licenses"
	sed -e 's/@EXECUTABLE@/$(APP_NAME)/' -e 's/@BUNDLE_ID@/$(BUNDLE_ID)/' \
	    -e 's/@VERSION@/$(VERSION)/' Info.plist.in > "$(APP_DIR)/Contents/Info.plist"
	printf 'APPL????' > "$(APP_DIR)/Contents/PkgInfo"
	@touch $@
	@echo "built: $(APP_DIR)"

# The core without a window: the first thing to bring up here, as on every other
# platform. Needs no Cocoa and no video output.
cli: $(BUILD)/rtv-cli

$(BUILD)/rtv-cli: $(CORE_SRC) $(CORE_SRC_NULL) $(HTTP_SRC) $(CLI_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $^ $(STATIC_RUNTIME) $(TLS_LIBS) -o $@

install: app
	rm -rf "/Applications/$(APP_NAME).app"
	cp -R "$(APP_DIR)" "/Applications/$(APP_NAME).app"
	@echo "installed: /Applications/$(APP_NAME).app"

toolchain:
	@echo "GCC 4.7.4, built on the machine itself (see docs/PORTING.md):"
	@echo "  ./configure --prefix=$$HOME/gcc47 --enable-languages=c,c++ \\"
	@echo "      --enable-threads=posix --enable-libstdcxx-time=yes \\"
	@echo "      --disable-multilib --disable-nls --disable-bootstrap"
	@echo "libVLC 0.9.10:  sh fetch-libvlc.sh   (run it on any Mac, copy the tree over)"
	@echo "OpenSSL + curl: built into $(CURL_PREFIX)"

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d)
