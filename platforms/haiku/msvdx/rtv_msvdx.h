/* Hardware H.264 on the GMA500's video decoder (MSVDX), behind a C interface
 * so that the VLC module never sees Chromium's headers and the engine never
 * sees VLC's.
 *
 * The engine is Chromium's H.264 parser and decoder (reference picture
 * management, POC, output order) driving psb_video's H.264 command builder
 * and a userland MSVDX driver: the same code R Chromium uses for YouTube. */
#ifndef RTV_MSVDX_H
#define RTV_MSVDX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rtv_msvdx rtv_msvdx;

/* One decoded picture, NV12, in output (presentation) order. The planes are
 * valid only for the duration of the call. */
typedef struct rtv_msvdx_picture {
	const uint8_t* luma;
	const uint8_t* chroma;	/* interleaved Cb Cr */
	int stride;
	int coded_width, coded_height;
	int visible_x, visible_y, visible_width, visible_height;
	int64_t pts;
} rtv_msvdx_picture;

typedef void (*rtv_msvdx_output_fn)(void* opaque, const rtv_msvdx_picture*);

/* The firmware file, or NULL if none is installed. */
const char* rtv_msvdx_firmware(void);

/* Claims the hardware; NULL if there is no firmware, no GMA500, or another
 * application (R Chromium playing a video, say) has it. */
rtv_msvdx* rtv_msvdx_create(rtv_msvdx_output_fn output, void* opaque);
void rtv_msvdx_destroy(rtv_msvdx*);

/* One Annex B access unit (or SPS/PPS). Pictures come out through the
 * callback. Returns 0, or -1 when the stream is one this hardware path
 * cannot decode (interlaced, 4:2:2, too large...) and the caller should hand
 * it to a software decoder. */
int rtv_msvdx_decode(rtv_msvdx*, const uint8_t* data, size_t size,
	int64_t pts);

/* Output every picture still held for reordering (end of stream). */
int rtv_msvdx_drain(rtv_msvdx*);

/* Forget everything in flight (seek, discontinuity). */
void rtv_msvdx_reset(rtv_msvdx*);

#ifdef __cplusplus
}
#endif

#endif
