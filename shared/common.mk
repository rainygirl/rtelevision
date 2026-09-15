# Sources and flags shared by every platform front end.
#
# A platform Makefile sets SHARED_DIR (path to this directory) and then
# includes this file. It must also add exactly one media backend
# ($(CORE_SRC_VLC), $(CORE_SRC_FFMPEG) or $(CORE_SRC_NULL)) and one HTTP backend
# ($(CORE_SRC_HTTP_CURL) or $(CORE_SRC_HTTP_HAIKU)) to its source list.

CORE_SRC := \
	$(SHARED_DIR)/core/StringUtil.cpp \
	$(SHARED_DIR)/core/Country.cpp \
	$(SHARED_DIR)/core/Strings.cpp \
	$(SHARED_DIR)/core/M3UParser.cpp \
	$(SHARED_DIR)/core/Paths.cpp \
	$(SHARED_DIR)/core/PlaylistStore.cpp \
	$(SHARED_DIR)/core/ChannelIndex.cpp \
	$(SHARED_DIR)/core/Favorites.cpp \
	$(SHARED_DIR)/core/MediaPlayer.cpp \
	$(SHARED_DIR)/core/HlsRelay.cpp \
	$(SHARED_DIR)/core/RelayedMediaPlayer.cpp \
	$(SHARED_DIR)/core/AppController.cpp

# A platform picks exactly one media backend ...
CORE_SRC_VLC    := $(SHARED_DIR)/core/VlcMediaPlayer.cpp
CORE_SRC_FFMPEG := $(SHARED_DIR)/core/FFmpegMediaPlayer.cpp
CORE_SRC_NULL   := $(SHARED_DIR)/core/NullMediaPlayer.cpp

# ... and exactly one HTTP backend.
CORE_SRC_HTTP_CURL  := $(SHARED_DIR)/core/CurlHttpClient.cpp
CORE_SRC_HTTP_HAIKU := $(SHARED_DIR)/core/HaikuHttpClient.cpp

CLI_SRC := $(SHARED_DIR)/tools/cli_main.cpp

# -MMD -MP makes the compiler emit .d files so a changed header forces a
# rebuild. Without it a struct layout change silently leaves stale objects.
CORE_CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -MMD -MP -I$(SHARED_DIR)
