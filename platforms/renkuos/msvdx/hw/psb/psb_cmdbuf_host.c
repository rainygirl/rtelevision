/* The host half of psb_video's command buffers, rewritten for this
 * prototype: where psb_video hands a buffer list and relocation records to
 * the DRM cmdbuf ioctl and the kernel patches addresses and sends the
 * FW_VA_RENDER messages, here every buffer's decoder address is known when
 * it is allocated, so relocations are applied on the spot and the messages
 * go straight onto the MTX ring (msvdx.c).
 *
 * The FW_VA_RENDER layout is the one in Intel's psb kernel driver
 * (psb-kmp, psb_msvdx.h): 32 bytes, size/id/buffer size, MMUPTD, LLDMA
 * address, context, fence, operating mode, first/last MB, flags. */
#include "psb_shim.h"

#include <stdlib.h>

#define CMD_SIZE		(0x3000)
#define LLDMA_SIZE		(0x2000)

#define VA_MSGID_RENDER			0x81
#define VA_MSGID_CMD_COMPLETED	0xc0
#define VA_MSGID_CMD_FAILED		0xc5
#define VA_MSGID_CMD_UNSUPPORTED 0xc6
#define VA_MSGID_CMD_HW_PANIC	0xc7

int psb_verbose = 0;
static uint32_t sFence = 1;


/* ---- buffers and surfaces ------------------------------------------------ */

VAStatus
psb_buffer_create(psb_driver_data_p driver_data, unsigned int size,
	psb_buffer_type_t type, psb_buffer_p buf)
{
	(void)driver_data;
	memset(buf, 0, sizeof(*buf));
	if (msvdx_alloc(&buf->mb, size) != 0)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;
	buf->type = type;
	return VA_STATUS_SUCCESS;
}


void
psb_buffer_destroy(psb_buffer_p buf)
{
	msvdx_free(&buf->mb);
}


int
psb_buffer_map(psb_buffer_p buf, void* address)
{
	*(void**)address = buf->mb.cpu;
	return 0;
}


int
psb_buffer_unmap(psb_buffer_p buf)
{
	/* Whatever the CPU wrote has to reach memory before the decoder reads
	 * it: the page tables map these pages cached. */
	msvdx_flush(&buf->mb, 0, buf->mb.size);
	return 0;
}


VAStatus
psb_surface_create(psb_driver_data_p driver_data, int width, int height,
	psb_surface_p surface)
{
	memset(surface, 0, sizeof(*surface));
	/* psb_surface.c: the stride is one of the hardware's fixed strides. */
	if (width <= 512) {
		surface->stride_mode = STRIDE_512;
		surface->stride = 512;
	} else if (width <= 1024) {
		surface->stride_mode = STRIDE_1024;
		surface->stride = 1024;
	} else if (width <= 1280) {
		surface->stride_mode = STRIDE_1280;
		surface->stride = 1280;
	} else if (width <= 2048) {
		surface->stride_mode = STRIDE_2048;
		surface->stride = 2048;
	} else
		return VA_STATUS_ERROR_ALLOCATION_FAILED;
	height = (height + 31) & ~31;
	surface->luma_offset = 0;
	surface->chroma_offset = surface->stride * height;
	surface->size = surface->stride * height * 3 / 2;
	return psb_buffer_create(driver_data, surface->size, psb_bt_surface,
		&surface->buf);
}


void
psb_surface_destroy(psb_surface_p surface)
{
	psb_buffer_destroy(&surface->buf);
}


VAStatus
psb_surface_set_chroma(psb_surface_p surface, int value)
{
	memset(surface->buf.mb.cpu + surface->chroma_offset, value,
		surface->size - surface->chroma_offset);
	msvdx_flush(&surface->buf.mb, 0, surface->buf.mb.size);
	return VA_STATUS_SUCCESS;
}


VAStatus
psb_surface_sync(psb_surface_p surface)
{
	/* psb_context_flush_cmdbuf() already waits for completion. */
	msvdx_flush(&surface->buf.mb, 0, surface->buf.mb.size);
	return VA_STATUS_SUCCESS;
}


void*
object_heap_lookup(struct object_heap_s* heap, int id)
{
	if (id < 0 || id >= PSB_MAX_SURFACES)
		return NULL;
	return heap->surfaces[id];
}


/* ---- command buffers ----------------------------------------------------- */

void
psb_cmdbuf_add_relocation(psb_cmdbuf_p cmdbuf, uint32_t* addr_in_cmdbuf,
	psb_buffer_p ref_buffer, uint32_t buf_offset, uint32_t mask,
	uint32_t background, uint32_t align_shift, uint32_t dst_buffer)
{
	/* psb_apply_reloc() in the kernel: val = offset + pre_add, shifted
	 * right by the alignment shift, merged into the background. */
	uint32_t val = ref_buffer->mb.dev + buf_offset;
	(void)cmdbuf;
	(void)dst_buffer;
	val >>= align_shift;
	*addr_in_cmdbuf = (background & ~mask) | (val & mask);
}


int
psb_cmdbuf_buffer_ref(psb_cmdbuf_p cmdbuf, psb_buffer_p buf)
{
	(void)cmdbuf;
	(void)buf;
	return 0;
}


static psb_cmdbuf_p sCmdbuf;

void
psb_host_shutdown(void)
{
	/* Call before msvdx_close(): the command buffer's decoder mapping goes
	 * with the page tables. */
	if (sCmdbuf != NULL) {
		psb_buffer_destroy(&sCmdbuf->buf);
		free(sCmdbuf);
		sCmdbuf = NULL;
	}
}

int
psb_context_get_next_cmdbuf(object_context_p obj_context)
{
	psb_cmdbuf_p cmdbuf = obj_context->cmdbuf;
	if (cmdbuf == NULL) {
		if (sCmdbuf == NULL) {
			sCmdbuf = calloc(1, sizeof(*sCmdbuf));
			if (psb_buffer_create(NULL, CMD_SIZE + LLDMA_SIZE,
					psb_bt_cpu_vpu, &sCmdbuf->buf) != VA_STATUS_SUCCESS)
				return 1;
		}
		cmdbuf = sCmdbuf;
		obj_context->cmdbuf = cmdbuf;
	}
	if (cmdbuf->cmd_count == 0) {
		memset(cmdbuf->buf.mb.cpu, 0, CMD_SIZE + LLDMA_SIZE);
		cmdbuf->cmd_base = cmdbuf->buf.mb.cpu;
		cmdbuf->cmd_start = cmdbuf->cmd_base;
		cmdbuf->cmd_idx = (uint32_t*)cmdbuf->cmd_base;
		cmdbuf->lldma_base = cmdbuf->cmd_base + CMD_SIZE;
		cmdbuf->lldma_idx = cmdbuf->lldma_base;
		cmdbuf->lldma_last = NULL;
		cmdbuf->reg_start = NULL;
		cmdbuf->rendec_block_start = NULL;
		cmdbuf->rendec_chunk_start = NULL;
		cmdbuf->last_next_segment_cmd = NULL;
		cmdbuf->skip_block_start = NULL;
	}
	return 0;
}


int
psb_context_submit_cmdbuf(object_context_p obj_context)
{
	psb_cmdbuf_p cmdbuf = obj_context->cmdbuf;
	uint32_t size = (uint8_t*)cmdbuf->cmd_idx - cmdbuf->cmd_start;
	uint32_t* msg;
	uint32_t lldma;

	if (cmdbuf->cmd_count >= 16)
		psb_context_flush_cmdbuf(obj_context);
	msg = cmdbuf->msgs[cmdbuf->cmd_count++];
	*cmdbuf->cmd_idx = 0;	/* a trailing 0, as psb_video does */

	/* An LLDMA record that copies this command buffer into the MTX. */
	lldma = psb_cmdbuf_lldma_create(cmdbuf, &cmdbuf->buf,
		cmdbuf->cmd_start - cmdbuf->cmd_base, size, 0,
		obj_context->video_op == psb_video_vld
			? LLDMA_TYPE_RENDER_BUFF_VLD : LLDMA_TYPE_RENDER_BUFF_MC);

	memset(msg, 0, 32);
	msg[0] = 32 | (VA_MSGID_RENDER << 8) | ((size & 0x0fff) << 16);
	msg[1] = 0;	/* MMUPTD, at send time */
	msg[2] = cmdbuf->buf.mb.dev + lldma;
	msg[3] = obj_context->msvdx_context;
	msg[4] = 0;	/* fence, at send time */
	msg[5] = obj_context->operating_mode;
	msg[6] = (obj_context->first_mb & 0xffff)
		| ((obj_context->last_mb & 0xffff) << 16);
	msg[7] = obj_context->flags;

	cmdbuf->cmd_start = (uint8_t*)cmdbuf->cmd_idx;
	return 0;
}


static int
wait_for_fence(uint32_t fence)
{
	uint32_t reply[32];
	int n;
	for (;;) {
		n = msvdx_receive(reply, 32, 2000);
		if (n == 0) {
			fprintf(stderr, "msvdx: no reply for fence %u\n", fence);
			msvdx_dump_state("timeout");
			return 1;
		}
		switch ((reply[0] >> 8) & 0xff) {
			case VA_MSGID_CMD_COMPLETED:
				psb__information_message("completed: fence %u flags %08x\n",
					reply[1], n > 2 ? reply[2] : 0);
				if (reply[1] == fence)
					return 0;
				break;
			case VA_MSGID_CMD_FAILED:
				fprintf(stderr, "msvdx: CMD_FAILED fence %u irq %08x\n",
					reply[1], n > 2 ? reply[2] : 0);
				if (reply[1] == fence)
					return 1;
				break;
			default:
				fprintf(stderr, "msvdx: message id %02x:",
					(reply[0] >> 8) & 0xff);
				for (int i = 0; i < n && i < 8; i++)
					fprintf(stderr, " %08x", reply[i]);
				fprintf(stderr, "\n");
				if (((reply[0] >> 8) & 0xff) >= VA_MSGID_CMD_UNSUPPORTED)
					return 1;
				break;
		}
	}
}


int
psb_context_flush_cmdbuf(object_context_p obj_context)
{
	psb_cmdbuf_p cmdbuf = obj_context->cmdbuf;
	uint32_t last = 0;
	int i;
	int ret = 0;

	if (cmdbuf == NULL || cmdbuf->cmd_count == 0)
		return 0;
	msvdx_flush(&cmdbuf->buf.mb, 0, cmdbuf->buf.mb.size);
	for (i = 0; i < cmdbuf->cmd_count; i++) {
		uint32_t* msg = cmdbuf->msgs[i];
		/* Only the last message of a batch asks for an answer. */
		msg[7] |= (i == cmdbuf->cmd_count - 1)
			? FW_DXVA_RENDER_HOST_INT : FW_DXVA_RENDER_NO_RESPONCE_MSG;
		if (obj_context->video_op == psb_video_vld)
			msg[7] |= FW_DXVA_RENDER_IS_VLD_NOT_MC;
		msg[1] = msvdx_mmu_ptd();
		msg[4] = last = sFence++;
		if (psb_verbose > 1) {
			for (int w = 0; w < 8; w++)
				printf("msg[%d] = %08x\n", w, msg[w]);
		}
		if (msvdx_send(msg) != 0) {
			ret = 1;
			break;
		}
	}
	if (ret == 0)
		ret = wait_for_fence(last);
	cmdbuf->cmd_count = 0;
	obj_context->cmdbuf = NULL;
	return ret;
}


int
psb_cmdbuf_second_pass(object_context_p obj_context, uint32_t OperatingModeCmd,
	unsigned char* pvParamBase, uint32_t PicWidthInMbs,
	uint32_t FrameHeightInMbs, psb_buffer_p target_buf, uint32_t chroma_offset)
{
	fprintf(stderr, "msvdx: two-pass deblocking is not supported\n");
	return 1;
}


int
psb_context_submit_deblock(object_context_p obj_context,
	psb_buffer_p dst_buf, psb_buffer_p colocated_buf, uint32_t picture_widht_mb,
	uint32_t frame_height_mb, uint32_t chroma_offset)
{
	return 1;
}
