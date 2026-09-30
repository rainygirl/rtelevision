/* The psb_video hardware definitions, included the way psb_cmdbuf.c does.
 * img_defs.h refuses any OS it does not know; it only needs the GNU C
 * branch, which it keys on __linux__. Defined for these headers only, after
 * the Haiku system headers are already in. */
#ifndef MSVDX_HW_H
#define MSVDX_HW_H

#include <OS.h>
#include <stdint.h>

#ifndef __linux__
#define __linux__ 1
#define LINUX 1
#define MSVDX_UNDEF_LINUX
#endif

#include <assert.h>
#ifndef IMG_ASSERT
#define IMG_ASSERT(x) assert(x)
#endif

#include "hwdefs/img_types.h"
#include "hwdefs/mem_io.h"
#include "hwdefs/msvdx_offsets.h"
#include "hwdefs/dma_api.h"
#include "hwdefs/reg_io2.h"
#include "hwdefs/msvdx_defs.h"
#include "hwdefs/msvdx_vec_reg_io2.h"
#include "hwdefs/msvdx_vdmc_reg_io2.h"
#include "hwdefs/msvdx_mtx_reg_io2.h"
#include "hwdefs/msvdx_dmac_linked_list.h"
#include "hwdefs/msvdx_rendec_mtx_slice_cntrl_reg_io2.h"
#include "hwdefs/dxva_cmdseq_msg.h"
#include "hwdefs/dxva_fw_ctrl.h"
#include "hwdefs/dxva_fw_flags.h"
#include "hwdefs/fwrk_msg_mem_io.h"
#include "hwdefs/dxva_msg.h"
#include "hwdefs/msvdx_cmds_io2.h"

#ifdef MSVDX_UNDEF_LINUX
#undef __linux__
#undef LINUX
#undef MSVDX_UNDEF_LINUX
#endif

#endif
