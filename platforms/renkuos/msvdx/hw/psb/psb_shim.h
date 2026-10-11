/* What psb_H264.c (psb_video, 2010, MIT) expects of the driver around it,
 * reduced to this prototype: no libva, no DRM, no wsbm. Buffers are
 * msvdx_buf (contiguous memory mapped into the decoder's MMU), surfaces are
 * NV12 in one such buffer, and a "context" is one decoder instance.
 *
 * psb_def.h, psb_surface.h, psb_cmdbuf.h and psb_H264.h all include this
 * file, so psb_H264.c compiles unchanged. */
#ifndef PSB_SHIM_H
#define PSB_SHIM_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../msvdx.h"
#include "../hw.h"
#include "../va_h264.h"

/* ---- libva odds and ends ------------------------------------------------ */

typedef int VAStatus;
#define VA_STATUS_SUCCESS						0x00000000
#define VA_STATUS_ERROR_ALLOCATION_FAILED		0x00000002
#define VA_STATUS_ERROR_INVALID_CONFIG			0x00000004
#define VA_STATUS_ERROR_INVALID_CONTEXT			0x00000005
#define VA_STATUS_ERROR_INVALID_SURFACE			0x00000006
#define VA_STATUS_ERROR_UNSUPPORTED_PROFILE		0x0000000c
#define VA_STATUS_ERROR_ATTR_NOT_SUPPORTED		0x00000010
#define VA_STATUS_ERROR_RESOLUTION_NOT_SUPPORTED	0x00000013
#define VA_STATUS_ERROR_UNKNOWN					0xFFFFFFFF

typedef unsigned int VAContextID;
typedef unsigned int VAConfigID;

typedef enum {
	VAProfileH264Baseline = 5,
	VAProfileH264Main = 6,
	VAProfileH264High = 7,
	VAProfileH264ConstrainedBaseline = 13
} VAProfile;

typedef enum { VAEntrypointVLD = 1 } VAEntrypoint;

typedef enum { VAConfigAttribRTFormat = 0 } VAConfigAttribType;
typedef struct {
	VAConfigAttribType type;
	uint32_t value;
} VAConfigAttrib;

typedef enum {
	VAPictureParameterBufferType = 0,
	VAIQMatrixBufferType = 1,
	VASliceGroupMapBufferType = 3,
	VASliceParameterBufferType = 4,
	VASliceDataBufferType = 5,
	VAProtectedSliceDataBufferType = 0x7f000000
} VABufferType;

/* ---- psb_def.h ------------------------------------------------------------ */

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define ASSERT(x) do { if (!(x)) fprintf(stderr, "ASSERT %s:%d: %s\n", \
	__FILE__, __LINE__, #x); } while (0)
#define DEBUG_FAILURE do { if (vaStatus != VA_STATUS_SUCCESS) \
	fprintf(stderr, "failure %d at %s:%d\n", vaStatus, __FILE__, __LINE__); \
	} while (0)
#define DEBUG_FAILURE_RET do { if (ret) fprintf(stderr, \
	"failure %d at %s:%d\n", ret, __FILE__, __LINE__); } while (0)

extern int psb_verbose;
#define psb__information_message(...) \
	do { if (psb_verbose) printf(__VA_ARGS__); } while (0)
#define psb__trace_message(...) \
	do { if (psb_verbose > 1) printf(__VA_ARGS__); } while (0)
#define psb__error_message(...) fprintf(stderr, __VA_ARGS__)

/* ---- psb_buffer.h ---------------------------------------------------------- */

typedef enum {
	psb_bt_cpu_vpu = 0,
	psb_bt_vpu_only,
	psb_bt_surface
} psb_buffer_type_t;

typedef struct psb_driver_data_s* psb_driver_data_p;

struct psb_buffer_s {
	msvdx_buf mb;
	psb_buffer_type_t type;
	unsigned int buffer_ofs;
};
typedef struct psb_buffer_s* psb_buffer_p;

VAStatus psb_buffer_create(psb_driver_data_p driver_data, unsigned int size,
	psb_buffer_type_t type, psb_buffer_p buf);
void psb_buffer_destroy(psb_buffer_p buf);
int psb_buffer_map(psb_buffer_p buf, void* address);
int psb_buffer_unmap(psb_buffer_p buf);

/* ---- psb_surface.h --------------------------------------------------------- */

typedef enum {
	STRIDE_352 = 0,
	STRIDE_720 = 1,
	STRIDE_1280 = 2,
	STRIDE_1920 = 3,
	STRIDE_512 = 4,
	STRIDE_1024 = 5,
	STRIDE_2048 = 6,
	STRIDE_4096 = 7,
	STRIDE_NA,
	STRIDE_UNDEFINED
} psb_surface_stride_t;

struct psb_surface_s {
	struct psb_buffer_s buf;
	psb_surface_stride_t stride_mode;
	int stride;
	unsigned int luma_offset;
	unsigned int chroma_offset;
	int extra_info[6];
	int size;
};
typedef struct psb_surface_s* psb_surface_p;

VAStatus psb_surface_create(psb_driver_data_p driver_data, int width,
	int height, psb_surface_p surface);
void psb_surface_destroy(psb_surface_p surface);
VAStatus psb_surface_set_chroma(psb_surface_p surface, int value);
VAStatus psb_surface_sync(psb_surface_p surface);

/* ---- psb_drv_video.h ------------------------------------------------------- */

typedef struct object_surface_s {
	VASurfaceID surface_id;
	int width;
	int height;
	psb_surface_p psb_surface;
} *object_surface_p;

typedef struct object_buffer_s {
	VABufferType type;
	void* buffer_data;
	unsigned int size;
	unsigned int num_elements;
	psb_buffer_p psb_buffer;
} *object_buffer_p;

typedef struct object_config_s {
	VAProfile profile;
	int attrib_count;
	VAConfigAttrib attrib_list[4];
} *object_config_p;

#define PSB_MAX_SURFACES 24
struct object_heap_s {
	object_surface_p surfaces[PSB_MAX_SURFACES];
};
void* object_heap_lookup(struct object_heap_s* heap, int id);

struct psb_driver_data_s {
	struct object_heap_s surface_heap;
	uint32_t msvdx_context_base;
};

typedef enum {
	psb_video_none = 0,
	psb_video_vld,
	psb_video_mc
} psb_video_op_t;

typedef struct psb_cmdbuf_s* psb_cmdbuf_p;

typedef struct object_context_s {
	psb_driver_data_p driver_data;
	int picture_width;
	int picture_height;
	int num_render_targets;
	object_surface_p current_render_target;
	void* format_data;
	psb_cmdbuf_p cmdbuf;

	/* Filled in by the format code for the render message */
	psb_video_op_t video_op;
	uint32_t operating_mode;
	uint32_t flags;
	uint32_t first_mb;
	uint32_t last_mb;
	uint32_t msvdx_context;
} *object_context_p;

struct format_vtable_s {
	void (*queryConfigAttributes)(VAProfile profile, VAEntrypoint entrypoint,
		VAConfigAttrib* attrib_list, int num_attribs);
	VAStatus (*validateConfig)(object_config_p obj_config);
	VAStatus (*createContext)(object_context_p obj_context,
		object_config_p obj_config);
	void (*destroyContext)(object_context_p obj_context);
	VAStatus (*beginPicture)(object_context_p obj_context);
	VAStatus (*renderPicture)(object_context_p obj_context,
		object_buffer_p* buffers, int num_buffers);
	VAStatus (*endPicture)(object_context_p obj_context);
};

/* ---- psb_cmdbuf.h ---------------------------------------------------------- */

struct psb_cmdbuf_s {
	struct psb_buffer_s buf;	/* CMD_SIZE of commands, then LLDMA_SIZE */
	uint8_t* cmd_base;
	uint8_t* cmd_start;
	uint32_t* cmd_idx;
	uint32_t* cmd_bitstream_size;
	uint8_t* lldma_base;
	uint8_t* lldma_idx;
	void* lldma_last;

	uint32_t msgs[16][8];	/* FW_VA_RENDER messages waiting to be sent */
	int cmd_count;

	uint32_t* reg_start;
	uint32_t* rendec_block_start;
	uint32_t* rendec_chunk_start;
	uint32_t* last_next_segment_cmd;
	uint32_t first_segment_size;
	uint32_t* skip_block_start;
	uint32_t skip_condition;
};

typedef enum {
	SKIP_ON_CONTEXT_SWITCH = 1
} E_SKIP_CONDITION;

void psb_cmdbuf_add_relocation(psb_cmdbuf_p cmdbuf, uint32_t* addr_in_cmdbuf,
	psb_buffer_p ref_buffer, uint32_t buf_offset, uint32_t mask,
	uint32_t background, uint32_t align_shift, uint32_t dst_buffer);

#define RELOC(dest, offset, buf) psb_cmdbuf_add_relocation(cmdbuf, \
	(uint32_t*)&dest, buf, offset, 0XFFFFFFFF, 0, 0, 1)
#define RELOC_MSG(dest, offset, buf) psb_cmdbuf_add_relocation(cmdbuf, \
	(uint32_t*)&dest, buf, offset, 0XFFFFFFFF, 0, 0, 0)
#define RELOC_SHIFT4(dest, offset, background, buf) \
	psb_cmdbuf_add_relocation(cmdbuf, (uint32_t*)&dest, buf, offset, \
		0X0FFFFFFF, background, 4, 1)

int psb_cmdbuf_buffer_ref(psb_cmdbuf_p cmdbuf, psb_buffer_p buf);

void psb_cmdbuf_reg_start_block(psb_cmdbuf_p cmdbuf);
#define psb_cmdbuf_reg_set(cmdbuf, reg, val) \
	do { *cmdbuf->cmd_idx++ = reg; *cmdbuf->cmd_idx++ = val; } while (0)
void psb_cmdbuf_reg_set_address(psb_cmdbuf_p cmdbuf, uint32_t reg,
	psb_buffer_p buffer, uint32_t buffer_offset);
void psb_cmdbuf_reg_end_block(psb_cmdbuf_p cmdbuf);

void psb_cmdbuf_rendec_start_block(psb_cmdbuf_p cmdbuf);
void psb_cmdbuf_rendec_start_chunk(psb_cmdbuf_p cmdbuf, uint32_t dest_address);
#define psb_cmdbuf_rendec_write(cmdbuf, val) \
	do { *cmdbuf->cmd_idx++ = val; } while (0)
void psb_cmdbuf_rendec_write_block(psb_cmdbuf_p cmdbuf, unsigned char* block,
	uint32_t size);
void psb_cmdbuf_rendec_write_address(psb_cmdbuf_p cmdbuf, psb_buffer_p buffer,
	uint32_t buffer_offset);
void psb_cmdbuf_rendec_end_chunk(psb_cmdbuf_p cmdbuf);
void psb_cmdbuf_rendec_end_block(psb_cmdbuf_p cmdbuf);

void psb_cmdbuf_skip_start_block(psb_cmdbuf_p cmdbuf, uint32_t skip_condition);
void psb_cmdbuf_skip_end_block(psb_cmdbuf_p cmdbuf);

#include "../hwdefs/lldma_defs.h"
void psb_cmdbuf_lldma_write_cmdbuf(psb_cmdbuf_p cmdbuf,
	psb_buffer_p bitstream_buf, uint32_t buffer_offset, uint32_t size,
	uint32_t dest_offset, LLDMA_TYPE cmd);
uint32_t psb_cmdbuf_lldma_create(psb_cmdbuf_p cmdbuf,
	psb_buffer_p bitstream_buf, uint32_t buffer_offset, uint32_t size,
	uint32_t dest_offset, LLDMA_TYPE cmd);
void psb_cmdbuf_lldma_write_bitstream(psb_cmdbuf_p cmdbuf,
	psb_buffer_p bitstream_buf, uint32_t buffer_offset,
	uint32_t size_in_bytes, uint32_t offset_in_bits, uint32_t flags);
void psb_cmdbuf_lldma_write_bitstream_chained(psb_cmdbuf_p cmdbuf,
	psb_buffer_p bitstream_buf, uint32_t size_in_bytes);

int psb_context_get_next_cmdbuf(object_context_p obj_context);
int psb_context_submit_cmdbuf(object_context_p obj_context);
int psb_context_flush_cmdbuf(object_context_p obj_context);

/* Frees the shared command buffer; call before msvdx_close(). */
void psb_host_shutdown(void);

/* Two-pass deblocking (slice groups) is not supported here. */
int psb_cmdbuf_second_pass(object_context_p obj_context,
	uint32_t OperatingModeCmd, unsigned char* pvParamBase,
	uint32_t PicWidthInMbs, uint32_t FrameHeightInMbs,
	psb_buffer_p target_buf, uint32_t chroma_offset);
int psb_context_submit_deblock(object_context_p obj_context,
	psb_buffer_p dst_buf, psb_buffer_p colocated_buf, uint32_t picture_widht_mb,
	uint32_t frame_height_mb, uint32_t chroma_offset);

/* ---- psb_H264.h ------------------------------------------------------------ */

extern struct format_vtable_s psb_H264_vtable;

#endif
