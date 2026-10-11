/* A libVLC 3 video decoder module for H.264 on the GMA500's video decoder
 * (MSVDX). It scores above libavcodec, so VLC tries it first; when there is
 * no firmware, no GMA500, the hardware is taken, or the stream is one this
 * path does not handle (interlaced, 4:2:2, larger than 1920x1088), it steps
 * aside and libavcodec decodes as before.
 *
 * Pictures come out as NV12, which swscale converts and scales straight into
 * the RV32 buffer R Television draws. */
#include <stdlib.h>
#include <string.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_codec.h>

#include "rtv_msvdx.h"

static int Open(vlc_object_t*);
static void Close(vlc_object_t*);

vlc_module_begin()
	set_shortname("MSVDX")
	set_description("GMA500 (MSVDX) hardware H.264 decoder")
	set_capability("video decoder", 80)
	set_category(CAT_INPUT)
	set_subcategory(SUBCAT_INPUT_VCODEC)
	set_callbacks(Open, Close)
	add_shortcut("msvdx")
vlc_module_end()

struct decoder_sys_t {
	rtv_msvdx* hw;
	int width, height, vx, vy, vw, vh;
};

/* Set when a stream turns out to be one this path cannot decode. The decoder
 * then asks VLC to reload it, and the Open() that reload triggers has to say
 * no so that libavcodec gets the stream. */
static bool sRefuseNext;


static void
OutputPicture(void* opaque, const rtv_msvdx_picture* p)
{
	decoder_t* dec = opaque;
	decoder_sys_t* sys = dec->p_sys;
	picture_t* pic;
	int row;

	if (p->coded_width != sys->width || p->coded_height != sys->height
		|| p->visible_x != sys->vx || p->visible_y != sys->vy
		|| p->visible_width != sys->vw || p->visible_height != sys->vh) {
		video_format_t* v = &dec->fmt_out.video;
		dec->fmt_out.i_codec = VLC_CODEC_NV12;
		v->i_chroma = VLC_CODEC_NV12;
		v->i_width = p->coded_width;
		v->i_height = p->coded_height;
		v->i_x_offset = p->visible_x;
		v->i_y_offset = p->visible_y;
		v->i_visible_width = p->visible_width;
		v->i_visible_height = p->visible_height;
		if (dec->fmt_in.video.i_sar_num && dec->fmt_in.video.i_sar_den) {
			v->i_sar_num = dec->fmt_in.video.i_sar_num;
			v->i_sar_den = dec->fmt_in.video.i_sar_den;
		} else {
			v->i_sar_num = v->i_sar_den = 1;
		}
		v->i_frame_rate = dec->fmt_in.video.i_frame_rate;
		v->i_frame_rate_base = dec->fmt_in.video.i_frame_rate_base;
		v->b_color_range_full = dec->fmt_in.video.b_color_range_full;
		v->space = dec->fmt_in.video.space;
		v->primaries = dec->fmt_in.video.primaries;
		v->transfer = dec->fmt_in.video.transfer;
		if (decoder_UpdateVideoFormat(dec) != 0)
			return;
		sys->width = p->coded_width;
		sys->height = p->coded_height;
		sys->vx = p->visible_x;
		sys->vy = p->visible_y;
		sys->vw = p->visible_width;
		sys->vh = p->visible_height;
		msg_Dbg(dec, "msvdx: %dx%d, showing %dx%d", sys->width, sys->height,
			sys->vw, sys->vh);
	}

	pic = decoder_NewPicture(dec);
	if (pic == NULL)
		return;
	for (row = 0; row < p->coded_height && row < pic->p[0].i_lines; row++) {
		memcpy(pic->p[0].p_pixels + row * pic->p[0].i_pitch,
			p->luma + row * p->stride, p->coded_width);
	}
	for (row = 0; row < p->coded_height / 2 && row < pic->p[1].i_lines;
			row++) {
		memcpy(pic->p[1].p_pixels + row * pic->p[1].i_pitch,
			p->chroma + row * p->stride, p->coded_width);
	}
	pic->date = p->pts > 0 ? p->pts : VLC_TS_INVALID;
	pic->b_progressive = true;
	decoder_QueueVideo(dec, pic);
}


static int
Decode(decoder_t* dec, block_t* block)
{
	decoder_sys_t* sys = dec->p_sys;
	mtime_t pts;

	if (block == NULL) {
		rtv_msvdx_drain(sys->hw);
		return VLCDEC_SUCCESS;
	}
	if (block->i_flags & BLOCK_FLAG_CORRUPTED) {
		block_Release(block);
		return VLCDEC_SUCCESS;
	}
	pts = block->i_pts > VLC_TS_INVALID ? block->i_pts : block->i_dts;
	if (rtv_msvdx_decode(sys->hw, block->p_buffer, block->i_buffer,
			pts) != 0) {
		/* VLC hands this block to whichever decoder the reload picks, so
		 * it is not released here. */
		msg_Warn(dec, "msvdx: stream not supported, handing it to software");
		sRefuseNext = true;
		return VLCDEC_RELOAD;
	}
	block_Release(block);
	return VLCDEC_SUCCESS;
}


static void
Flush(decoder_t* dec)
{
	rtv_msvdx_reset(dec->p_sys->hw);
}


static int
Open(vlc_object_t* obj)
{
	decoder_t* dec = (decoder_t*)obj;
	decoder_sys_t* sys;
	const char* env = getenv("RTV_MSVDX");

	if (dec->fmt_in.i_codec != VLC_CODEC_H264)
		return VLC_EGENERIC;
	if (sRefuseNext) {
		sRefuseNext = false;
		return VLC_EGENERIC;
	}
	if (env != NULL && strcmp(env, "0") == 0)
		return VLC_EGENERIC;
	if (dec->fmt_in.video.i_width > 1920 || dec->fmt_in.video.i_height > 1088)
		return VLC_EGENERIC;
	if (rtv_msvdx_firmware() == NULL)
		return VLC_EGENERIC;

	sys = calloc(1, sizeof(*sys));
	if (sys == NULL)
		return VLC_ENOMEM;
	dec->p_sys = sys;
	sys->hw = rtv_msvdx_create(OutputPicture, dec);
	if (sys->hw == NULL) {
		msg_Dbg(dec, "msvdx: hardware unavailable");
		free(sys);
		return VLC_EGENERIC;
	}

	/* SPS and PPS in Annex B form, when the packetizer found them first. */
	if (dec->fmt_in.i_extra > 4) {
		const uint8_t* x = dec->fmt_in.p_extra;
		if (x[0] == 0 && x[1] == 0 && (x[2] == 1 || (x[2] == 0 && x[3] == 1))
			&& rtv_msvdx_decode(sys->hw, x, dec->fmt_in.i_extra, -1) != 0) {
			rtv_msvdx_destroy(sys->hw);
			free(sys);
			return VLC_EGENERIC;
		}
	}

	dec->fmt_out.i_codec = VLC_CODEC_NV12;
	dec->fmt_out.video.i_chroma = VLC_CODEC_NV12;
	dec->pf_decode = Decode;
	dec->pf_flush = Flush;
	msg_Info(dec, "msvdx: decoding H.264 on the GMA500");
	return VLC_SUCCESS;
}


static void
Close(vlc_object_t* obj)
{
	decoder_t* dec = (decoder_t*)obj;
	rtv_msvdx_destroy(dec->p_sys->hw);
	free(dec->p_sys);
}
