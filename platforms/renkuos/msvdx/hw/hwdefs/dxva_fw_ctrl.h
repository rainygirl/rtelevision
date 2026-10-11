/*
 * Copyright (c) 2007 Intel Corporation. All Rights Reserved.
 * Copyright (c) Imagination Technologies Limited, UK  
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/******************************************************************************

 @File         dxva_fw_ctrl.h

 @Title        Dxva Firmware Control Allocation Commands

 @Platform     

 @Description  Defined commands that may be placed int the Control Allocation

******************************************************************************/
#ifndef _DXVA_FW_CTRL_H_
#define _DXVA_FW_CTRL_H_


#define CMD_MASK						(0xF0000000)

/* No Operation */
#define CMD_NOP							(0x00000000)

/* Register Value Pair Block */
#define CMD_REGVALPAIR_WRITE			(0x10000000)
#define CMD_REGVALPAIR_COUNT_MASK		(0x000FFFFF)
#define CMD_REGVALPAIR_COUNT_SHIFT		(0)

#define CMD_REGVALPAIR_FLAG_MB_LAYER	(0x00100000)
#define CMD_REGVALPAIR_FLAG_HL_LAYER	(0x00200000)
#define CMD_REGVALPAIR_FLAG_PRELOAD		(0x00400000)



#define CMD_REGVALPAIR_FORCE_MASK		(0x08000000)

/* Rendec Write Block */
#define CMD_RENDEC_WRITE				(0x20000000)
#define CMD_RENDEC_COUNT_MASK			(0x000FFFFF)
#define CMD_RENDEC_COUNT_SHIFT			(0)

/* Command Allocation temination Commands */
#define CMD_COMPLETION					(0x60000000)

/* Use this to notify mxt of the context */
#define CMD_HEADER					(0x70000000)
#define CMD_HEADER_CONTEXT_MASK		(0x0fffffff)

#define CMD_CONDITIONAL_SKIP		(0x80000000)

#define CMD_HEADER_VC1				(0x90000000)


typedef struct _RENDER_BUFFER_HEADER_VC1_TAG
{
	IMG_UINT32 ui32Cmd;
	IMG_UINT32 ui32RangeMappingBase[2]; /* Store flags in bottom bits of [0] */
	IMG_UINT32 ui32SliceParams;
	union 
	{
		struct	_LLDMA_VC1_ 
		{
				IMG_UINT32		ui32PreloadSave;		
				IMG_UINT32		ui32PreloadRestore;
		}		LLDMA_VC1;
	}	ui32LLDMAPointers;			

} RENDER_BUFFER_HEADER_VC1;

typedef struct _RENDER_BUFFER_HEADER_TAG
{
	IMG_UINT32 ui32Cmd;
	IMG_UINT32 ui32Reserved; /* used as ui32SliceParams in MPEG4 */
	union 
	{
		struct	_LLDMA_MPEG4_ 
		{
				IMG_UINT32		ui32FEStatesSave;
				IMG_UINT32		ui32FEStatesRestore;
		}		LLDMA_MPEG4;

		struct _LLDMA_H264_
		{
				IMG_UINT32		ui32PreloadSave;		
				IMG_UINT32		ui32PreloadRestore;
	
		}		LLDMA_H264;
	}	ui32LLDMAPointers;			

} RENDER_BUFFER_HEADER;

/* Linked list DMA Command */
#define CMD_LLDMA					(0xA0000000)
#define CMD_SLLDMA					(0xC0000000)		/* Syncronose LLDMA */
typedef struct
{
	IMG_UINT32 ui32CmdAndDevLinAddr;
} LLDMA_CMD;

/* Shift Register Setup Command */
#define CMD_SR_SETUP				(0xB0000000)
#define CMD_ENABLE_RBDU_EXTRACTION		(0x00000001)
typedef struct
{
	IMG_UINT32 ui32Cmd;
	IMG_UINT32 ui32BitstreamOffsetBits;
	IMG_UINT32 ui32BitstreamSizeBytes;
} SR_SETUP_CMD;

/* Next Segment Command */
#define CMD_NEXT_SEG				(0xD0000000)	/* Also Syncronose */

#endif
