# R Television - Haiku front end (BeAPI)
#
#   make deps       vendor libVLC into third_party (installs nothing)
#   make            build build/RTelevision
#   make cli        build the headless tool only (no BeAPI needed)
#   make install    install into ~/config/apps and the Deskbar
#   make clean
#
# Packages required:  pkgman install gcc make curl_devel haiku_devel
# Optional (playback): vlc_devel - see MEDIA BACKEND below.

SHARED_DIR := ../../shared
include $(SHARED_DIR)/common.mk

APP    := RTelevision
BUILD  := build
UI_SRC := main.cpp MainWindow.cpp VideoView.cpp Icons.cpp AudioOutput.cpp

SEED        := ../../resources/seed-playlist.m3u
RDEF        := $(APP).rdef
HVIF        := ../../resources/icon/$(APP).hvif
ELF_TYPE    := application/x-vnd.be-elfexecutable
set_attrs    = resattr -o $(1) $(BUILD)/$(APP).rsrc && settype -t $(ELF_TYPE) $(1)
NOTICES     := ../../LICENSE ../../THIRD-PARTY-NOTICES.md
# ~/config/apps is packagefs and read-only; user binaries go here.
INSTALL_DIR ?= $(HOME)/config/non-packaged/apps
DATA_DIR    ?= $(HOME)/config/non-packaged/data/RTelevision

# MEDIA BACKEND
# fetch-libvlc.sh vendors libVLC into third_party/vlc-haiku-<arch>; a system
# vlc_devel is used when no vendored tree exists. HaikuPorts ships vlc_devel for
# x86_64/x86 but not (yet) for arm64 - with neither available the null backend
# is compiled instead: the channel list, search, favorites and the offline cache
# all work, playback reports that no backend is present.
# Override with VLC=0 / VLC=1.
ARCH        := $(shell uname -m)
VLC_VENDOR  := ../../third_party/vlc-haiku-$(ARCH)
VLC_HEADER  := /boot/system/develop/headers/vlc/vlc.h
# FFmpeg fallback for architectures with no VLC at all (arm64 today).
FF_VENDOR   := ../../third_party/ffmpeg-haiku-$(ARCH)

ifneq ($(wildcard $(VLC_VENDOR)/include/vlc/vlc.h),)
  VLC ?= 1
  VLC_CFLAGS := -I$(VLC_VENDOR)/include
  # $$ORIGIN: Haiku's runtime loader does not search the binary's own directory
  # on its own, so the bundled libvlc is found through an rpath instead.
  # libvlccore is named explicitly so it becomes a direct dependency of the
  # binary: an rpath does not carry over to a dependency's own dependencies.
  VLC_LDLIBS := -L$(VLC_VENDOR)/lib -lvlc -lvlccore -Wl,-rpath,'$$ORIGIN' 
else ifneq ($(wildcard $(VLC_HEADER)),)
  VLC ?= 1
  VLC_CFLAGS :=
  VLC_LDLIBS := -lvlc
else ifneq ($(wildcard $(FF_VENDOR)/include/libavcodec/avcodec.h),)
  VLC ?= 0
  FFMPEG ?= 1
  VLC_CFLAGS := -I$(FF_VENDOR)/include
  VLC_LDLIBS := -L$(FF_VENDOR)/lib -lavformat -lavcodec -lswscale -lswresample -lavutil \
                -Wl,-rpath,'$$ORIGIN'
else
  VLC ?= 0
  VLC_CFLAGS :=
  VLC_LDLIBS :=
endif
FFMPEG ?= 0

# HTTP BACKEND
# Same story for curl_devel. Without it the app talks HTTP through Haiku's own
# network services (libnetservices), so the offline cache still refreshes.
CURL_HEADER := /boot/system/develop/headers/curl/curl.h
CURL ?= $(if $(wildcard $(CURL_HEADER)),1,0)

ifeq ($(CURL),1)
  HTTP_SRC  := $(CORE_SRC_HTTP_CURL)
  HTTP_LIBS := -lcurl
else
  HTTP_SRC  := $(CORE_SRC_HTTP_HAIKU)
  # libnetservices.a pulls BPrivate::HashString out of libshared.a.
  HTTP_LIBS := -lnetservices -lshared -lbnetapi
  # netservices headers are private to the system, not in the default path.
  HTTP_CFLAGS := -I/boot/system/develop/headers/private/netservices
endif

ifeq ($(VLC),1)
  MEDIA_SRC  := $(CORE_SRC_VLC)
  MEDIA_LIBS := $(VLC_LDLIBS)
else ifeq ($(FFMPEG),1)
  MEDIA_SRC  := $(CORE_SRC_FFMPEG)
  MEDIA_LIBS := $(VLC_LDLIBS)
else
  MEDIA_SRC  := $(CORE_SRC_NULL)
  MEDIA_LIBS :=
  VLC_CFLAGS :=
endif

SRC := $(CORE_SRC) $(MEDIA_SRC) $(HTTP_SRC) $(UI_SRC)
OBJ := $(addprefix $(BUILD)/obj/,$(notdir $(SRC:.cpp=.o)))

CXXFLAGS := $(CORE_CXXFLAGS) -I. $(HTTP_CFLAGS) $(VLC_CFLAGS)
LIBS     := -lbe -lmedia -lnetwork $(HTTP_LIBS) $(MEDIA_LIBS)

VPATH := $(SHARED_DIR)/core:$(SHARED_DIR)/tools:.

.PHONY: all cli install uninstall clean backend

all: $(BUILD)/$(APP)

backend:
	@echo "media backend: $(if $(filter 1,$(VLC)),libVLC,$(if $(filter 1,$(FFMPEG)),FFmpeg,none))"
	@echo "http backend : $(if $(filter 1,$(CURL)),libcurl,Haiku netservices)"

$(BUILD)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Signature, version and icon: compiled from the .rdef and merged into the
# binary, then written to the attributes Tracker and the Deskbar read. resattr
# does that directly; mimeset would too, but only where the registrar sniffs
# the ELF correctly, which some development builds do not.
$(BUILD)/$(APP).rsrc: $(RDEF) $(HVIF)
	@mkdir -p $(BUILD)
	rc -o $@ $(RDEF)

$(BUILD)/$(APP): $(OBJ) $(BUILD)/$(APP).rsrc
	@mkdir -p $(BUILD)
	$(CXX) $(OBJ) $(LIBS) -o $@
	xres -o $@ $(BUILD)/$(APP).rsrc
	$(call set_attrs,$@)
ifneq ($(wildcard $(VLC_VENDOR)/plugins),)
	@# Haiku's runtime loader searches the binary's own directory, so the
	@# bundled libraries go right next to it - no rpath needed.
	cp -a $(VLC_VENDOR)/lib/*.so* $(BUILD)/ 2>/dev/null || true
	rm -rf $(BUILD)/vlc
	mkdir -p $(BUILD)/vlc
	cp -a $(VLC_VENDOR)/plugins $(BUILD)/vlc/plugins
	cp $(SEED) $(BUILD)/seed-playlist.m3u
endif
	@echo "built: $@  (media backend: $(if $(filter 1,$(VLC)),libVLC,$(if $(filter 1,$(FFMPEG)),FFmpeg,none)))"

cli: $(BUILD)/rtv-cli

# The headless tool never plays anything, so it takes the null media backend.
# libbe is still needed: BUrl and BReferenceable live there, and libnetservices
# uses both.
$(BUILD)/rtv-cli: $(CORE_SRC) $(CORE_SRC_NULL) $(HTTP_SRC) $(CLI_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -lbe -lnetwork $(HTTP_LIBS) -o $@

install: $(BUILD)/$(APP)
	mkdir -p "$(INSTALL_DIR)" "$(DATA_DIR)"
	cp $(BUILD)/$(APP) "$(INSTALL_DIR)/$(APP)"
	$(call set_attrs,"$(INSTALL_DIR)/$(APP)")
	cp $(SEED) "$(DATA_DIR)/seed-playlist.m3u"
	@# The bundled libVLC/FFmpeg are LGPL/GPL: their notices go with them.
	cp $(NOTICES) "$(DATA_DIR)/"
	rm -rf "$(DATA_DIR)/licenses"
	cp -a ../../licenses "$(DATA_DIR)/licenses"
ifeq ($(FFMPEG),1)
	cp -a $(FF_VENDOR)/lib/*.so* "$(INSTALL_DIR)/" 2>/dev/null || true
	cp $(SEED) "$(INSTALL_DIR)/seed-playlist.m3u"
endif
ifneq ($(wildcard $(VLC_VENDOR)/plugins),)
	cp -a $(VLC_VENDOR)/lib/*.so* "$(INSTALL_DIR)/" 2>/dev/null || true
	rm -rf "$(INSTALL_DIR)/vlc"
	mkdir -p "$(INSTALL_DIR)/vlc"
	cp -a $(VLC_VENDOR)/plugins "$(INSTALL_DIR)/vlc/plugins"
	cp $(SEED) "$(INSTALL_DIR)/seed-playlist.m3u"
endif
	@# Deskbar picks up anything linked into the applications menu directory.
	mkdir -p "$(HOME)/config/settings/deskbar/menu/Applications"
	ln -sf "$(INSTALL_DIR)/$(APP)" \
	       "$(HOME)/config/settings/deskbar/menu/Applications/$(APP)"
	@echo "installed: $(INSTALL_DIR)/$(APP)"

uninstall:
	rm -f "$(INSTALL_DIR)/$(APP)" \
	      "$(HOME)/config/settings/deskbar/menu/Applications/$(APP)"
	rm -rf "$(DATA_DIR)"

clean:
	rm -rf $(BUILD)

# Generated by -MMD: rebuild whatever a changed header touches.
-include $(OBJ:.o=.d)
