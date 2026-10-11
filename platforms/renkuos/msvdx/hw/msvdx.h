/* Userland access to the GMA500's video decoder (Imagination VXD370, MSVDX).
 *
 * A prototype: everything goes through /dev/misc/poke and runs in the calling
 * process. The register sequences follow Intel's psb kernel driver
 * (psb-kmp: psb_msvdxinit.c, psb_msvdx.c, psb_mmu.c); the message and command
 * formats come from the MIT-licensed psb_video headers in hwdefs/.
 */
#ifndef MSVDX_H
#define MSVDX_H

#include <OS.h>

typedef struct {
	area_id	area;
	uint8*	cpu;		/* CPU address */
	uint32	phys;		/* physical address of the first page */
	uint32	dev;		/* address the decoder sees, through its MMU */
	uint32	size;
} msvdx_buf;

/* Map the registers, soft-reset the core, set up the MMU and RENDEC, load
 * the firmware and wait for its signature. Returns 0 on success. */
int msvdx_open(const char* firmwarePath);
void msvdx_close(void);

/* Physically contiguous memory, mapped into the decoder's address space. */
int msvdx_alloc(msvdx_buf* buf, uint32 size);
void msvdx_free(msvdx_buf* buf);

/* Write CPU caches back to memory before the decoder reads a buffer, and
 * drop them before the CPU reads what the decoder wrote. */
void msvdx_flush(const msvdx_buf* buf, uint32 offset, uint32 size);

/* Physical address of the page directory, for FW_VA_RENDER's MMUPTD field.
 * Bit 0 asks the firmware to invalidate its TLB; set it on the first message
 * after the page tables changed. */
uint32 msvdx_mmu_ptd(void);

/* Put one message (its first byte is its size in bytes) on the host-to-MTX
 * ring and kick the MTX. */
int msvdx_send(const uint32* msg);

/* Take one message off the MTX-to-host ring. Returns its size in words, or 0
 * if none arrived within timeoutMs. */
int msvdx_receive(uint32* msg, int maxWords, int timeoutMs);

uint32 msvdx_read(uint32 offset);
/* An MTX core register (MTX_INTERNAL_REG(r, u)), e.g. 0x05 is the PC. */
uint32 msvdx_read_core_reg(uint32 reg);
void msvdx_dump_state(const char* label);

#endif
