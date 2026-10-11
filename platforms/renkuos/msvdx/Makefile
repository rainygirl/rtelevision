# libmsvdx_plugin.so: hardware H.264 for libVLC on the GMA500 (x86_gcc2
# hybrid, secondary architecture). See vlc_msvdx.c and README.md.
#
#   setarch x86 make VLC_INCLUDE=<dir holding vlc/plugins> VLCCORE=<libvlccore.so>
#
# VLC_INCLUDE defaults to the vlc_x86_devel headers; VLCCORE to the copy the
# application bundles.
CC      = gcc-x86
CXX     = g++-x86
BUILD   ?= build
VLC_INCLUDE ?= /boot/system/develop/headers/x86/vlc
VLCCORE ?= ../../../third_party/vlc-haiku-BePC/lib/libvlccore.so.9

HW_C  := hw/msvdx.c hw/msvdx_decode.c hw/psb/psb_H264.c hw/psb/psb_cmdbuf.c \
         hw/psb/psb_cmdbuf_host.c
CR_CC := chromium/media/video/h264_parser.cc chromium/media/video/h264_poc.cc \
         chromium/media/video/h264_bit_reader.cc \
         chromium/media/video/h264_level_limits.cc \
         chromium/media/gpu/h264_dpb.cc chromium/media/gpu/codec_picture.cc \
         chromium/media/gpu/h264_decoder.cc shim/rtv_shim.cc rtv_msvdx.cc

OBJ := $(patsubst %.c,$(BUILD)/%.o,$(HW_C)) \
       $(patsubst %.cc,$(BUILD)/%.o,$(CR_CC)) $(BUILD)/vlc_msvdx.o

# psb_video is written for gcc -std=gnu99 and is warning-heavy.
HW_FLAGS := -std=gnu99 -O2 -fPIC -w -Ihw -Ihw/hwdefs -Ihw/psb \
            -I/boot/system/develop/headers/private/drivers
CR_FLAGS := -std=gnu++17 -O2 -fPIC -fno-exceptions -Ishim -Ichromium -I. -Ihw
VLC_FLAGS := -std=gnu99 -O2 -fPIC -D__PLUGIN__ -DMODULE_STRING=\"msvdx\" \
             -I$(VLC_INCLUDE)/plugins -I.

$(BUILD)/libmsvdx_plugin.so: $(OBJ)
	$(CXX) -shared -o $@ $(OBJ) $(VLCCORE)

$(BUILD)/hw/%.o: hw/%.c
	@mkdir -p $(dir $@)
	$(CC) $(HW_FLAGS) -c $< -o $@

$(BUILD)/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CR_FLAGS) -c $< -o $@

$(BUILD)/vlc_msvdx.o: vlc_msvdx.c rtv_msvdx.h
	@mkdir -p $(dir $@)
	$(CC) $(VLC_FLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: clean
