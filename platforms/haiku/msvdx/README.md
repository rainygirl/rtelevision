# libmsvdx_plugin: hardware H.264 for libVLC on the GMA500

A libVLC 3 video decoder module for the Intel GMA500 (Poulsbo) video
decoder, an Imagination VXD370 ("MSVDX") -- the chipset of the Sony VAIO P.
It scores 80, above libavcodec's 70, so VLC tries it first; it declines, and
libavcodec decodes as before, when:

- there is no `msvdx_fw.bin` (see below),
- there is no GMA500, or another application holds it (R Chromium playing a
  video: the named port `msvdx owner` is the lock),
- `RTV_MSVDX=0` is set,
- the stream is not H.264, is larger than 1920x1088, or turns out to be
  interlaced or not 4:2:0. The last two are only known from the SPS, so the
  module asks VLC to reload the decoder and refuses the reload.

## Layout

| path | what | licence |
|---|---|---|
| `vlc_msvdx.c` | the VLC module | MIT |
| `rtv_msvdx.{h,cc}` | C interface; Chromium's decoder driving the hardware | MIT |
| `chromium/` | Chromium 114's H.264 parser, POC and decoder, unmodified | BSD-3 |
| `shim/` | the slice of `//base`, `//ui/gfx`, `//media/base` those files use | MIT |
| `hw/` | userland MSVDX driver and psb_video's H.264 command builder | MIT |

`hw/` is a copy of `chromium114_port/files/media/gpu/haiku/msvdx/` in
haiku-rchromium-x86, where the same code decodes YouTube. Keep the two in
step.

## Firmware

Intel's `msvdx_fw.bin` goes in `~/config/non-packaged/data/firmware/` (or
`RTV_MSVDX_FIRMWARE`). Its licence does not allow redistribution; it is not
in this repository or any package.

## Building

`make` in `platforms/haiku` builds and bundles it on an x86_gcc2 machine
(`setarch x86 make`). By hand:

    setarch x86 make VLC_INCLUDE=<dir with vlc/plugins> VLCCORE=<libvlccore.so.9>
