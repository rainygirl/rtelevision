/* A small C interface to the MSVDX H.264 path, for callers that should not
 * see psb_video's headers (they define ASSERT, TRUE and friends). Only
 * stdint and the libva H.264 structures cross it.
 *
 * One decoder at a time: the hardware has one MTX and msvdx.c keeps its
 * page tables in globals. */
#ifndef MSVDX_DECODE_H
#define MSVDX_DECODE_H

#include <stdint.h>

#include "va_h264.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Map the registers, set up the MMU and load the firmware. 0 on success. */
int msvdx_decode_open(const char* firmware_path);
void msvdx_decode_close(void);

enum {
	MSVDX_PROFILE_BASELINE = 0,
	MSVDX_PROFILE_MAIN = 1,
	MSVDX_PROFILE_HIGH = 2
};

/* Allocate `count` NV12 surfaces of width x height and a decode context.
 * Replaces any previous configuration. 0 on success. */
int msvdx_decode_configure(int width, int height, int count, int profile);

/* One picture: begin (the target surface and the picture parameters),
 * one call per slice, end. `pic` and `iq` are copied. The slice data is
 * copied into the bitstream buffer; slice_data_offset/size are set here.
 * end() sends everything and waits for the hardware. 0 on success. */
int msvdx_decode_begin(int surface, const VAPictureParameterBufferH264* pic,
	const VAIQMatrixBufferH264* iq);
int msvdx_decode_slice(VASliceParameterBufferH264* slice, const uint8_t* data,
	uint32_t size);
int msvdx_decode_end(void);

/* The decoded picture, NV12, after the CPU cache has been dropped. */
int msvdx_decode_read(int surface, const uint8_t** luma,
	const uint8_t** chroma, int* stride);

#ifdef __cplusplus
}
#endif

#endif
