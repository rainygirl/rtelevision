/* See msvdx.h. */
#include "msvdx.h"

#include <Drivers.h>
#include <PCI.h>
#include <poke.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PSB_MSVDX_OFFSET		0x50000

/* MTX and core registers (psb_msvdx.h) */
#define MTX_ENABLE				0x0000
#define MTX_KICKI				0x0088
#define MTX_RW_DATA				0x00f8
#define MTX_RW_REQUEST			0x00fc
#define MTX_RAM_DATA			0x0104
#define MTX_RAM_CONTROL			0x0108
#define MTX_RAM_STATUS			0x010c
#define MTX_SOFT_RESET			0x0200
#define MSVDX_CONTROL			0x0600
#define MSVDX_INT_STATUS		0x0608
#define MSVDX_INT_CLEAR			0x060c
#define MSVDX_HOST_INT_ENABLE	0x0610
#define MSVDX_MAN_CLK_ENABLE	0x0620
#define MSVDX_MMU_CONTROL0		0x0680
#define MSVDX_MTX_RAM_BANK		0x06f0
#define RENDEC_CONTROL0			0x0868
#define RENDEC_CONTROL1			0x086c
#define RENDEC_BUFFER_SIZE		0x0870
#define RENDEC_BASE_ADDR0		0x0874
#define RENDEC_BASE_ADDR1		0x0878
#define RENDEC_CONTEXT0			0x0950

#define SOFT_RESET_ALL			(0x00000100 | 0x00010000 | 0x00100000 \
									| 0x01000000 | 0x10000000)
#define CLK_ALL					0x7f
#define CLK_MINIMAL				0x41

/* Communication area */
#define COMMS					0x2cc0
#define COMMS_FW_STATUS			(COMMS - 0x10)
#define COMMS_MSG_COUNTER		(COMMS - 0x04)
#define COMMS_SIGNATURE			(COMMS + 0x00)
#define COMMS_TO_HOST_RD		(COMMS + 0x08)
#define COMMS_TO_HOST_WRT		(COMMS + 0x0c)
#define COMMS_TO_MTX_RD			(COMMS + 0x14)
#define COMMS_FLAGS				(COMMS + 0x18)
#define COMMS_TO_MTX_WRT		(COMMS + 0x1c)
#define COMMS_TO_HOST_BUF		(COMMS + 0x20)
#define NUM_WORDS_HOST_BUF		100
#define COMMS_TO_MTX_BUF		(COMMS_TO_HOST_BUF + (NUM_WORDS_HOST_BUF << 2))
#define NUM_WORDS_MTX_BUF		100
#define SIGNATURE_VALUE			0xa5a5a5a5

#define MTX_CODE_BASE			0x80900000
#define MTX_DATA_BASE			0x82880000
#define PC_START_ADDRESS		0x80900000
#define MTX_CORE_CODE_MEM		0x10
#define MTX_CORE_DATA_MEM		0x18
#define MTX_PC					((0 << 4) | 5)

#define FLAGS_MMU_NONOPT_INV	0x002
#define FLAGS_MMU_HW_INVAL		0x020
#define FLAGS_BRN23154			0x200
#define POULSBO_D1				0x6

#define RENDEC_A_SIZE			(2 * 1024 * 1024)
#define RENDEC_B_SIZE			(RENDEC_A_SIZE / 4)

/* MMU (psb_drv.h) */
#define PSB_PTE_VALID			0x0001
#define PSB_PTE_CACHED			0x0008
#define DEV_VA_BASE				0x01000000

struct msvdx_fw {
	uint32 ver;
	uint32 text_size;
	uint32 data_size;
	uint32 data_location;
};

static volatile uint8* sRegs;
static area_id sRegsArea = -1;
static int sPoke = -1;
static int sHwUp;	/* between a successful mapping in msvdx_open() and msvdx_close() */

/* One team at a time. Opening resets the whole block, so a second user --
 * R Television while R Chromium plays a video, say -- would wipe the first
 * one's decode mid-frame. A named port is the lock: the kernel deletes it
 * when its team dies, so a crash cannot leave the hardware claimed. */
#define OWNER_PORT_NAME "msvdx owner"
static port_id sOwner = -1;

static void
release_owner(void)
{
	if (sOwner >= 0)
		delete_port(sOwner);
	sOwner = -1;
}
static uint8 sRevision;

static area_id sPdArea = -1;
static uint32* sPd;
static uint32 sPdPhys;
static area_id sDummyArea = -1;
static uint32 sDummyPtPhys;
static uint32 sDummyPagePhys;
static area_id sPtArea[1024];
static uint32* sPt[1024];
static uint32 sNextDev = DEV_VA_BASE;
static int sPtdInvalidate = 1;

static msvdx_buf sRendecA;
static msvdx_buf sRendecB;


uint32
msvdx_read(uint32 off)
{
	return *(volatile uint32*)(sRegs + off);
}


static void
wr(uint32 off, uint32 v)
{
	*(volatile uint32*)(sRegs + off) = v;
}


static int
wait_for(uint32 off, uint32 value, uint32 mask)
{
	int i;
	for (i = 0; i < 1000; i++) {
		if ((msvdx_read(off) & mask) == value)
			return 0;
		snooze(100);
	}
	fprintf(stderr, "msvdx: timeout at %04lx: want %08lx mask %08lx, got "
		"%08lx\n", (unsigned long)off, (unsigned long)value,
		(unsigned long)mask, (unsigned long)msvdx_read(off));
	return 1;
}


/* Keep an area out of copy-on-write when the team forks.
 *
 * fork() copies every area that is not B_SHARED_AREA copy-on-write, which
 * write-protects the parent's pages. For the register mapping that is fatal:
 * the next register write faults on a present, read-only device page and the
 * kernel asserts (PANIC in X86VMTranslationMapPAE::Map, "(*entry &
 * X86_PAE_PTE_PRESENT) == 0", twice on 2026-09-30). libVLC forks for every
 * HTTP stream -- posix_spawn of the libproxy helper -- so R Television hit it
 * as soon as it played an HLS channel. For the buffers it would be quieter
 * and as bad: a page the decoder's MMU points at could be swapped for a copy.
 *
 * Userland cannot set B_SHARED_AREA, but vm_clone_area() sets it on the
 * source area and leaves it there, so cloning once and dropping the clone is
 * enough. */
static void
share_area(area_id area)
{
	void* address;
	area_id clone = clone_area("msvdx shared", &address, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, area);
	if (clone < 0) {
		fprintf(stderr, "msvdx: cannot share area %ld: %s\n", (long)area,
			strerror(clone));
		return;
	}
	delete_area(clone);
}


/* The physical page behind a locked virtual address. */
static uint32
physical_address(void* cpu)
{
	/* get_memory_map() is kernel-only; poke answers the same question. */
	mem_map_args args;
	memset(&args, 0, sizeof(args));
	args.signature = POKE_SIGNATURE;
	args.address = cpu;
	args.size = 1;	/* get_memory_map() of a zero-length range maps nothing */
	if (ioctl(sPoke, POKE_GET_PHYSICAL_ADDRESS, &args, sizeof(args)) < 0)
		return 0;
	return args.physical_address > 0xffffffffULL
		? 0 : (uint32)args.physical_address;
}


static area_id
alloc_contiguous(uint32 size, void** cpu, uint32* phys)
{
	area_id area = create_area("msvdx", cpu, B_ANY_ADDRESS,
		(size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1), B_CONTIGUOUS,
		B_READ_AREA | B_WRITE_AREA);
	if (area < 0) {
		fprintf(stderr, "msvdx: create_area(%lu, B_CONTIGUOUS): %s\n",
			(unsigned long)size, strerror(area));
		return area;
	}
	share_area(area);
	memset(*cpu, 0, size);	/* also faults the pages in */
	*phys = physical_address(*cpu);
	if (*phys == 0 || physical_address((uint8*)*cpu + size - 1)
			!= *phys + ((size - 1) & ~(B_PAGE_SIZE - 1))
				+ ((size - 1) & (B_PAGE_SIZE - 1))) {
		fprintf(stderr, "msvdx: physical address of %p is %08lx, of its "
			"last byte %08lx\n", *cpu, (unsigned long)*phys,
			(unsigned long)physical_address((uint8*)*cpu + size - 1));
		delete_area(area);
		return B_ERROR;
	}
	return area;
}


void
msvdx_flush(const msvdx_buf* buf, uint32 offset, uint32 size)
{
	uint8* p = buf->cpu + (offset & ~63u);
	uint8* end = buf->cpu + offset + size;
	for (; p < end; p += 64)
		__asm__ __volatile__("clflush (%0)" :: "r"(p) : "memory");
	__asm__ __volatile__("mfence" ::: "memory");
}


static int
mmu_init(void)
{
	void* cpu;
	uint32 i;

	sPdArea = alloc_contiguous(B_PAGE_SIZE, &cpu, &sPdPhys);
	if (sPdArea < 0)
		return 1;
	sPd = (uint32*)cpu;

	/* Page 0 is a page table whose every entry points at page 1, a dummy
	 * page. Every unmapped address resolves to the dummy page rather than
	 * to whatever physical page 0 holds. */
	sDummyArea = alloc_contiguous(2 * B_PAGE_SIZE, &cpu, &sDummyPtPhys);
	if (sDummyArea < 0)
		return 1;
	sDummyPagePhys = sDummyPtPhys + B_PAGE_SIZE;
	for (i = 0; i < 1024; i++)
		((uint32*)cpu)[i] = sDummyPagePhys | PSB_PTE_VALID;
	for (i = 0; i < 1024; i++) {
		sPd[i] = sDummyPtPhys | PSB_PTE_VALID;
		sPtArea[i] = -1;
	}
	{
		msvdx_buf tmp = { sDummyArea, (uint8*)cpu, sDummyPtPhys, 0,
			2 * B_PAGE_SIZE };
		msvdx_buf pd = { sPdArea, (uint8*)sPd, sPdPhys, 0, B_PAGE_SIZE };
		msvdx_flush(&tmp, 0, tmp.size);
		msvdx_flush(&pd, 0, pd.size);
	}
	return 0;
}


/* Point one decoder page at one physical page. */
static int
mmu_map_page(uint32 va, uint32 phys)
{
	uint32 pdi = va >> 22;
	uint32 pti = (va >> 12) & 0x3ff;
	if (sPtArea[pdi] < 0) {
		void* cpu;
		uint32 ptPhys;
		uint32 i;
		sPtArea[pdi] = alloc_contiguous(B_PAGE_SIZE, &cpu, &ptPhys);
		if (sPtArea[pdi] < 0)
			return 1;
		sPt[pdi] = (uint32*)cpu;
		for (i = 0; i < 1024; i++)
			sPt[pdi][i] = sDummyPagePhys | PSB_PTE_VALID;
		sPd[pdi] = ptPhys | PSB_PTE_VALID;
	}
	sPt[pdi][pti] = phys | PSB_PTE_VALID | PSB_PTE_CACHED;
	return 0;
}


/* Map `size` bytes of a locked area at `cpu` to decoder addresses from
 * `dev`, page by page. The decoder sees memory only through its own MMU, so
 * the pages need not be physically contiguous -- which matters: an hour after
 * boot Haiku's file cache has scattered free memory so thoroughly that no
 * 1 MB contiguous run is left, with 1.5 GB nominally free (measured
 * 2026-09-30), and a 720p surface is 1.4 MB. */
static int
mmu_map(uint32 dev, const uint8* cpu, uint32 size)
{
	uint32 off;
	for (off = 0; off < size; off += B_PAGE_SIZE) {
		uint32 phys = physical_address((void*)(cpu + off));
		if (phys == 0 || mmu_map_page(dev + off, phys) != 0)
			return 1;
	}
	/* The page tables are read by the decoder, not the CPU. */
	for (off = 0; off < 1024; off++) {
		if (sPtArea[off] >= 0) {
			msvdx_buf pt = { sPtArea[off], (uint8*)sPt[off], 0, 0,
				B_PAGE_SIZE };
			msvdx_flush(&pt, 0, B_PAGE_SIZE);
		}
	}
	{
		msvdx_buf pd = { sPdArea, (uint8*)sPd, sPdPhys, 0, B_PAGE_SIZE };
		msvdx_flush(&pd, 0, B_PAGE_SIZE);
	}
	sPtdInvalidate = 1;
	return 0;
}


int
msvdx_alloc(msvdx_buf* buf, uint32 size)
{
	void* cpu;
	size = (size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);
	/* Locked, so every page stays where the decoder's MMU points. */
	buf->area = create_area("msvdx", &cpu, B_ANY_ADDRESS, size, B_FULL_LOCK,
		B_READ_AREA | B_WRITE_AREA);
	if (buf->area < 0) {
		fprintf(stderr, "msvdx: create_area(%lu, B_FULL_LOCK): %s\n",
			(unsigned long)size, strerror(buf->area));
		return 1;
	}
	share_area(buf->area);
	memset(cpu, 0, size);
	buf->cpu = (uint8*)cpu;
	buf->phys = physical_address(cpu);
	buf->size = size;
	buf->dev = sNextDev;
	sNextDev += size + B_PAGE_SIZE;	/* a guard page between buffers */
	if (mmu_map(buf->dev, buf->cpu, size) != 0)
		return 1;
	msvdx_flush(buf, 0, size);
	return 0;
}


void
msvdx_free(msvdx_buf* buf)
{
	if (buf->area >= 0)
		delete_area(buf->area);
	buf->area = -1;
}


uint32
msvdx_mmu_ptd(void)
{
	uint32 ptd = sPdPhys | (sPtdInvalidate ? 1 : 0);
	sPtdInvalidate = 0;
	return ptd;
}


static int
upload(uint32 mem, uint32 bankSize, uint32 address, uint32 words,
	const uint32* data, int verify)
{
	uint32 saved = msvdx_read(MTX_RAM_CONTROL);
	uint32 bank = ~0u;
	uint32 i;
	int bad = 0;

	wait_for(MTX_RAM_STATUS, 1, 0xffffffff);
	for (i = 0; i < words; i++) {
		uint32 ramId = mem + address / bankSize;
		if (ramId != bank) {
			wr(MTX_RAM_CONTROL, (ramId << 20)
				| (((address >> 2) << 2) & 0x000ffffc) | (1 << 1)
				| (verify ? 1 : 0));
			bank = ramId;
		}
		address += 4;
		if (verify) {
			wait_for(MTX_RAM_STATUS, 1, 0xffffffff);
			if (msvdx_read(MTX_RAM_DATA) != data[i]) {
				bad = 1;
				break;
			}
		} else {
			wr(MTX_RAM_DATA, data[i]);
			wait_for(MTX_RAM_STATUS, 1, 0xffffffff);
		}
	}
	wr(MTX_RAM_CONTROL, saved);
	return bad;
}


static int
load_firmware(const char* path)
{
	struct msvdx_fw* fw;
	uint32* text;
	uint32* data;
	uint32 bankSize;
	size_t size;
	char* blob;
	FILE* f = fopen(path, "rb");
	int i;

	if (f == NULL)
		return 1;
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	blob = malloc(size);
	if (fread(blob, 1, size, f) != size) {
		fclose(f);
		return 1;
	}
	fclose(f);
	fw = (struct msvdx_fw*)blob;
	if (fw->ver != 2 || size != sizeof(*fw)
			+ 4 * (fw->text_size + fw->data_size)) {
		fprintf(stderr, "msvdx: not a version 2 msvdx_fw.bin\n");
		return 1;
	}
	text = (uint32*)(blob + sizeof(*fw));
	data = text + fw->text_size;

	wr(MTX_SOFT_RESET, 1);
	wr(COMMS_FLAGS, sRevision >= POULSBO_D1
		? FLAGS_MMU_HW_INVAL | FLAGS_BRN23154
		: FLAGS_MMU_NONOPT_INV | FLAGS_MMU_HW_INVAL | FLAGS_BRN23154);
	wr(COMMS_MSG_COUNTER, 0);
	wr(COMMS_SIGNATURE, 0);
	wr(COMMS_TO_HOST_RD, 0);
	wr(COMMS_TO_HOST_WRT, 0);
	wr(COMMS_TO_MTX_RD, 0);
	wr(COMMS_TO_MTX_WRT, 0);
	wr(COMMS_FW_STATUS, 0);

	bankSize = 1u << (((msvdx_read(MSVDX_MTX_RAM_BANK) & 0x000f0000) >> 16)
		+ 2);
	upload(MTX_CORE_CODE_MEM, bankSize, PC_START_ADDRESS - MTX_CODE_BASE,
		fw->text_size, text, 0);
	upload(MTX_CORE_DATA_MEM, bankSize, fw->data_location - MTX_DATA_BASE,
		fw->data_size, data, 0);
	if (upload(MTX_CORE_CODE_MEM, bankSize, PC_START_ADDRESS - MTX_CODE_BASE,
			fw->text_size, text, 1)
		|| upload(MTX_CORE_DATA_MEM, bankSize,
			fw->data_location - MTX_DATA_BASE, fw->data_size, data, 1)) {
		fprintf(stderr, "msvdx: firmware upload did not verify\n");
		free(blob);
		return 1;
	}
	free(blob);

	wr(MTX_RW_DATA, PC_START_ADDRESS);
	wr(MTX_RW_REQUEST, MTX_PC);
	wait_for(MTX_RW_REQUEST, 0x80000000, 0x80000000);
	wr(MTX_ENABLE, 1);

	for (i = 0; i < 2000 && msvdx_read(COMMS_SIGNATURE) != SIGNATURE_VALUE;
			i++)
		snooze(100);
	if (msvdx_read(COMMS_SIGNATURE) != SIGNATURE_VALUE) {
		fprintf(stderr, "msvdx: firmware did not come up\n");
		return 1;
	}
	return 0;
}


int
msvdx_open(const char* firmwarePath)
{
	pci_info info;
	pci_info_args args;
	mem_map_args mmio;
	int index;
	int i;

	if (find_port(OWNER_PORT_NAME) >= 0) {
		fprintf(stderr, "msvdx: in use by another application\n");
		return 1;
	}
	sOwner = create_port(1, OWNER_PORT_NAME);
	if (sOwner < 0)
		return 1;

	/* The registers are mapped once per team and kept until it exits;
	 * there is nothing to gain from unmapping them between videos. */
	if (sRegsArea < 0) {
		sPoke = open(POKE_DEVICE_FULLNAME, O_RDWR);
		if (sPoke < 0)
			return 1;
		args.signature = POKE_SIGNATURE;
		args.info = &info;
		for (index = 0; index < 255; index++) {
			args.index = index;
			if (ioctl(sPoke, POKE_GET_NTH_PCI_INFO, &args, sizeof(args))
					!= B_OK || args.status != B_OK)
				return 1;
			if (info.vendor_id == 0x8086 && info.device_id == 0x8108)
				break;
		}
		sRevision = info.revision;
		memset(&mmio, 0, sizeof(mmio));
		mmio.signature = POKE_SIGNATURE;
		mmio.name = "msvdx regs";
		mmio.physical_address = info.u.h0.base_registers[0];
		mmio.size = info.u.h0.base_register_sizes[0];
		mmio.flags = B_ANY_ADDRESS;
		mmio.protection = B_READ_AREA | B_WRITE_AREA;
		if (ioctl(sPoke, POKE_MAP_MEMORY, &mmio, sizeof(mmio)) < 0)
			return 1;
		sRegsArea = mmio.area;
		share_area(sRegsArea);
		sRegs = (volatile uint8*)mmio.address + PSB_MSVDX_OFFSET;
	}
	sHwUp = 1;

	/* psb_msvdx_reset(): stop whatever an earlier run left going. */
	wr(MTX_ENABLE, 0);
	wr(MSVDX_CONTROL, SOFT_RESET_ALL);
	if (wait_for(MSVDX_CONTROL, 0, 0x00000100) != 0) {
		fprintf(stderr, "msvdx: soft reset did not clear\n");
		return 1;
	}
	wr(MSVDX_HOST_INT_ENABLE, 0);
	wr(MSVDX_INT_CLEAR, 0xffffffff);

	sRendecA.area = sRendecB.area = -1;
	if (mmu_init() != 0) {
		fprintf(stderr, "msvdx: mmu_init failed\n");
		return 1;
	}

	/* psb_msvdx_init() */
	wr(MSVDX_MAN_CLK_ENABLE, CLK_ALL);
	wr(MSVDX_MMU_CONTROL0, 0);	/* remove the MMU bypass */

	if (msvdx_alloc(&sRendecA, RENDEC_A_SIZE) != 0
		|| msvdx_alloc(&sRendecB, RENDEC_B_SIZE) != 0) {
		fprintf(stderr, "msvdx: RENDEC allocation failed\n");
		return 1;
	}
	wr(RENDEC_BASE_ADDR0, sRendecA.dev);
	wr(RENDEC_BASE_ADDR1, sRendecB.dev);
	wr(RENDEC_BUFFER_SIZE, (RENDEC_A_SIZE / 4096)
		| ((RENDEC_B_SIZE / 4096) << 16));
	wr(RENDEC_CONTROL1, (1 << 24) /* external memory */
		| (1 << 18) /* burst write */ | (1 << 16) /* burst read */);
	for (i = 0; i < 6; i++)
		wr(RENDEC_CONTEXT0 + 4 * i, 0x00101010);
	wr(RENDEC_CONTROL0, 1);	/* initialise */

	if (load_firmware(firmwarePath) != 0)
		return 1;
	wr(MSVDX_MAN_CLK_ENABLE, CLK_MINIMAL);
	return 0;
}


void
msvdx_close(void)
{
	int i;

	if (!sHwUp) {
		release_owner();
		return;
	}
	sHwUp = 0;
	wr(MTX_ENABLE, 0);
	wr(MSVDX_CONTROL, SOFT_RESET_ALL);
	wait_for(MSVDX_CONTROL, 0, 0x00000100);
	wr(MSVDX_MMU_CONTROL0, 0x0f000000);	/* back to bypass, as the BIOS left it */
	wr(MSVDX_MAN_CLK_ENABLE, 0x01);

	/* Everything the decoder could address goes, so that a later
	 * msvdx_open() -- a browser opens one per video -- starts clean. */
	msvdx_free(&sRendecA);
	msvdx_free(&sRendecB);
	for (i = 0; i < 1024; i++) {
		if (sPtArea[i] >= 0)
			delete_area(sPtArea[i]);
		sPtArea[i] = -1;
		sPt[i] = NULL;
	}
	if (sPdArea >= 0)
		delete_area(sPdArea);
	if (sDummyArea >= 0)
		delete_area(sDummyArea);
	sPdArea = sDummyArea = -1;
	sPd = NULL;
	sNextDev = DEV_VA_BASE;
	sPtdInvalidate = 1;
	/* The register mapping and the poke descriptor stay; see msvdx_open(). */
	release_owner();
}


int
msvdx_send(const uint32* msg)
{
	uint32 words = ((msg[0] & 0xff) + 3) / 4;
	uint32 rd;
	uint32 wrIdx;
	uint32 i;

	wr(MSVDX_MAN_CLK_ENABLE, CLK_ALL);
	if (words == 0 || words > NUM_WORDS_MTX_BUF)
		return 1;
	rd = msvdx_read(COMMS_TO_MTX_RD);
	wrIdx = msvdx_read(COMMS_TO_MTX_WRT);
	if (wrIdx + words > NUM_WORDS_MTX_BUF) {
		/* Would wrap: pad to the end of the ring first. */
		uint32 pad = (NUM_WORDS_MTX_BUF - wrIdx) << 2;	/* size, id 0 */
		if (rd == 0)
			return 1;
		wr(COMMS_TO_MTX_BUF + (wrIdx << 2), pad);
		wrIdx = 0;
		wr(COMMS_TO_MTX_WRT, wrIdx);
		wr(MTX_KICKI, 1);
	}
	for (i = 0; i < words; i++) {
		wr(COMMS_TO_MTX_BUF + (wrIdx << 2), msg[i]);
		if (++wrIdx == NUM_WORDS_MTX_BUF)
			wrIdx = 0;
	}
	wr(COMMS_TO_MTX_WRT, wrIdx);
	wr(MTX_KICKI, 1);
	return 0;
}


int
msvdx_receive(uint32* msg, int maxWords, int timeoutMs)
{
	uint32 rd;
	uint32 words;
	uint32 i;
	int t;

	for (t = 0; t < timeoutMs * 10; t++) {
		if (msvdx_read(COMMS_TO_HOST_RD) != msvdx_read(COMMS_TO_HOST_WRT))
			break;
		snooze(100);
	}
	rd = msvdx_read(COMMS_TO_HOST_RD);
	if (rd == msvdx_read(COMMS_TO_HOST_WRT))
		return 0;
	msg[0] = msvdx_read(COMMS_TO_HOST_BUF + (rd << 2));
	words = ((msg[0] & 0xff) + 3) / 4;
	if (words == 0)
		words = 1;
	if (++rd >= NUM_WORDS_HOST_BUF)
		rd = 0;
	for (i = 1; i < words; i++) {
		uint32 v = msvdx_read(COMMS_TO_HOST_BUF + (rd << 2));
		if ((int)i < maxWords)
			msg[i] = v;
		if (++rd >= NUM_WORDS_HOST_BUF)
			rd = 0;
	}
	wr(COMMS_TO_HOST_RD, rd);
	return (int)words;
}


uint32
msvdx_read_core_reg(uint32 reg)
{
	/* MTX_RNW (bit 16) = 1: read. DREADY (bit 31) goes up when done. */
	wr(MTX_RW_REQUEST, reg | 0x00010000);
	if (wait_for(MTX_RW_REQUEST, 0x80000000, 0x80000000) != 0)
		return 0xdeadbeef;
	return msvdx_read(MTX_RW_DATA);
}


void
msvdx_dump_state(const char* label)
{
	printf("[%s] MTX_ENABLE %08lx INT_STATUS %08lx MMU0 %08lx CLK %08lx "
		"FW_STATUS %08lx to-mtx rd/wr %lu/%lu to-host rd/wr %lu/%lu\n", label,
		(unsigned long)msvdx_read(MTX_ENABLE),
		(unsigned long)msvdx_read(MSVDX_INT_STATUS),
		(unsigned long)msvdx_read(MSVDX_MMU_CONTROL0),
		(unsigned long)msvdx_read(MSVDX_MAN_CLK_ENABLE),
		(unsigned long)msvdx_read(COMMS_FW_STATUS),
		(unsigned long)msvdx_read(COMMS_TO_MTX_RD),
		(unsigned long)msvdx_read(COMMS_TO_MTX_WRT),
		(unsigned long)msvdx_read(COMMS_TO_HOST_RD),
		(unsigned long)msvdx_read(COMMS_TO_HOST_WRT));
}
