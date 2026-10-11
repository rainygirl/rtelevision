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

/* The command-buffer encoders from psb_video's psb_cmdbuf.c (Intel, 2010,
 * the Moorestown-era tree Wind River published under the licence above),
 * copied verbatim: LLDMA records, register-pair blocks, RENDEC blocks and
 * skip blocks, in the DE2 format this firmware speaks. What is not copied is
 * the DRM half -- relocations, buffer lists, the cmdbuf ioctl -- which
 * psb_cmdbuf_host.c replaces. */
#include "psb_shim.h"

#define CMD_SIZE		(0x3000)
#define LLDMA_SIZE		(0x2000)
#define CMD_END(cmdbuf)		((void *)(cmdbuf->lldma_base))
#define LLDMA_END(cmdbuf)	((void *)(cmdbuf->cmd_base + CMD_SIZE + LLDMA_SIZE))

typedef enum {
    MMU_GROUP0 = 0,
    MMU_GROUP1 = 1,
} MMU_GROUP;

typedef enum    {
    HOST_TO_MSVDX = 0,
    MSXDX_TO_HOST = 1,
} DMA_DIRECTION;

typedef struct {
	IMG_UINT32 ui32DevDestAddr ;	/* destination address */
	DMA_ePW	ePeripheralWidth;
	DMA_ePeriphIncrSize	ePeriphIncrSize;
	DMA_ePeriphIncr	ePeriphIncr;
	IMG_BOOL		bSynchronous;
	MMU_GROUP		eMMUGroup;
	DMA_DIRECTION	eDMADir;
	DMA_eBurst		eDMA_eBurst;
} DMA_DETAIL_LOOKUP;


static const DMA_DETAIL_LOOKUP DmaDetailLookUp[] =
{
	/* LLDMA_TYPE_VLC_TABLE */ { 	REG_MSVDX_VEC_VLC_OFFSET  ,
									DMA_PWIDTH_16_BIT,	/* 16 bit wide data*/
									DMA_PERIPH_INCR_4,	/* Incrament the dest by 32 bits */
									DMA_PERIPH_INCR_ON,
									IMG_TRUE,
									MMU_GROUP0,
									HOST_TO_MSVDX,
									DMA_BURST_2
								},
	/* LLDMA_TYPE_BITSTREAM */ {
									( REG_MSVDX_VEC_OFFSET + MSVDX_VEC_CR_VEC_SHIFTREG_STREAMIN_OFFSET  ),
									DMA_PWIDTH_8_BIT,
									DMA_PERIPH_INCR_1,
									DMA_PERIPH_INCR_OFF,
									IMG_FALSE,
									MMU_GROUP0,
									HOST_TO_MSVDX,
									DMA_BURST_4
								},
	/*LLDMA_TYPE_RESIDUAL*/		{
									(REG_MSVDX_VDMC_OFFSET + MSVDX_VDMC_CR_VDMC_RESIDUAL_DIRECT_INSERT_DATA_OFFSET),
									DMA_PWIDTH_32_BIT,
									DMA_PERIPH_INCR_1,	
									DMA_PERIPH_INCR_OFF,
									IMG_FALSE,
									MMU_GROUP1,
									HOST_TO_MSVDX,
									DMA_BURST_2
								},

	/*LLDMA_TYPE_RENDER_BUFF_MC*/{
									(REG_MSVDX_MTX_OFFSET + MTX_CORE_CR_MTX_SYSC_CDMAT_OFFSET ),
									DMA_PWIDTH_32_BIT,
									DMA_PERIPH_INCR_1,	
									DMA_PERIPH_INCR_OFF,
									IMG_TRUE,
									MMU_GROUP1,
									HOST_TO_MSVDX,
									DMA_BURST_1		/* Into MTX */
								},
	/*LLDMA_TYPE_RENDER_BUFF_VLD*/{
									(REG_MSVDX_MTX_OFFSET + MTX_CORE_CR_MTX_SYSC_CDMAT_OFFSET ),
									DMA_PWIDTH_32_BIT,
									DMA_PERIPH_INCR_1,	
									DMA_PERIPH_INCR_OFF,
									IMG_TRUE,
									MMU_GROUP0,
									HOST_TO_MSVDX,
									DMA_BURST_1		/* Into MTX */
								},
	/*LLDMA_TYPE_MPEG4_FESTATE_SAVE*/{
									(REG_MSVDX_VEC_RAM_OFFSET + 0x700 ),
									DMA_PWIDTH_32_BIT,
									DMA_PERIPH_INCR_4,	
									DMA_PERIPH_INCR_ON,
									IMG_TRUE,
									MMU_GROUP0,
									MSXDX_TO_HOST,
									DMA_BURST_2		 /* From VLR */
								},
	/*LLDMA_TYPE_MPEG4_FESTATE_RESTORE*/{
									(REG_MSVDX_VEC_RAM_OFFSET + 0x700 ),
									DMA_PWIDTH_32_BIT,
									DMA_PERIPH_INCR_4,	
									DMA_PERIPH_INCR_ON,
									IMG_TRUE,
									MMU_GROUP0,
									HOST_TO_MSVDX,
									DMA_BURST_2		/* Into VLR */
								},
	/*LLDMA_TYPE_H264_PRELOAD_SAVE*/{
									(REG_MSVDX_MTX_OFFSET + MTX_CORE_CR_MTX_SYSC_CDMAT_OFFSET ),
									DMA_PWIDTH_32_BIT,
									DMA_PERIPH_INCR_1,	
									DMA_PERIPH_INCR_OFF,
									IMG_TRUE,	/* na */
									MMU_GROUP1,
									MSXDX_TO_HOST,
									DMA_BURST_1		/* From MTX */
								},
	/*LLDMA_TYPE_H264_PRELOAD_RESTORE*/{
									(REG_MSVDX_MTX_OFFSET + MTX_CORE_CR_MTX_SYSC_CDMAT_OFFSET ),
									DMA_PWIDTH_32_BIT,
									DMA_PERIPH_INCR_1,	
									DMA_PERIPH_INCR_OFF,
									IMG_TRUE,	/* na */
									MMU_GROUP1,
									HOST_TO_MSVDX,
									DMA_BURST_1		/* Into MTX */
								},
	/*LLDMA_TYPE_VC1_PRELOAD_SAVE*/{
									(REG_MSVDX_MTX_OFFSET + MTX_CORE_CR_MTX_SYSC_CDMAT_OFFSET ),
									DMA_PWIDTH_32_BIT,
									DMA_PERIPH_INCR_1,	
									DMA_PERIPH_INCR_OFF,
									IMG_TRUE,	/* na */
									MMU_GROUP1,
									MSXDX_TO_HOST,
									DMA_BURST_1		//2	/* From MTX */
								},
	/*LLDMA_TYPE_VC1_PRELOAD_RESTORE*/{
									(REG_MSVDX_MTX_OFFSET + MTX_CORE_CR_MTX_SYSC_CDMAT_OFFSET ),
									DMA_PWIDTH_32_BIT,
									DMA_PERIPH_INCR_1,	
									DMA_PERIPH_INCR_OFF,
									IMG_TRUE,	/* na */
									MMU_GROUP1,
									HOST_TO_MSVDX,
									DMA_BURST_1		/* Into MTX */
								},
};

#define MAX_DMA_LEN     ( 0xffff )


static void psb_cmdbuf_lldma_create_internal( psb_cmdbuf_p cmdbuf, LLDMA_CMD *pLLDMACmd, psb_buffer_p bitstream_buf, uint32_t buffer_offset, uint32_t size, uint32_t dest_offset, LLDMA_TYPE cmd);


void psb_cmdbuf_lldma_write_cmdbuf( psb_cmdbuf_p cmdbuf,
                                 psb_buffer_p bitstream_buf,
                                 uint32_t buffer_offset,
                                 uint32_t size,
                                 uint32_t dest_offset,
                                 LLDMA_TYPE cmd)
{
    LLDMA_CMD *pLLDMACmd = (LLDMA_CMD*) cmdbuf->cmd_idx++;
    psb_cmdbuf_lldma_create_internal(cmdbuf, pLLDMACmd, bitstream_buf, buffer_offset, size,
                dest_offset, cmd);
}

uint32_t psb_cmdbuf_lldma_create( psb_cmdbuf_p cmdbuf,
                                 psb_buffer_p bitstream_buf,
                                 uint32_t buffer_offset,
                                 uint32_t size,
                                 uint32_t dest_offset,
                                 LLDMA_TYPE cmd)
{
    uint32_t lldma_record_offset = (((void*)cmdbuf->lldma_idx) - ((void *) cmdbuf->cmd_base));
    psb_cmdbuf_lldma_create_internal(cmdbuf, 0, bitstream_buf, buffer_offset, size,
                dest_offset, cmd);
    return lldma_record_offset;
}

void psb_cmdbuf_lldma_write_bitstream( psb_cmdbuf_p cmdbuf,
                                      psb_buffer_p bitstream_buf,
                                      uint32_t buffer_offset,
                                      uint32_t size_in_bytes,
                                      uint32_t offset_in_bits,
                                      uint32_t flags)
{
/*
 * We use byte alignment instead of 32bit alignment.
 * The third frame of sa10164.vc1 results in the following bitstream 
 * patttern:
 * [0000] 00 00 03 01 76 dc 04 8d
 * with offset_in_bits = 0x1e
 * This causes an ENTDEC failure because 00 00 03 is a start code
 * By byte aligning the datastream the start code will be eliminated.
 */
//don't need to change the offset_in_bits, size_in_bytes and buffer_offset
#if 0
#define ALIGNMENT	 sizeof(uint8_t)    
    uint32_t bs_offset_in_dwords    = ((offset_in_bits /8) / ALIGNMENT);
    size_in_bytes                   -= bs_offset_in_dwords * ALIGNMENT;
    offset_in_bits                  -= bs_offset_in_dwords * 8 * ALIGNMENT;
    buffer_offset                   += bs_offset_in_dwords * ALIGNMENT;
#endif

    *cmdbuf->cmd_idx++ = CMD_SR_SETUP | flags;
    *cmdbuf->cmd_idx++ = offset_in_bits;
    cmdbuf->cmd_bitstream_size = cmdbuf->cmd_idx;
    *cmdbuf->cmd_idx++ = size_in_bytes;
    
    psb_cmdbuf_lldma_write_cmdbuf( cmdbuf, bitstream_buf, buffer_offset, 
                            size_in_bytes, 0, LLDMA_TYPE_BITSTREAM );

#ifdef DEBUG_TRACE
    psb__debug_schedule_hexdump("Bitstream", bitstream_buf, buffer_offset, size_in_bytes);
#endif
}

void psb_cmdbuf_lldma_write_bitstream_chained( psb_cmdbuf_p cmdbuf,
                                      psb_buffer_p bitstream_buf,
                                      uint32_t size_in_bytes)
{
    DMA_sLinkedList* pasDmaList = (DMA_sLinkedList*) cmdbuf->lldma_last;
    uint32_t lldma_record_offset = psb_cmdbuf_lldma_create( cmdbuf, bitstream_buf, bitstream_buf->buffer_ofs, 
                            size_in_bytes, 0, LLDMA_TYPE_BITSTREAM );
    /* Update WD7 of last LLDMA record to point to this one */
    RELOC_SHIFT4(pasDmaList->ui32Word_7, lldma_record_offset, 0, &(cmdbuf->buf));
    /* This touches WD1 */
    MEMIO_WRITE_FIELD(pasDmaList, DMAC_LL_LIST_FIN, 0);

#ifdef DEBUG_TRACE
    psb__debug_schedule_hexdump("Bitstream (chained)", bitstream_buf, 0, size_in_bytes);
#endif

    *(cmdbuf->cmd_bitstream_size) += size_in_bytes;
}

static void psb_cmdbuf_lldma_create_internal( psb_cmdbuf_p cmdbuf,
                 LLDMA_CMD *pLLDMACmd,
                                 psb_buffer_p bitstream_buf,
                                 uint32_t buffer_offset,
                                 uint32_t size,
                                 uint32_t dest_offset,
                                 LLDMA_TYPE cmd)
{
    const DMA_DETAIL_LOOKUP* pDmaDetail;
    IMG_UINT32 ui32DMACount, ui32LLDMA_Offset, ui32DMADestAddr, ui32Cmd;
    DMA_sLinkedList* pasDmaList;
    static IMG_UINT32 lu[] = {4,2,1};

    /* See if we will fit */
    ASSERT( cmdbuf->lldma_idx + sizeof( DMA_sLinkedList ) < LLDMA_END(cmdbuf) );

    pDmaDetail = &DmaDetailLookUp[cmd];

    ui32DMACount = size / lu[pDmaDetail->ePeripheralWidth];

    /* DMA list must be 16byte alligned if it is done in Hw */
    pasDmaList = (DMA_sLinkedList*) (cmdbuf->lldma_idx) ;
    // psaDmaList = (DMA_sLinkedList*) ((( cmdbuf->lldma_idx )+0x0f) & ~0x0f );

    /* Offset of LLDMA record in cmdbuf */
    ui32LLDMA_Offset = (IMG_UINT32)(((IMG_UINT8*)pasDmaList) -((IMG_UINT8*) cmdbuf->cmd_base));

    ASSERT( 0 == (ui32LLDMA_Offset&0xf) );

    ui32DMADestAddr = pDmaDetail->ui32DevDestAddr + dest_offset;

    /* Write the header */
    if (pLLDMACmd)
    {
         ui32Cmd = ((pDmaDetail->bSynchronous) ? CMD_SLLDMA : CMD_LLDMA );
         RELOC_SHIFT4(pLLDMACmd->ui32CmdAndDevLinAddr, ui32LLDMA_Offset, ui32Cmd, &(cmdbuf->buf));
    }

    while( ui32DMACount )
    {
        memset( pasDmaList , 0 ,sizeof(DMA_sLinkedList) );

        DMA_LL_SET_WD2(pasDmaList, ui32DMADestAddr );    

        /* DMA_LL_SET_WD6 with relocation */
        ASSERT(DMAC_LL_SA_SHIFT == 0);

        RELOC(pasDmaList->ui32Word_6, buffer_offset, bitstream_buf);

        if( ui32DMACount > MAX_DMA_LEN )
        {    
            ui32LLDMA_Offset+=sizeof(DMA_sLinkedList);

            /* DMA_LL_SET_WD7 with relocation */
            ASSERT(DMAC_LL_LISTPTR_SHIFT == 0);
            RELOC_SHIFT4(pasDmaList->ui32Word_7, ui32LLDMA_Offset, 0, &(cmdbuf->buf));
            /* This touches WD1 */
            MEMIO_WRITE_FIELD(pasDmaList, DMAC_LL_LIST_FIN, 0);
            
            DMA_LL_SET_WD1(pasDmaList, pDmaDetail->ePeriphIncr, pDmaDetail->ePeriphIncrSize, MAX_DMA_LEN );    /* size */

            ui32DMACount-= MAX_DMA_LEN;

            if(  pDmaDetail->ePeriphIncr == DMA_PERIPH_INCR_ON )
            {
                /* Update Destination pointers */
                ui32DMADestAddr += ((MAX_DMA_LEN)* lu[pDmaDetail->ePeriphIncrSize] ); 
            }

            /* Update Source Pointer */
            buffer_offset += ((MAX_DMA_LEN)*lu[pDmaDetail->ePeripheralWidth]); 
        }
        else
        {
            /* This also set LIST_FIN in WD1 to 1*/
            DMA_LL_SET_WD7(pasDmaList, IMG_NULL);                // next linked list
            DMA_LL_SET_WD1(pasDmaList,pDmaDetail->ePeriphIncr, pDmaDetail->ePeriphIncrSize, ui32DMACount );    /* size */

            ui32DMACount =0;
        }

        /* Keep pointer in case we need to chain another LLDMA command */
        cmdbuf->lldma_last = (void *) pasDmaList;

        DMA_LL_SET_WD0(pasDmaList, DMA_BSWAP_NO_SWAP, 
            (pDmaDetail->eDMADir==HOST_TO_MSVDX)?DMA_DIR_MEM_TO_PERIPH:DMA_DIR_PERIPH_TO_MEM ,
            pDmaDetail->ePeripheralWidth);

        DMA_LL_SET_WD3(pasDmaList, DMA_ACC_DEL_0, pDmaDetail->eDMA_eBurst, pDmaDetail->eMMUGroup );
        DMA_LL_SET_WD4(pasDmaList, DMA_MODE_2D_OFF, 0);    // 2d
        DMA_LL_SET_WD5(pasDmaList, 0, 0);                    // 2d


        pasDmaList++;
    }

    /* there can be up to 3 Bytes of padding after header */
    cmdbuf->lldma_idx    = (void *)pasDmaList;
}

void psb_cmdbuf_reg_start_block( psb_cmdbuf_p cmdbuf )
{
    ASSERT(NULL == cmdbuf->rendec_block_start); /* Can't have both */

    cmdbuf->reg_start = cmdbuf->cmd_idx++;
}

void psb_cmdbuf_reg_set_address( psb_cmdbuf_p cmdbuf, 
                                 uint32_t reg,
                                 psb_buffer_p buffer,
                                 uint32_t buffer_offset )
{
    *cmdbuf->cmd_idx++ = reg;
    RELOC(*cmdbuf->cmd_idx++, buffer_offset, buffer);
}

void psb_cmdbuf_reg_end_block( psb_cmdbuf_p cmdbuf )
{
    uint32_t reg_count = ((cmdbuf->cmd_idx - cmdbuf->reg_start) - 1) / 2;
    
    *cmdbuf->reg_start = CMD_REGVALPAIR_WRITE | reg_count;
    cmdbuf->reg_start = NULL;
}

typedef enum
{
    MTX_CTRL_HEADER = 0,
    RENDEC_SL_HDR,
    RENDEC_SL_NULL,
    RENDEC_CK_HDR,
} RENDEC_CHUNK_OFFSETS;

void psb_cmdbuf_rendec_start_block( psb_cmdbuf_p cmdbuf )
{
    ASSERT(NULL == cmdbuf->rendec_block_start); /* Can't have both */
    cmdbuf->rendec_block_start = cmdbuf->cmd_idx;

    cmdbuf->rendec_block_start[RENDEC_SL_HDR] = 0;
    REGIO_WRITE_FIELD_LITE (cmdbuf->rendec_block_start[RENDEC_SL_HDR], RENDEC_SLICE_INFO, SL_HDR_CK_START, SL_ROUTING_INFO,      1);
    REGIO_WRITE_FIELD_LITE (cmdbuf->rendec_block_start[RENDEC_SL_HDR], RENDEC_SLICE_INFO, SL_HDR_CK_START, SL_ENCODING_METHOD,   3);
    REGIO_WRITE_FIELD_LITE (cmdbuf->rendec_block_start[RENDEC_SL_HDR], RENDEC_SLICE_INFO, SL_HDR_CK_START, SL_NUM_SYMBOLS_LESS1, 1);

    cmdbuf->rendec_block_start[RENDEC_SL_NULL] = 0; /* empty */

    cmdbuf->cmd_idx += RENDEC_CK_HDR;
}

void psb_cmdbuf_rendec_start_chunk( psb_cmdbuf_p cmdbuf, uint32_t dest_address )
{
    ASSERT(NULL != cmdbuf->rendec_block_start); /* Must have a RENDEC block open */
    cmdbuf->rendec_chunk_start = cmdbuf->cmd_idx++;

    *cmdbuf->rendec_chunk_start = 0;
    REGIO_WRITE_FIELD_LITE(*cmdbuf->rendec_chunk_start, RENDEC_SLICE_INFO, CK_HDR, CK_ENCODING_METHOD, 3);
    REGIO_WRITE_FIELD_LITE(*cmdbuf->rendec_chunk_start, RENDEC_SLICE_INFO, CK_HDR, CK_START_ADDRESS, ( dest_address >> 2));
}

void psb_cmdbuf_rendec_write_block( psb_cmdbuf_p cmdbuf, 
                                    unsigned char *block,
                                    uint32_t size )
{
    ASSERT((size & 0x3) == 0);
    int i;
    for( i = 0; i < size; i += 4)
    {
        uint32_t val = block[i] | (block[i+1] << 8) | (block[i+2] << 16) | (block[i+3] << 24);
        psb_cmdbuf_rendec_write( cmdbuf, val );
    }
}

void psb_cmdbuf_rendec_write_address( psb_cmdbuf_p cmdbuf, 
                                      psb_buffer_p buffer,
                                      uint32_t buffer_offset )
{
    RELOC(*cmdbuf->cmd_idx++, buffer_offset, buffer);
}

void psb_cmdbuf_rendec_end_chunk( psb_cmdbuf_p cmdbuf )
{
    ASSERT(NULL != cmdbuf->rendec_block_start); /* Must have an open RENDEC block */
    ASSERT(NULL != cmdbuf->rendec_chunk_start); /* Must have an open RENDEC chunk */
    uint32_t dword_count = (cmdbuf->cmd_idx - cmdbuf->rendec_chunk_start) - 1;

    REGIO_WRITE_FIELD_LITE (*cmdbuf->rendec_chunk_start,
                RENDEC_SLICE_INFO,
                CK_HDR,
                CK_NUM_SYMBOLS_LESS1,
                (2 * dword_count) - 1);        /* Number of 16-bit symbols, minus 1.*/

    cmdbuf->rendec_chunk_start = NULL;
}

void psb_cmdbuf_rendec_end_block( psb_cmdbuf_p cmdbuf )
{
    ASSERT(NULL != cmdbuf->rendec_block_start); /* Must have an open RENDEC block */
    ASSERT(NULL == cmdbuf->rendec_chunk_start); /* All chunks must be closed */

    uint32_t block_size = cmdbuf->cmd_idx - cmdbuf->rendec_block_start;  /* Include separator but not mtx block header*/

    /* Write separator (footer-type thing)    */    
    *cmdbuf->cmd_idx = 0;
    REGIO_WRITE_FIELD( *cmdbuf->cmd_idx, RENDEC_SLICE_INFO, SLICE_SEPARATOR, SL_SEP_SUFFIX, 7); 
    cmdbuf->cmd_idx++;

    /* Write CMD Header    */
    cmdbuf->rendec_block_start[MTX_CTRL_HEADER] = CMD_RENDEC_WRITE | block_size;

    cmdbuf->rendec_block_start = NULL;
}

void psb_cmdbuf_skip_start_block( psb_cmdbuf_p cmdbuf, uint32_t skip_condition )
{
    ASSERT(NULL == cmdbuf->rendec_block_start); /* Can't be inside a rendec block */
    ASSERT(NULL == cmdbuf->reg_start); /* Can't be inside a reg block */
    ASSERT(NULL == cmdbuf->skip_block_start); /* Can't be inside another skip block (limitation of current sw design)*/
    
    cmdbuf->skip_condition = skip_condition;
    cmdbuf->skip_block_start = cmdbuf->cmd_idx++;
}

void psb_cmdbuf_skip_end_block( psb_cmdbuf_p cmdbuf )
{
    ASSERT(NULL == cmdbuf->rendec_block_start); /* Rendec block must be closed */
    ASSERT(NULL == cmdbuf->reg_start); /* Reg block must be closed */
    ASSERT(NULL != cmdbuf->skip_block_start); /* Skip block must still be open */

    uint32_t block_size = cmdbuf->cmd_idx - (cmdbuf->skip_block_start + 1);

    *cmdbuf->skip_block_start = CMD_CONDITIONAL_SKIP | (cmdbuf->skip_condition << 20 ) | block_size;
    cmdbuf->skip_block_start = NULL;
}
