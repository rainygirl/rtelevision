/* See msvdx_decode.h. */
#include "msvdx_decode.h"

#include <stdlib.h>
#include <string.h>

#include "msvdx.h"
#include "psb/psb_shim.h"

/* Every slice of one picture stays here until msvdx_decode_end(): psb_H264
 * builds a command buffer per slice that DMAs its slice from this buffer. */
#define BITSTREAM_SIZE	(4 * 1024 * 1024)

static int sOpen;
static struct psb_driver_data_s sDriver;
static struct psb_surface_s sSurface[PSB_MAX_SURFACES];
static struct object_surface_s sObject[PSB_MAX_SURFACES];
static int sSurfaces;
static struct object_context_s sContext;
static struct psb_buffer_s sBitstream;
static uint32_t sBitstreamUsed;


static void
release_configuration(void)
{
	int i;
	if (sContext.format_data != NULL)
		psb_H264_vtable.destroyContext(&sContext);
	memset(&sContext, 0, sizeof(sContext));
	for (i = 0; i < sSurfaces; i++)
		psb_surface_destroy(&sSurface[i]);
	sSurfaces = 0;
	memset(&sDriver, 0, sizeof(sDriver));
}


int
msvdx_decode_open(const char* firmware_path)
{
	if (sOpen)
		return 1;
	if (msvdx_open(firmware_path) != 0) {
		msvdx_close();
		return 1;
	}
	if (psb_buffer_create(&sDriver, BITSTREAM_SIZE, psb_bt_cpu_vpu,
			&sBitstream) != VA_STATUS_SUCCESS) {
		msvdx_close();
		return 1;
	}
	sOpen = 1;
	return 0;
}


void
msvdx_decode_close(void)
{
	if (!sOpen)
		return;
	release_configuration();
	psb_buffer_destroy(&sBitstream);
	psb_host_shutdown();
	msvdx_close();
	sOpen = 0;
}


int
msvdx_decode_configure(int width, int height, int count, int profile)
{
	struct object_config_s config;
	int i;

	if (!sOpen)
		return 1;
	release_configuration();
	if (count > PSB_MAX_SURFACES)
		count = PSB_MAX_SURFACES;
	for (i = 0; i < count; i++) {
		if (psb_surface_create(&sDriver, width, height, &sSurface[i])
				!= VA_STATUS_SUCCESS) {
			sSurfaces = i;
			release_configuration();
			return 1;
		}
		sObject[i].surface_id = i;
		sObject[i].width = width;
		sObject[i].height = height;
		sObject[i].psb_surface = &sSurface[i];
		sDriver.surface_heap.surfaces[i] = &sObject[i];
	}
	sSurfaces = count;

	memset(&sContext, 0, sizeof(sContext));
	sContext.driver_data = &sDriver;
	sContext.picture_width = width;
	sContext.picture_height = height;
	sContext.num_render_targets = count;
	sContext.msvdx_context = 1;

	memset(&config, 0, sizeof(config));
	config.profile = profile == MSVDX_PROFILE_BASELINE ? VAProfileH264Baseline
		: profile == MSVDX_PROFILE_MAIN ? VAProfileH264Main : VAProfileH264High;
	if (psb_H264_vtable.createContext(&sContext, &config) != VA_STATUS_SUCCESS) {
		release_configuration();
		return 1;
	}
	return 0;
}


int
msvdx_decode_begin(int surface, const VAPictureParameterBufferH264* pic,
	const VAIQMatrixBufferH264* iq)
{
	VAPictureParameterBufferH264* picCopy;
	VAIQMatrixBufferH264* iqCopy;
	struct object_buffer_s picBuffer;
	struct object_buffer_s iqBuffer;
	object_buffer_p buffers[2];

	if (sContext.format_data == NULL || surface < 0 || surface >= sSurfaces)
		return 1;
	/* psb_H264 takes ownership of both and frees them. */
	picCopy = malloc(sizeof(*picCopy));
	iqCopy = malloc(sizeof(*iqCopy));
	memcpy(picCopy, pic, sizeof(*picCopy));
	memcpy(iqCopy, iq, sizeof(*iqCopy));

	sContext.current_render_target = &sObject[surface];
	sBitstreamUsed = 0;

	picBuffer.type = VAPictureParameterBufferType;
	picBuffer.buffer_data = picCopy;
	picBuffer.size = sizeof(*picCopy);
	picBuffer.num_elements = 1;
	picBuffer.psb_buffer = NULL;
	iqBuffer.type = VAIQMatrixBufferType;
	iqBuffer.buffer_data = iqCopy;
	iqBuffer.size = sizeof(*iqCopy);
	iqBuffer.num_elements = 1;
	iqBuffer.psb_buffer = NULL;
	buffers[0] = &picBuffer;
	buffers[1] = &iqBuffer;
	if (psb_H264_vtable.beginPicture(&sContext) != VA_STATUS_SUCCESS)
		return 1;
	return psb_H264_vtable.renderPicture(&sContext, buffers, 2)
		!= VA_STATUS_SUCCESS;
}


int
msvdx_decode_slice(VASliceParameterBufferH264* slice, const uint8_t* data,
	uint32_t size)
{
	struct object_buffer_s sliceBuffer;
	struct object_buffer_s dataBuffer;
	object_buffer_p buffers[2];

	if (sBitstreamUsed + size > BITSTREAM_SIZE)
		return 1;
	memcpy(sBitstream.mb.cpu + sBitstreamUsed, data, size);
	msvdx_flush(&sBitstream.mb, sBitstreamUsed, size);
	slice->slice_data_offset = sBitstreamUsed;
	slice->slice_data_size = size;
	slice->slice_data_flag = VA_SLICE_DATA_FLAG_ALL;
	sBitstreamUsed += (size + 15) & ~15u;

	sliceBuffer.type = VASliceParameterBufferType;
	sliceBuffer.buffer_data = slice;
	sliceBuffer.size = sizeof(*slice);
	sliceBuffer.num_elements = 1;
	sliceBuffer.psb_buffer = NULL;
	dataBuffer.type = VASliceDataBufferType;
	dataBuffer.buffer_data = NULL;
	dataBuffer.size = size;
	dataBuffer.num_elements = 1;
	dataBuffer.psb_buffer = &sBitstream;
	buffers[0] = &sliceBuffer;
	buffers[1] = &dataBuffer;
	return psb_H264_vtable.renderPicture(&sContext, buffers, 2)
		!= VA_STATUS_SUCCESS;
}


int
msvdx_decode_end(void)
{
	if (sContext.format_data == NULL)
		return 1;
	return psb_H264_vtable.endPicture(&sContext) != VA_STATUS_SUCCESS;
}


int
msvdx_decode_read(int surface, const uint8_t** luma, const uint8_t** chroma,
	int* stride)
{
	struct psb_surface_s* s;
	if (surface < 0 || surface >= sSurfaces)
		return 1;
	s = &sSurface[surface];
	msvdx_flush(&s->buf.mb, 0, s->buf.mb.size);
	*luma = s->buf.mb.cpu;
	*chroma = s->buf.mb.cpu + s->chroma_offset;
	*stride = s->stride;
	return 0;
}
