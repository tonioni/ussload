
/* Real hardware UAE state file loader */
/* Copyright 2019 Toni Wilen */

#define VER "2.0 BETA #5"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <proto/expansion.h>
#include <graphics/gfxbase.h>
#include <dos/dosextens.h>
#include <hardware/cia.h>
#include <hardware/custom.h>

#include "header.h"

extern struct GfxBase *GfxBase;
extern struct DosLibrary *DosBase;

static const UBYTE *const version = "$VER: ussload " VER " (" REVDATE ")";

static const char *const chunknames[] =
{
	"ASF ",
	"CPU ", "FPU ", "CHIP", "AGAC",
	"CIAA", "CIAB", "ROM ",
	"DSK0", "DSK1", "DSK2", "DSK3",
	"AUD0", "AUD1", "AUD2", "AUD3",
	"SPR0", "SPR1", "SPR2", "SPR3",
	"SPR4", "SPR5", "SPR6", "SPR7",
	"END ",
	NULL
};
static const char *const memchunknames[] =
{
	"CRAM", "BRAM", "FRAM",
	NULL
};
static const char *const unsupportedchunknames[] =
{
	"FRA2", "FRA3", "FRA4",
	"ZRA2", "ZRA3", "ZRA4",
	"ZCRM", "PRAM",
	"A3K1", "A3K2",
	"BORO", "P96 ",
	"FSYC",
	NULL
};

static ULONG getlong(UBYTE *chunk, int offset)
{
	ULONG v;
	
	chunk += offset;
	v = (chunk[0] << 24) | (chunk[1] << 16) | (chunk[2] << 8) | (chunk[3] << 0);
	return v;
}
static ULONG getword(UBYTE *chunk, int offset)
{
	ULONG v;
	
	chunk += offset;
	v = (chunk[0] << 8) | (chunk[1] << 0);
	return v;
}

static void set_agacolor(UBYTE *p)
{
	volatile struct Custom *c = (volatile struct Custom*)0xdff000;

	int aga = (c->vposr & 0x0f00) == 0x0300;
	if (!aga)
		return;
	
	for (int i = 0; i < 8; i++) {
		for (int k = 0; k < 2; k++) {
			c->bplcon3 = (i << 13) | (k ? (1 << 9) : 0);
			for (int j = 0; j < 32; j++) {
				ULONG c32 = getlong(p, (j + i * 32) * 4);
				if (!k)
					c32 >>= 4;
				// R1R2G1G2B1B2 -> R2G2B2
				UWORD col = ((c32 & 0x00000f) << 0) | ((c32 & 0x000f00) >> 4) | ((c32 & 0x0f0000) >> 8);
				if (!k && (c32 & 0x80000000))
					col |= 0x8000; // genlock transparency bit
				c->color[j] = col;
			}			
		}
	}
	c->bplcon3 = 0x0c00;
}

static void wait_lines(WORD lines)
{
	volatile struct Custom *c = (volatile struct Custom*)0xdff000;

	UWORD line = c->vhposr & 0xff00;
	while (lines-- > 0) {
		for (;;) {
			UWORD line2 = c->vhposr & 0xff00;
			if (line == line2)
				continue;
			line = line2;
			break;
		}
	}
}

static void step_floppy(void)
{
	volatile struct CIA *ciab = (volatile struct CIA*)0xbfd000;
	ciab->ciaprb &= ~CIAF_DSKSTEP;
	// delay
	ciab->ciaprb &= ~CIAF_DSKSTEP;
	ciab->ciaprb |= CIAF_DSKSTEP;
	wait_lines(200);
}

static void reset_floppy(struct uaestate *st)
{
	volatile struct CIA *ciab = (volatile struct CIA*)0xbfd000;

	// all drives motor off
	ciab->ciaprb = 0xff;
	// select all
	ciab->ciaprb &= ~(15 << 3);
	// deselect all
	ciab->ciaprb |= 15 << 3;
}

static void set_floppy(UBYTE *p, ULONG num, struct uaestate *st)
{
	ULONG id = getlong(p, 0);
	UBYTE state = p[4];
	UBYTE track = p[5];

	if ((st->flags & FLAGS_NOFLOPPY) || !p)
		return;

 	// drive disabled?
	if (state & 2)
		return;
	// invalid track?
	if (track >= 80)
		return;

	volatile struct CIA *ciaa = (volatile struct CIA*)0xbfe001;
	volatile struct CIA *ciab = (volatile struct CIA*)0xbfd000;

	ciab->ciaprb = 0xff;
	
	// motor on?
	if (state & 1) {
		ciab->ciaprb &= ~CIAF_DSKMOTOR;
	}
	// select drive
	ciab->ciaprb &= ~(CIAF_DSKSEL0 << num);

	wait_lines(100);
	int seekcnt = 80;
	while (seekcnt-- > 0) {
		if (!(ciaa->ciapra & CIAF_DSKTRACK0))
			break;
		step_floppy();
	}
	wait_lines(100);
	if (seekcnt <= 0) {
		// no track0 after 80 steps: drive missing or not responding
		ciab->ciaprb |= CIAF_DSKMOTOR;
		ciab->ciaprb |= CIAF_DSKSEL0 << num;
		return;
	}
	
	ciab->ciaprb &= ~CIAF_DSKDIREC;
	wait_lines(800);
	for (UBYTE i = 0; i < track; i++) {
		step_floppy();
	}

	ciab->ciaprb |= CIAF_DSKSEL0 << num;
}

// current AUDxLEN and AUDxPT
static void set_audio(UBYTE *p, ULONG num)
{
	volatile struct Custom *c = (volatile struct Custom*)0xdff000;
	ULONG l;

	if (!p)
		return;

	c->aud[num].ac_vol = p[1]; // AUDxVOL
	c->aud[num].ac_per = getword(p, 1 + 1 + 1 + 1 + 2 + 2 + 2); // AUDxPER
	c->aud[num].ac_len = getword(p, 1 + 1 + 1 + 1 + 2); // AUDxLEN
	l = getword(p, 1 + 1 + 1 + 1 + 2 + 2 + 2 + 2 + 2 + 2) << 16; // AUDxLCH
	l |= getword(p, 1 + 1 + 1 + 1 + 2 + 2 + 2 + 2 + 2 + 2 + 2); // AUDxLCL
	c->aud[num].ac_ptr = (UWORD*)l;
}

// latched AUDxLEN and AUDxPT
void set_audio_final(struct uaestate *st)
{
	volatile struct Custom *c = (volatile struct Custom*)0xdff000;
	for (UWORD num = 0; num < 4; num++) {
		UBYTE *p = st->audio_chunk[num];
		if (p) {
			ULONG l;
			c->aud[num].ac_len = getword(p, 1 + 1 + 1 + 1); // AUDxLEN
			l = getword(p, 1 + 1 + 1 + 1 + 2 + 2 + 2 + 2) << 16; // AUDxLCH
			l |= getword(p, 1 + 1 + 1 + 1 + 2 + 2 + 2 + 2 + 2); // AUDxLCL
			c->aud[num].ac_ptr = (UWORD*)l;
		}
	}
}

static void set_sprite(UBYTE *p, ULONG num)
{
	volatile struct Custom *c = (volatile struct Custom*)0xdff000;
	ULONG l;
	
	if (!p)
		return;
	
	l = getword(p, 0) << 16; // SPRxPTH
	l |= getword(p, 2); // SPRxPTL
	c->sprpt[num] = (APTR)l;
	c->spr[num].pos = getword(p, 2 + 2); // SPRxPOS
	c->spr[num].ctl = getword(p, 2 + 2 + 2); // SPRxCTL
}

static void set_custom(struct uaestate *st)
{
	volatile UWORD *c = (volatile UWORD*)0xdff000;
	UBYTE *p = st->custom_chunk;
	p += 4;
	for (WORD i = 0; i < 0x1fe; i += 2, c++) {

		// sprites
		if (i >= 0x120 && i < 0x180)
			continue;
			
		// audio
		if (i >= 0xa0 && i < 0xe0)
			continue;

		// skip blitter start, DMACON, INTENA, registers
		// that are write strobed, unused registers,
		// read-only registers.
		switch(i)
		{
			case 0x00:
			case 0x02:
			case 0x04:
			case 0x06:
			case 0x08:
			case 0x10:
			case 0x16:
			case 0x18:
			case 0x1a:
			case 0x1c:
			case 0x1e:
			case 0x24: // DSKLEN
			case 0x26:
			case 0x28:
			case 0x2a: // VPOSW
			case 0x2c: // VHPOSW
			case 0x30:
			case 0x38:
			case 0x3a:
			case 0x3c:
			case 0x3e:
			case 0x58:
			case 0x5a:
			case 0x5e:
			case 0x68:
			case 0x6a:
			case 0x6c:
			case 0x6e:
			case 0x76:
			case 0x78:
			case 0x7a:
			case 0x7c:
			case 0x88:
			case 0x8a:
			case 0x8c:			
			case 0x96: // DMACON
			case 0x9a: // INTENA
			case 0x9c: // INTREQ
			p += 2;
			continue;
		}

		// skip programmed sync registers except BEAMCON0
		// skip unused registers
		if (i >= 0x1c0 && i < 0x1fc && i != 0x1e4 && i != 0x1dc) {
			p += 2;
			continue;
		}

		UWORD v = getword(p, 0);
		p += 2;

		// diwhigh
		if (i == 0x1e4) { 
			// diwhigh_written not set? skip.
			if (!(v & 0x8000))
				continue;
			v &= ~0x8000;
		}

 		// BEAMCON0: PAL/NTSC only
		if (i == 0x1dc) {
			if (st->flags & FLAGS_FORCEPAL)
				v = 0x20;
			else if (st->flags & FLAGS_FORCENTSC)
				v = 0x00;
			v &= 0x20;
		}

		// ADKCON
		if (i == 0x9e) {
			v |= 0x8000;
		}

		*c = v;
	}
}

void set_custom_final(UBYTE *p)
{
	volatile struct Custom *c = (volatile struct Custom*)0xdff000;
	c->intena = 0x7fff;
	c->intreq = 0x7fff;
	c->vposw = (getword(p, 4 + 0x04) & 0x8000) | (c->vposr & 7); // set LOF
	c->dmacon = (getword(p, 4 + 0x96) & ~15) | 0x8000;
	c->intena = getword(p, 4 + 0x9a) | 0x8000;
	c->intreq = getword(p, 4 + 0x9c) | 0x8000;
}

static void set_cia(UBYTE *p, ULONG num)
{
	volatile struct CIA *cia = (volatile struct CIA*)(num ? 0xbfd000 : 0xbfe001);
	volatile struct Custom *c = (volatile struct Custom*)0xdff000;

	cia->ciacra &= ~(CIACRAF_START | CIACRAF_RUNMODE);
	cia->ciacrb &= ~(CIACRBF_START | CIACRBF_RUNMODE);
	volatile UBYTE dummy = cia->ciaicr;
	cia->ciaicr = 0x7f;
	c->intreq = 0x7fff;
	
	p[14] &= ~CIACRAF_LOAD;
	p[15] &= ~CIACRAF_LOAD;
	
	UBYTE flags = p[16 + 1 + 2 * 2 + 3 + 3];
	
	cia->ciapra = p[0];
	cia->ciaprb = p[1];
	cia->ciaddra = p[2];
	cia->ciaddrb = p[3];
	
	// load timers
	cia->ciatalo = p[4];
	cia->ciatahi = p[5];
	cia->ciatblo = p[6];
	cia->ciatbhi = p[7];
	cia->ciacra |= CIACRAF_LOAD;
	cia->ciacrb |= CIACRBF_LOAD;
	// load timer latches
	cia->ciatalo = p[16 + 1];
	cia->ciatahi = p[16 + 2];
	cia->ciatblo = p[16 + 3];
	cia->ciatbhi = p[16 + 4];
	
	// load alarm
	UBYTE *alarm = &p[16 + 1 + 2 * 2 + 3];
	cia->ciacrb |= CIACRBF_ALARM;
	if (flags & 2) {
		// leave latched
		cia->ciatodlow = alarm[0];	
		cia->ciatodmid = alarm[1];
		cia->ciatodhi = alarm[2];
	} else {
		cia->ciatodhi = alarm[2];
		cia->ciatodmid = alarm[1];
		cia->ciatodlow = alarm[0];
	}
	cia->ciacrb &= ~CIACRBF_ALARM;

	// load tod
	UBYTE *tod = &p[8];
	if (flags & 1) {
		// leave latched
		cia->ciatodlow = tod[0];
		cia->ciatodmid = tod[1];
		cia->ciatodhi = tod[2];
	} else {
		cia->ciatodhi = tod[2];
		cia->ciatodmid = tod[1];
		cia->ciatodlow = tod[0];
	}
}

void set_cia_final(UBYTE *p, ULONG num)
{
	volatile struct CIA *cia = (volatile struct CIA*)(num ? 0xbfd000 : 0xbfe001);
	volatile UBYTE dummy = cia->ciaicr;
	cia->ciaicr = p[16] | CIAICRF_SETCLR;	
}

static void free_allocations(struct uaestate *st)
{
	for (int i = st->num_allocations - 1; i >= 0; i--) {
		struct Allocation *a = &st->allocations[i];
		if (a->mh) {
			Deallocate(a->mh, a->addr, a->size);		
		} else {
			FreeMem(a->addr, a->size);
		}
	}
}

static struct Allocation *add_allocation(void *addr, ULONG size, struct uaestate *st)
{
	if (st->num_allocations >= ALLOCATIONS) {
		printf("ERROR: Too many allocations!\n");
		return NULL;
	}
	struct Allocation *a = &st->allocations[st->num_allocations++];
	a->addr = addr;
	a->size = size;
	return a;
}

static ULONG mmu_remap(ULONG addr, ULONG size, BOOL wp, ULONG alignedphys, struct uaestate *st)
{
	void *phys;
	BOOL alloc = FALSE;
	if (!st->canusemmu)
		return 0;
	if (!alignedphys) {
		// Fast RAM required, must fail if only chip ram available
		phys = AllocMem(size + 4095, MEMF_FAST);
		if (!phys) {
			printf("MMU: Error allocating remap space for %08lx-%08lx.\n", addr, addr + size - 1);
			return 0;
		}
		Forbid();
		FreeMem(phys, size + 4095);
		alignedphys = (((ULONG)phys) + 4095) & ~4095;
		phys = AllocAbs(size, (void*)alignedphys);
		Permit();
		if (!phys) {
			printf("MMU: Error allocating remap space for %08lx-%08lx.\n", addr, addr + size - 1);
			return 0;
		}
		alloc = TRUE;
	} else {
		phys = (void*)alignedphys;
	}
	if (!map_region(st, (void*)addr, phys, size, FALSE, wp, FALSE, 0)) {
		if (alloc)
			FreeMem(phys, size);
		return 0;
	}
	if (alloc) {
		add_allocation(phys, size, st);
	}
	st->mmuused++;
	return alignedphys;
}

UBYTE *allocate_abs(ULONG size, ULONG addr, struct uaestate *st)
{
		UBYTE *b = AllocAbs(size, (APTR)addr);
		if (b) {
			add_allocation(b, size, st);
			return b;
		}
		return NULL;
}

static UBYTE *extra_allocate(ULONG size, struct uaestate *st)
{
	UBYTE *b;
	
	for (;;) {
		b = AllocAbs(size, st->extra_mem_pointer);
		if (b) {
			add_allocation(b, size, st);
			st->extra_mem_pointer += (size + 7) & ~7;
			return b;
		}
		st->extra_mem_pointer += 8;
		if (st->extra_mem_pointer + size >= st->extra_ram + st->extra_ram_size)
			return NULL;
	}
}

// allocate from extra mem
static UBYTE *tempmem_allocate(ULONG size, struct uaestate *st)
{
	UBYTE *b = NULL;
	if (st->extra_mem_head) {
		b = Allocate(st->extra_mem_head, size);
		if (b) {
			struct Allocation *a = add_allocation(b, size, st);
			if (a)
				a->mh = st->extra_mem_head;
		}
	}
	if (!b) {
		b = extra_allocate(size, st);
	}
	return b;
}

// allocate from statefile reserved bank index
static UBYTE *tempmem_allocate_reserved(ULONG size, WORD index, struct uaestate *st)
{
	struct MemoryBank *mb = &st->membanks[index];
	if (!mb->targetsize)
		return NULL;
	UBYTE *addr = mb->targetaddr;
	for (;;) {
		addr += 32768;
		if (addr - mb->targetaddr + size >= mb->targetsize)
			return NULL;
		UBYTE *b = AllocAbs(size, addr);
		if (b) {
			add_allocation(b, size, st);
			return b;
		}
	}
}

static void copyrom(ULONG addr, struct uaestate *st)
{
	ULONG *dst = (ULONG*)addr;
	ULONG *src = (ULONG*)st->maprom;
	UWORD cnt = st->mapromsize / 16;
	for (UWORD i = 0; i < cnt; i++) {
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
	}
	if (st->mapromsize == 262144) {
		src = (ULONG*)st->maprom;
		for (UWORD i = 0; i < cnt; i++) {
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
		}
	}
}

static void set_maprom(struct uaestate *st)
{
	struct mapromdata *mrd = &st->mrd[1];
	if (mrd->type == MAPROM_ACA500 || mrd->type == MAPROM_ACA500P) {
		volatile UBYTE *base = (volatile UBYTE*)mrd->board;
		base[0x3000] = 0;
		base[0x7000] = 0;
		base[0xf000] = 0;
		base[0xb000] = 0;
		base[0x23000] = 0;
		copyrom(mrd->addr, st);
		base[0x23000] = 0xff;
		base[0x3000] = 0;
	}
	mrd = &st->mrd[0];
	if (mrd->type == MAPROM_ACA1221EC) {
		volatile UBYTE *base = (volatile UBYTE*)mrd->board;
		base[0x1000] = 0x05;
		base[0x1001] = 0x00;
		base[0x2000] = 0x00;
		base[0x1000] = 0x03;
		base[0x1001] = 0x01;
		base[0x2000] = 0x00;		
		copyrom(0x600000, st);
		copyrom(0x780000, st);
		base[0x1000] = 0x03;
		base[0x1001] = 0x00;
		base[0x2000] = 0x00;
		base[0x1000] = 0x05;
		base[0x1001] = 0x01;
		base[0x2000] = 0x00;
	}
	if (mrd->type == MAPROM_ACA12xx) {
		ULONG mapromaddr = mrd->addr;
		volatile UWORD *base = (volatile UWORD*)mrd->board;
		base[0 / 2] = 0xffff;
		base[4 / 2] = 0x0000;
		base[8 / 2] = 0xffff;
		base[12 / 2] = 0xffff;
		base[0x18 / 2] = 0x0000; // maprom off
		copyrom(mapromaddr, st);
		copyrom(mapromaddr + 524288, st);
		base[0x18 / 2] = 0xffff; // maprom on
		volatile UWORD dummy = base[0];
	}
	if (mrd->type == MAPROM_ACA1233N) {
		volatile UWORD *base = (volatile UWORD*)mrd->board;
		ULONG mapromaddr = mrd->addr;
		volatile UWORD dummy = base[0];
		base[0x00 / 2] = 0xffff; // s unlock 0
		base[0x02 / 2] = 0xffff; // s unlock 1
		base[0x20 / 2] = 0xffff; // c unlock 0
		base[0x04 / 2] = 0xffff; // s unlock 2
		base[0x22 / 2] = 0xffff; // c unlock 1
		base[0x06 / 2] = 0xffff; // s unlock 3
		// maprom off
		base[0x28 / 2] = 0xffff;
		if (mrd->config) {
			// maprom overlay on
			for(int i = 0; i < 6; i++)
				base[0x1c / 2] = 0xffff;
			base[0x1a / 2] = 0xffff;
		}
		copyrom(mapromaddr, st);
		copyrom(mapromaddr + 524288, st);
		if (mrd->config) {
			// maprom overlay off
			base[0x3a / 2] = 0xffff;
		}
		// maprom on
		base[0x08 / 2] = 0xffff;
	}
	if (mrd->type == MAPROM_GVP) {
		copyrom(mrd->addr, st);
		volatile UWORD *base = (volatile UWORD*)mrd->board;
		*base = (UWORD)mrd->config;
	}
	if (mrd->type == MAPROM_BLIZZARD12x0) {
		copyrom(mrd->addr, st);
		volatile UBYTE *base = (volatile UBYTE*)mrd->board;
		*base = (UBYTE)mrd->config;
	}
	if (mrd->type == MAPROM_MMU) {
		copyrom(mrd->addr, st);
	}
}

static BOOL has_maprom_blizzard(struct uaestate *st)
{
	struct mapromdata *mrd = &st->mrd[0];
	struct ConfigDev *cd;
	// 1230MKIV/1240/1260
	cd  = (struct ConfigDev*)FindConfigDev(0, 8512, 17);
	if (cd) {
		mrd->type = MAPROM_BLIZZARD12x0;
		mrd->addr = 0x4ff80000;
	}
	// 1230MKIII
	cd  = (struct ConfigDev*)FindConfigDev(0, 8512, 13);
	if (cd) {
		mrd->type = MAPROM_BLIZZARD12x0;
		mrd->addr = 0x1ef80000;
	}
	// 1230MKI/II
	cd  = (struct ConfigDev*)FindConfigDev(0, 8512, 11);
	if (cd) {
		mrd->type = MAPROM_BLIZZARD12x0;
		mrd->addr = 0x0ff80000;
	}
	mrd->board = (APTR)0x80ffff00;
	mrd->config = 0x42;
	if (st->debug && mrd->type)
		printf("Blizzard12x0 MapROM %08lx\n", mrd->addr);
	return mrd->type != 0;
}


static const ULONG gvp_a530[] =
{
	0x00a00000, 14,
	0x00600000, 10,
	0x00400000, 12,
	0x00300000, 8,
	0
};
static const ULONG gvp_gforce030[] =
{
	0x02000000, 10,
	0x01c00000, 12,
	0x01800000, 8,
	0x01400000, 6,
	0x00a00000, 2,
	0x00600000, 4,
	0
};
static const ULONG gvp_gforce2k_040[] =
{
	0x05000000, 8,
	0x04000000, 12,
	0x03000000, 10,
	0x02000000, 14,
	0x01c00000, 10,
	0x01800000, 12,
	0
};
#if 0
static const ULONG gvp_gforce3k_040[] =
{
	0x08800000, 0xC0000000,
	0x08400000, 0x80000000,
	0
};
#endif

static BOOL has_maprom_gvp(struct uaestate *st)
{
		struct mapromdata *mrd = &st->mrd[0];
		struct ConfigDev *cd = (struct ConfigDev*)FindConfigDev(0, 2017, 11);
		if (!cd)
			return FALSE;
		UBYTE v = *((volatile UBYTE*)cd->cd_BoardAddr + 0x8001);
		if (st->debug)
			printf("GVP ID=%02x\n", v);
		const ULONG *gvpmaprom = NULL;
		switch(v & 0xf8)
		{
			case 0x20:
			case 0x30:
				gvpmaprom = gvp_gforce2k_040;
				break;
			case 0x60:
			case 0x70:
			case 0xe0:
			case 0xf0:
				gvpmaprom = gvp_gforce030;
				break;
			case 0xc0:
			case 0xd0:
				gvpmaprom = gvp_a530;
			break;
			case 0x40:
				break;
		}
		if (!gvpmaprom)
			return FALSE;
		Forbid();
		struct MemHeader *mh = (struct MemHeader*)SysBase->MemList.lh_Head;
		while (mh->mh_Node.ln_Succ && !mrd->type) {
			ULONG mend = ((((ULONG)mh->mh_Upper) + 0xffff) & 0xffff0000);
			for (int i = 0; gvpmaprom[i]; i += 2) {
				if (gvpmaprom[i] == mend) {
					BOOL memok = TRUE;
					// if Z2 space: make sure it is located in accelerator
					// board RAM instead of some plain Z2 RAM board.
					if (mend <= 0xa00000) {
						memok = FALSE;
						struct ConfigDev *cd = NULL;
						while ((cd = (struct ConfigDev*)FindConfigDev((ULONG)cd, 2017, 9))) {
							if ((APTR)mend == cd->cd_BoardAddr + cd->cd_BoardSize) {
								memok = TRUE;
								break;
							}
						}
					}
					if (memok) {
						mrd->type = MAPROM_GVP;
						mrd->addr = mend - 524288;
						mrd->config = gvpmaprom[i + 1];
						mrd->board = cd->cd_BoardAddr + 0x68;
						// ignore return value, maprom may be already enabled
						allocate_abs(524288, mrd->addr, st);
						break;
					}
				}
			}
			mh = (struct MemHeader*)mh->mh_Node.ln_Succ;
		}
		Permit();
		if (st->debug)
			printf("GVP Map ROM=%08lx\n", mrd->addr);
		return mrd->type != 0;
}

static BOOL has_maprom_aca(struct uaestate *st)
{
	if (OpenResource("aca.resource")) {
		struct mapromdata *mrd2 = &st->mrd[1];
		// ACA500(+)
		volatile UBYTE *base = (volatile UBYTE*)0xb00000;
		Disable();
		base[0x3000] = 0;
		base[0x7000] = 0;
		base[0xf000] = 0;
		base[0xb000] = 0;
		UBYTE id = 0;
		if (base[0x13000] & 0x80)
			id |= 0x08;
		if (base[0x17000] & 0x80)
			id |= 0x04;
		if (base[0x1b000] & 0x80)
			id |= 0x02;
		if (base[0x1f000] & 0x80)
			id |= 0x01;
		if (id == 7) {
			mrd2->type = MAPROM_ACA500;
			mrd2->addr = 0x980000;
		} else if (id == 8 || id == 9) {
			mrd2->type = MAPROM_ACA500P;
			mrd2->addr = 0xa00000;
		}
		mrd2->board = (APTR)base;
		base[0x3000] = 0;
		Enable();
		if (st->debug)
			printf("ACA500/ACA500plus ID=%02x\n", id);
	}
	struct mapromdata *mrd = &st->mrd[0];
	if (FindConfigDev(0, 0x1212, 33) || FindConfigDev(0, 0x1212, 68)) {
		// ACA1233n 68030
		mrd->type = MAPROM_ACA1233N;
		mrd->addr = 0x47f00000;
		mrd->board = (APTR)0x47e8f000;
		mrd->config = 0;
	} else if (FindConfigDev(0, 0x1212, 32) || FindConfigDev(0, 0x1212, 72)) {
		// ACA1233n 68EC020
		if ((ULONG)has_maprom_aca >= 0x200000) {
			mrd->type = MAPROM_ACA1233N;
			mrd->addr = 0x100000;
			mrd->board = (APTR)0xb8f000;
			mrd->config = 1;
			st->maprom_memlimit |= 1 << MB_CHIP;
		}
	} else if (FindConfigDev(0, 0x1212, 0x16)) {
		// ACA1221EC
		mrd->type = MAPROM_ACA1221EC;
		mrd->addr = 0x780000;
		mrd->board = (APTR)0xe90000;
		// we can't use 0x200000 because it goes away when setting up maprom..
		mrd->memunavailable = 0x00200000;
	} else if (TypeOfMem((APTR)0x0bd80000)) { // at least 62M
		// This test should be better..
		// perhaps it is ACA12xx
		Disable();
		volatile UWORD *base = (volatile UWORD*)0xb8f000;
		volatile UWORD dummy = base[0];
		// unlock
		base[0 / 2] = 0xffff;
		base[4 / 2] = 0x0000;
		base[8 / 2] = 0xffff;
		base[12 / 2] = 0xffff;
		UBYTE id = 0;
		volatile UWORD *idbits = base + 4 / 2;
		for (UWORD i = 0; i < 5; i++) {
			id <<= 1;
			if (idbits[0] & 1)
				id |= 1;
			idbits += 4 / 2;
		}
		UWORD mrtest = 0;
		if (id != 0 && id != 31) {
			// test that maprom bit sticks
			UWORD oldmr = base[0x18 / 2];
			base[0x18 / 2] = 0xffff;
			dummy = base[0];
			mrtest = (base[0x18 / 2] & 0x8000) != 0;
			base[0 / 2] = 0xffff;
			base[4 / 2] = 0x0000;
			base[8 / 2] = 0xffff;
			base[12 / 2] = 0xffff;
			base[0x18 / 2] = oldmr;
		}
		dummy = base[0];
		Enable();
		if (st->debug)
			printf("ACA12xx ID=%02x. MapROM=%d\n", id, mrtest);
		ULONG mraddr = 0;
		if (mrtest) {
			if (TypeOfMem((APTR)0x0bf00000)) {
				if (!TypeOfMem((APTR)0x0fe80000)) {
					mrd->type = MAPROM_ACA12xx;
					mrd->addr = 0x0ff00000;
				}
			} else {
					mrd->type = MAPROM_ACA12xx;
					mrd->addr = 0x0bf00000;
			}
		}
		mrd->board = (APTR)base;
	}
	return st->mrd[0].type != 0;
}

static BOOL has_maprom_mmu(struct uaestate *st)
{
	if (!st->canusemmu)
		return FALSE;
	struct mapromdata *mrd = &st->mrd[0];
	unmap_region(st, (void*)0xf80000, 524288);
	mrd->addr = mmu_remap(0xf80000, 524288, FALSE, 0, st);
	if (mrd->addr) {
		mrd->type = MAPROM_MMU;
		return TRUE;
	}
	return FALSE;
}

static BOOL has_maprom(struct uaestate *st)
{
	if (!st->usemaprom)
		return -1;
	for (;;) {
		if (has_maprom_mmu(st))
			break;
		if (has_maprom_aca(st))
			break;
		if (has_maprom_gvp(st))
			break;
		if (has_maprom_blizzard(st))
			break;
		return FALSE;
	}
	if (st->debug) {
		for (int i = 0; i < 2; i++) {
			struct mapromdata *mrd = &st->mrd[i];
			if (mrd->type)
				printf("MAPROM type %d. Addr=%08lx Board=%08lx Cfg=%08lx.\n", mrd->type, mrd->addr, mrd->board, mrd->config);
		}
	}
	return st->mrd[0].type != 0 || st->mrd[1].type != 0;
}

static void load_rom(struct uaestate *st)
{
	UBYTE rompath[100], rompath2[100];
	UBYTE *p;
	
	if (!st->mrd[0].type && !st->mrd[1].type)
		return;
	
	sprintf(rompath, "DEVS:kickstarts/kick%d%03d.%s", st->romver, st->romrev, st->agastate ? "a1200" : "a500");
	p = rompath;
	FILE *f = fopen(rompath, "rb");
	if (!f) {
		sprintf(rompath2, "kick%d%03d.%s", st->romver, st->romrev, st->agastate ? "a1200" : "a500");
		f = fopen(rompath2, "rb");
		if (!f) {
			printf("Couldn't open ROM image '%s'\n", rompath);
			return;
		}
		p = rompath2;
	}
	fseek(f, 0, SEEK_END);
	st->mapromsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (!st->maprom && !(st->maprom_memlimit & (1 << MB_CHIP)))
		st->maprom = tempmem_allocate_reserved(st->mapromsize, MB_CHIP, st);
	if (!st->maprom && !(st->maprom_memlimit & (1 << MB_SLOW)))
		st->maprom = tempmem_allocate_reserved(st->mapromsize, MB_SLOW, st);
	if (!st->maprom)
		st->maprom = tempmem_allocate(st->mapromsize, st);
	if (!st->maprom) {
		printf("Couldn't allocate %luk for ROM image '%s'.\n", st->mapromsize >> 10, p);
		fclose(f);
		return;
	}
	if (st->debug)
		printf("MapROM temp %08lx\n", st->maprom);
	if (fread(st->maprom, 1, st->mapromsize, f) != st->mapromsize) {
		printf("Read error while reading map rom image '%s'\n", p);
		fclose(f);
		return;
	}
	fclose(f);
	printf("ROM '%s' (%luk) loaded .\n", rompath, st->mapromsize >> 10);
}

static void load_memory(FILE *f, WORD index, struct uaestate *st)
{
	struct MemoryBank *mb = &st->membanks[index];
	ULONG oldoffset = ftell(f);
	ULONG chunksize = mb->size + 12;
	fseek(f, mb->offset, SEEK_SET);
	if (st->debug)
		printf("Memory '%s', size %luk, offset %lu. Target %08lx.\n", mb->chunk, chunksize >> 10, mb->offset, mb->targetaddr);
	// if Chip RAM and free space in another statefile block? Put it there because chip ram is decompressed first.
	if (index == MB_CHIP) {
		mb->addr = tempmem_allocate_reserved(chunksize, MB_SLOW, st);
		if (!mb->addr)
			mb->addr = tempmem_allocate_reserved(chunksize, MB_FAST, st);
	} else if (index == MB_SLOW) {
		mb->addr = tempmem_allocate_reserved(chunksize, MB_FAST, st);
	}
	if (!mb->addr)
		mb->addr = tempmem_allocate(chunksize, st);
	if (mb->addr) {
		if (st->debug)
			printf(" - Address %08lx - %08lx.\n", mb->addr, mb->addr + chunksize - 1);
		if (fread(mb->addr, 1, chunksize, f) != chunksize) {
			printf("ERROR: Read error (Chunk '%s', %lu bytes)\n", mb->chunk, chunksize);
			st->errors++;
		}		
	} else {
		printf("ERROR: Out of memory (Chunk '%s', %lu bytes).\n", mb->chunk, chunksize);
		st->errors++;
	}
	fseek(f, oldoffset, SEEK_SET);
}

static int read_chunk_head(FILE *f, UBYTE *cnamep, ULONG *sizep, ULONG *flagsp)
{
	ULONG size = 0, flags = 0;
	UBYTE cname[5];

	*flagsp = 0;
	*sizep = 0;
	cnamep[0] = 0;
	if (fread(cname, 1, 4, f) != 4) {
		return 0;
	}
	cname[4] = 0;
	strcpy(cnamep, cname);

	if (fread(&size, 1, 4, f) != 4) {
		cnamep[0] = 0;
		return 0;
	}

	if (fread(&flags, 1, 4, f) == 0) {
		return 1;
	}

	if (size < 8)
		return 1;

	if (size < 12) {
		size = 0;
		flags = 0;
	} else {
		size -= 12;
	}
	*sizep = size;
	*flagsp = flags;
	return 1;
}

static UBYTE *load_chunk(FILE *f, UBYTE *cname, ULONG size, struct uaestate *st)
{
	UBYTE *b = NULL;
	int acate = 0;
	
	//printf("Allocating %lu bytes for '%s'.\n", size, cname);

	b = tempmem_allocate(size, st);
	
	//printf("Reading chunk '%s', %lu bytes to address %08x\.n", cname, size, b);
	
	if (!b) {
		printf("ERROR: Not enough memory (Chunk '%s', %ul bytes required).\n", cname, size);
		return NULL;
	}
	
	if (fread(b, 1, size, f) != size) {
		printf("ERROR: Read error  (Chunk '%s', %lu bytes).\n", cname, size);
		return NULL;
	}
	
	fseek(f, 4 - (size & 3), SEEK_CUR);
		
	return b;
}

static UBYTE *read_chunk(FILE *f, UBYTE *cname, ULONG *sizep, ULONG *flagsp, struct uaestate *st)
{
	ULONG size, orgsize, flags;

	if (!read_chunk_head(f, cname, &size, &flags))
		return NULL;
	orgsize = size;
	*flagsp = flags;

	if (size == 0)
		return NULL;

	ULONG maxsize = 0x7fffffff;

	for (int i = 0; unsupportedchunknames[i]; i++) {
		if (!strcmp(cname, unsupportedchunknames[i])) {
			printf("ERROR: Unsupported chunk '%s', %lu bytes, flags %08x.\n", cname, size, flags);
			st->errors++;
			return NULL;
		}
	}

	int found = 0;
	for (int i = 0; chunknames[i]; i++) {
		if (!strcmp(cname, chunknames[i])) {
			found = 1;
			if (st->debug)
				printf("Reading chunk '%s', %lu bytes, flags %08x.\n", cname, size, flags);
			break;
		}
	}
	if (!found) {
		// read only header if memory chunk
		for (int i = 0; memchunknames[i]; i++) {
			if (!strcmp(cname, memchunknames[i])) {
				found = 1;
				maxsize = 16;
				if (st->debug)
					printf("Checking memory chunk '%s', %lu bytes, flags %08x.\n", cname, size, flags);
				break;
			}	
		}
	}
	
	if (!found) {
		//printf("Skipped chunk '%s', %ld bytes, flags %08x\n", cname, size, flags);
		fseek(f, size, SEEK_CUR);
		if (size)
			fseek(f, 4 - (size & 3), SEEK_CUR);
		return NULL;
	}

	*sizep = size;
	if (size > maxsize)
		size = maxsize;	
	UBYTE *chunk = malloc(size);
	if (!chunk) {
		printf("ERROR: Not enough memory (Chunk '%s', %lu bytes).\n", cname, size);
		return NULL;
	}
	if (fread(chunk, 1, size, f) != size) {
		printf("ERROR: Read error (Chunk '%s', %lu bytes)..\n", cname, size);
		free(chunk);
		return NULL;
	}
	if (orgsize > size) {
		fseek(f, orgsize - size, SEEK_CUR);
	}
	fseek(f, 4 - (orgsize & 3), SEEK_CUR);
	return chunk;	
}

static void find_extra_ram(struct uaestate *st)
{
	Forbid();
	struct MemHeader *mh = (struct MemHeader*)SysBase->MemList.lh_Head;
	while (mh->mh_Node.ln_Succ) {
		ULONG mstart = ((ULONG)mh->mh_Lower) & 0xffff0000;
		ULONG msize = ((((ULONG)mh->mh_Upper) + 0xffff) & 0xffff0000) - mstart;
		int i;
		for (i = 0; i < MEMORY_REGIONS; i++) {
			if (st->mem_allocated[i] == mh)
				break;
		}
		if (i == MEMORY_REGIONS && (mstart != st->mrd[0].memunavailable && mstart != st->mrd[1].memunavailable)) {
			if (msize > st->extra_ram_size) {
				st->extra_ram = (UBYTE*)mstart;
				st->extra_ram_size = msize;
				st->extra_mem_head = mh;
			}	
		}
		mh = (struct MemHeader*)mh->mh_Node.ln_Succ;
	}
	Permit();
}

static ULONG check_ram(UBYTE *ramname, UBYTE *cname, UBYTE *chunk, WORD index, ULONG addr, ULONG offset, ULONG chunksize, ULONG flags, struct uaestate *st)
{
	ULONG size;
	if (flags & 1) // compressed
		size = getlong(chunk, 0);
	else
		size = chunksize;
	if (st->debug)
		printf("Statefile RAM: Address %08x, size %luk.\n", addr, size >> 10);
	int found = 0;
	ULONG mstart, msize;
	Forbid();
	struct MemHeader *mh = (struct MemHeader*)SysBase->MemList.lh_Head;
	while (mh->mh_Node.ln_Succ) {
		mstart = ((ULONG)mh->mh_Lower) & 0xffff0000;
		msize = ((((ULONG)mh->mh_Upper) + 0xffff) & 0xffff0000) - mstart;
		if (mstart == addr && st->mem_allocated[index] == 0) {
			if (msize >= size)
				found = 1;
			else
				found = -1;
			break;
		}
		mh = (struct MemHeader*)mh->mh_Node.ln_Succ;
	}
	Permit();
	if (!found) {
		// use MMU to create this address space if available
		if (mmu_remap(addr, size, FALSE, 0, st)) {
			msize = size;
			mstart = addr;
			mh = NULL;
			found = 1;
		} else {
			printf("ERROR: Required RAM address space %08x-%08x unavailable.\n", addr, addr + size - 1);
			st->errors++;
			return 0;
		}
	}
	st->mem_allocated[index] = mh;
	struct MemoryBank *mb = &st->membanks[index];
	mb->size = chunksize;
	mb->offset = offset;
	mb->targetaddr = (UBYTE*)addr;
	mb->targetsize = msize;
	mb->flags = flags;
	strcpy(mb->chunk, cname);
	if (st->debug)
		printf("- Detected memory at %08x, total size %luk. Offset %ld.\n", mstart, msize >> 10, offset);
	if (found > 0) {
		if (st->debug)
			printf("- Memory is usable (%luk required, %luk unused).\n", size >> 10, (msize - size) >> 10);
		ULONG extrasize = msize - size;
		if (extrasize >= 524288) {
			if ((mstart >= 0x00200000 && st->extra_ram < (UBYTE*)0x00200000) || extrasize > st->extra_ram_size) {
				st->extra_ram = (UBYTE*)(mstart + size);
				st->extra_ram_size = extrasize;
			}
		}
		return 1;
	}
	// if too small RAM area: mmu remap the missing part.
	if (st->canusemmu) {
		// if we have 512k chip ram, 512k slow ram and ECS Agnus: create special 0xc00000->0x080000 mapping.
		volatile struct Custom *c = (volatile struct Custom*)0xdff000;
		ULONG mmu_start = mstart + msize;
		ULONG mmu_size = size - msize;
		ULONG phys = 0;
		if (!st->mem_allocated[MB_SLOW] && !mstart && msize == 524288 && mmu_size >= 524288 && (c->vposr & 0x2000)) {
			// mark c00000 space as allocated
			Forbid();
			struct MemHeader *mh = (struct MemHeader*)SysBase->MemList.lh_Head;
			while (mh->mh_Node.ln_Succ) {
				mstart = ((ULONG)mh->mh_Lower) & 0xffff0000;
				if (mstart == 0xc00000) {
					st->mem_allocated[MB_SLOW] = mh;
					break;
				}
				mh = (struct MemHeader*)mh->mh_Node.ln_Succ;
			}
			Permit();
			printf("- MMU: ECS Agnus 512k to 1M Chip RAM remapping enabled.\n");
			phys = 0xc00000;
		}
		ULONG physout = mmu_remap(mmu_start, mmu_size, FALSE, phys, st);
		if (physout) {
			printf("- MMU remapped missing address space %08lx-%08lx -> %08lx\n", mmu_start, mmu_start + mmu_size - 1, physout);
			if (mstart == 0 && !phys) {
				printf("- WARNING: Part of Chip RAM remapped, custom chipset can't access it!!\n");
			}
			return 1;
		}
	}
	printf("ERROR: Not enough %s RAM available. %luk required.\n", ramname, size >> 10);
	st->errors++;
	return 0;
}

static void floppy_info(int num, UBYTE *p)
{
	UBYTE state = p[4];
	UBYTE track = p[5];
	if ((state & 2) || !p) // disabled
		return;
	UBYTE *path = &p[4 + 1 + 1 + 1 + 1 + 4 + 4];
	printf("DF%d: Track %d ", num, track);
	if (path[0])
		printf("'%s'.\n", path);
	else
		printf("<empty>\n");
}

static void fpu_process(UBYTE *fpu, struct uaestate *st)
{
	ULONG *b = (ULONG*)tempmem_allocate(12 * 8 + 3 * 4, st);	
	if (!b) {
		st->fpu_chunk = NULL;
		return;
	}
	*((ULONG**)st->fpu_chunk) = b;
	UWORD *src = (UWORD*)(st->fpu_chunk + 8);
	UWORD *dst = (UWORD*)b;
	// convert 10 byte statefile format to 12 byte extended doubled
	for (int i = 0; i < 8; i++) {
		*dst++ = *src++;
		*dst++ = 0;
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
	}
	// FPU other registers
	for (int i = 0; i < 3 * 2; i++) {
		*dst++ = *src++;
	}
}

static void check_rom(UBYTE *p, struct uaestate *st)
{
	UWORD ver = getword(p, 4 + 4 + 4);
	UWORD rev = getword(p, 4 + 4 + 4 + 2);
	
	UWORD *rom = (UWORD*)0xf80000;
	UWORD rver = rom[12 / 2];
	UWORD rrev = rom[14 / 2];
	
	ULONG start = getlong(p, 0);
	ULONG len = getlong(p, 4);
	if (start == 0xf80000 && len == 262144)
		start = 0xfc0000;
	ULONG crc32 = getlong(p, 4 + 4 + 4 + 4);
	
	UBYTE *path = &p[4 + 4 + 4 + 4 + 4];
	while (*path++);
	
	int mismatch = ver != rver || rev != rrev;
	if (mismatch)
		printf("- WARNING: ROM version mismatch: %d.%d. System ROM: %d.%d\n", ver, rev, rver, rrev);
	if (st->debug)
		printf("ROM %08lx-%08lx %d.%d (CRC=%08x).\n", start, start + len - 1, ver, rev, crc32);
	if (mismatch) {
		if (st->debug)
			printf("- '%s'\n", path);
		st->romver = ver;
		st->romrev = rev;
		WORD mr = has_maprom(st);
		if (mr == 0) {
			if (st->debug)
				printf("Map ROM support not detected\n");
		} else if (mr > 0) {
			printf("- Map ROM hardware detected.\n");
		}
	}
}

static int parse_pass_2(FILE *f, struct uaestate *st)
{
	for (int i = 0; i < MEMORY_REGIONS; i++) {
		struct MemoryBank *mb = &st->membanks[i];
		if (mb->size) {
			load_memory(f, i, st);
		}
	}
	if (st->romver) {
		load_rom(st);
	}
	
	for (;;) {
		ULONG size, flags;
		UBYTE cname [5];
		
		if (!read_chunk_head(f, cname, &size, &flags)) {
			return -1;
		}
		
		if (!strcmp(cname, "END "))
			break;

		if (!strcmp(cname, "CPU ")) {
			st->cpu_chunk = load_chunk(f, cname, size, st);
		} else if (!strcmp(cname, "FPU ")) {
			st->fpu_chunk = load_chunk(f, cname, size, st);
			fpu_process(st->fpu_chunk, st);		
		} else if (!strcmp(cname, "CHIP")) {
			st->custom_chunk = load_chunk(f, cname, size, st);
		} else if (!strcmp(cname, "AGAC")) {
			st->aga_colors_chunk = load_chunk(f, cname, size, st);
		} else if (!strcmp(cname, "CIAA")) {
			st->ciaa_chunk = load_chunk(f, cname, size, st);
		} else if (!strcmp(cname, "CIAB")) {
			st->ciab_chunk = load_chunk(f, cname, size, st);
		} else if (!strcmp(cname, "DSK0")) {
			st->floppy_chunk[0] = load_chunk(f, cname, size, st);
			floppy_info(0, st->floppy_chunk[0]);
		} else if (!strcmp(cname, "DSK1")) {
			st->floppy_chunk[1] = load_chunk(f, cname, size, st);
			floppy_info(1, st->floppy_chunk[1]);
		} else if (!strcmp(cname, "DSK2")) {
			st->floppy_chunk[2] = load_chunk(f, cname, size, st);
			floppy_info(2, st->floppy_chunk[2]);
		} else if (!strcmp(cname, "DSK3")) {
			st->floppy_chunk[3] = load_chunk(f, cname, size, st);
			floppy_info(3, st->floppy_chunk[3]);
		} else if (!strcmp(cname, "AUD0")) {
			st->audio_chunk[0] = load_chunk(f, cname, size, st);
		} else if (!strcmp(cname, "AUD1")) {
			st->audio_chunk[1] = load_chunk(f, cname, size, st);
		} else if (!strcmp(cname, "AUD2")) {
			st->audio_chunk[2] = load_chunk(f, cname, size, st);
		} else if (!strcmp(cname, "AUD3")) {
			st->audio_chunk[3] = load_chunk(f, cname, size, st);
		} else if (!strcmp(cname, "SPR0")) {
			st->sprite_chunk[0] = load_chunk(f, cname, size, st);
		} else if (!strcmp(cname, "SPR1")) {
			st->sprite_chunk[1] = load_chunk(f, cname, size, st);
		} else if (!strcmp(cname, "SPR2")) {
			st->sprite_chunk[2] = load_chunk(f, cname, size, st);
		} else if (!strcmp(cname, "SPR3")) {
			st->sprite_chunk[3] = load_chunk(f, cname, size, st);
		} else if (!strcmp(cname, "SPR4")) {
			st->sprite_chunk[4] = load_chunk(f, cname, size, st);
		} else if (!strcmp(cname, "SPR5")) {
			st->sprite_chunk[5] = load_chunk(f, cname, size, st);
		} else if (!strcmp(cname, "SPR6")) {
			st->sprite_chunk[6] = load_chunk(f, cname, size, st);
		} else if (!strcmp(cname, "SPR7")) {
			st->sprite_chunk[7] = load_chunk(f, cname, size, st);
		} else {
			fseek(f, size, SEEK_CUR);
			fseek(f, 4 - (size & 3), SEEK_CUR);
		}
	}

	return st->errors;
}

static int parse_pass_1(FILE *f, struct uaestate *st)
{
	int first = 1;
	UBYTE *b = NULL;

	for (;;) {
		ULONG offset = ftell(f);
		ULONG size, flags;
		UBYTE cname[5];
		b = read_chunk(f, cname, &size, &flags, st);
		if (!strcmp(cname, "END "))
			break;
		if (!b) {
			if (!cname[0])
				return -1;
			continue;
		}

		if (first) {
			if (strcmp(cname, "ASF ")) {
				printf("ERROR: Not UAE statefile.\n");
				return -1;
			}
			first = 0;
			continue;
		}

		if (!strcmp(cname, "CPU ")) {
			ULONG smodel = 68000;
			for (int i = 0; i < 4; i++) {
				if (SysBase->AttnFlags & (1 << i))
					smodel += 10;
			}
			if (SysBase->AttnFlags & 0x80)
				smodel = 68060;
			ULONG model = getlong(b, 0);
			printf("CPU: %lu.\n", model);
			if (smodel != model) {
				printf("- WARNING: %lu CPU statefile but system has %lu CPU.\n", model, smodel);
			}
		} else if(!strcmp(cname, "FPU ")) {
			ULONG model = getlong(b, 0);
			ULONG smodel = 0;
			if (SysBase->AttnFlags & AFF_68882)
				smodel = 68882;
			else if (SysBase->AttnFlags & AFF_68881)
				smodel = 68881;
			if (SysBase->AttnFlags & 0x80)
				smodel = 68060;
			else if (SysBase->AttnFlags & AFF_68040)
				smodel = 68040;
			if (model && !smodel) {
				printf("- ERROR: FPU required\n");
				st->errors++;
			} else if (model != smodel) {
				printf("- WARNING: %lu FPU statefile but system has %lu FPU.\n", model, smodel);
			}
		} else if (!strcmp(cname, "CHIP")) {
			UWORD vposr = getword(b, 4 + 4); // VPOSR
			volatile struct Custom *c = (volatile struct Custom*)0xdff000;
			UWORD svposr = c->vposr;
			int aga = (vposr & 0x0f00) == 0x0300;
			int ecs = (vposr & 0x2000) == 0x2000;
			int ntsc = (vposr & 0x1000) == 0x1000;
			int saga = (svposr & 0x0f00) == 0x0300;
			int secs = (svposr & 0x2000) == 0x2000;
			int sntsc = (svposr & 0x1000) == 0x1000;
			printf("Chipset: %s %s (0x%04X).\n", aga ? "AGA" : (ecs ? "ECS" : "OCS"), ntsc ? "NTSC" : "PAL", vposr);
			if (aga && !saga) {
				printf("- WARNING: AGA statefile but system is OCS/ECS.\n");
			}
			if (saga && !aga) {
				printf("- WARNING: OCS/ECS statefile but system is AGA.\n");
			}
			if (!sntsc && !secs && ntsc) {
				printf("- WARNING: NTSC statefile but system is OCS PAL.\n");
			}
			if (sntsc && !secs && !ntsc) {
				printf("- WARNING: PAL statefile but system is OCS NTSC.\n");
			}
			st->agastate = aga;
		} else if (!strcmp(cname, "CRAM")) {
			check_ram("Chip", cname, b, MB_CHIP, 0x000000, offset, size, flags, st);
		} else if (!strcmp(cname, "BRAM")) {
			check_ram("Slow", cname, b, MB_SLOW, 0xc00000, offset, size, flags, st);
		} else if (!strcmp(cname, "FRAM")) {
			check_ram("Fast", cname, b, MB_FAST, 0x200000, offset, size, flags, st);
		} else if (!strcmp(cname, "ROM ")) {
			check_rom(b, st);
		}

		free(b);
		b = NULL;
	}
	
	if (!st->errors) {
		find_extra_ram(st);
		if (!st->extra_ram) {
			printf("ERROR: At least 512k RAM not used by statefile required.\n");
			st->errors++;
		} else {
			if (st->debug)
				printf("%luk extra RAM at %08x.\n", st->extra_ram_size >> 10, st->extra_ram);
			st->extra_mem_pointer = st->extra_ram;
			st->errors = 0;
		}
	} else {
		printf("ERROR: Incompatible hardware configuration.\n");
		st->errors++;
	}

	free(b);
	
	return st->errors;
}

extern void runit(void*);
extern void callinflate(UBYTE*, UBYTE*);
extern void flushcache(void);
extern void detect060(void);
extern void detect030040(void);
extern UWORD detectmmu(void);

static void handlerambank(struct MemoryBank *mb, struct uaestate *st)
{
	UBYTE *sa = mb->addr + 12; /* skip chunk header */
	if (mb->flags & 1) {
		// skip decompressed size and zlib header
		callinflate(mb->targetaddr, sa + 4 + 2);
	} else {
		ULONG *s = (ULONG*)sa;
		ULONG *d = (ULONG*)mb->targetaddr;
		for (int i = 0; i < mb->size / 4; i++) {
			*d++ = *s++;
		}		
	}
}

// Interrupts are off, supervisor state
static void processstate(struct uaestate *st)
{
	volatile struct Custom *c = (volatile struct Custom*)0xdff000;

	reset_floppy(st);

	if (st->maprom && (st->mrd[0].type || st->mrd[1].type)) {
		c->color[0] = 0x404;
		set_maprom(st);
	}
	
	for (int i = 0; i < MEMORY_REGIONS; i++) {
		if (i == MB_CHIP)
			c->color[0] = 0x400;
		if (i == MB_SLOW)
			c->color[0] = 0x040;
		if (i == MB_FAST)
			c->color[0] = 0x004;
		struct MemoryBank *mb = &st->membanks[i];
		if (mb->addr) {
			handlerambank(mb, st);
		}
	}
	
	c->color[0] = 0x440;
		
	// must be before set_cia
	for (int i = 0; i < 4; i++) {
		set_floppy(st->floppy_chunk[i], i, st);
	}

	c->color[0] = 0x444;

	set_agacolor(st->aga_colors_chunk);
	set_custom(st);
	for (int i = 0; i < 4; i++) {
		set_audio(st->audio_chunk[i], i);
	}
	for (int i = 0; i < 8; i++) {
		set_sprite(st->sprite_chunk[i], i);
	}
	set_cia(st->ciaa_chunk, 0);
	set_cia(st->ciab_chunk, 1);

	runit(st);
}

static void take_over(struct uaestate *st)
{
	// Copy stack, variables and code to safe location

	UBYTE *tempsp = tempmem_allocate(TEMP_STACK_SIZE, st);
	if (!tempsp) {
		printf("Out of memory for temp stack (%lu bytes).\n", TEMP_STACK_SIZE);
		return;
	}

	struct uaestate *tempst = (struct uaestate*)tempmem_allocate(sizeof(struct uaestate), st);
	if (!tempst) {
		printf("Out of memory for temp state variables (%lu bytes).\n", sizeof(struct uaestate));
		return;	
	}
	memcpy(tempst, st, sizeof(struct uaestate));

	struct Process *me = (struct Process*)FindTask(0);
	struct CommandLineInterface *cli = (struct CommandLineInterface*)((((ULONG)me->pr_CLI) << 2));
	if (!cli) {
		printf("CLI == NULL?\n");
		return;
	}
	ULONG *module = (ULONG*)(cli->cli_Module << 2);
	ULONG hunksize = module[-1] << 2;
	UBYTE *newcode = tempmem_allocate(hunksize, st);
	if (!newcode) {
		printf("Out of memory for temp code (%lu bytes).\n", hunksize);
		return;
	}
	memcpy(newcode, module, hunksize);
	
	// ugly relocation hack but jumps to other module (from asm.S) are always absolute..
	// TODO: process the executable after linking
	UWORD *cp = (UWORD*)newcode;
	for (int i = 0; i < hunksize / 2; i++) {
		// JSR/JMP xxxxxxxx.L?
		if (*cp == 0x4eb9 || *cp == 0x4ef9) {
			ULONG *ap = (ULONG*)(cp + 1);
			ULONG *app = (ULONG*)(*ap);
			void *addr = (void*)app;
			if (addr == runit || addr == callinflate) {
				*ap = (ULONG)addr - (ULONG)module + (ULONG)newcode;
				//printf("Relocated %08x: %08x -> %08x\n", cp, addr, *ap);
			}
		}
		cp++;
	}
	
	if (st->testmode) {
		printf("Test mode finished. Exiting.\n");
		return;
	}
	
	if (!st->nowait) {
		if (st->debug) {
			printf("Code=%08lx Stack=%08lx Data=%08lx. Press RETURN!\n", newcode, tempsp, tempst);
		} else {
			printf("Change floppy disk(s) now if needed. Press RETURN to start.\n");
		}
		UBYTE b;
		fread(&b, 1, 1, stdin);
		Delay(100); // So that key release gets processed by AmigaOS
	}
	
	if (SysBase->LibNode.lib_Version >= 37) {
		flushcache();
	}

	if (GfxBase->LibNode.lib_Version >= 37) {
		LoadView(NULL);
		WaitTOF();
		WaitTOF();	
	}
	
	OwnBlitter();
	WaitBlit();
	
	// No turning back!
	extern void *killsystem(UBYTE*, struct uaestate*, ULONG);
	killsystem(tempsp + TEMP_STACK_SIZE, tempst, (ULONG)processstate - (ULONG)module + (ULONG)newcode);
}

int main(int argc, char *argv[])
{
	FILE *f;
	UBYTE *b;
	ULONG size;
	UBYTE cname[5];
	struct uaestate *st;
	
	printf("ussload v" VER " (" REVDATE " " REVTIME ")\n");
	if (argc < 2) {
		printf("Syntax: ussload <statefile.uss> (parameters)\n");
		printf("- nowait = don't wait for return key.\n");
		printf("- debug = enable debug output.\n");
		printf("- test = test mode.\n");
		printf("- nomaprom = do not use map rom.\n");
		printf("- mmu = use MMU (If 68030, MMU is not used by default).\n");
		printf("- nommu = do not use MMU (68030/68040/68060).\n");
		printf("- nocache = disable caches before starting (68020+)\n");
		printf("- nocache2 = disable caches when taking over the system (68020+)\n");
		printf("- pause = restore state, wait left mouse button press\n");
		printf("- pal/ntsc = set PAL or NTSC mode (ECS/AGA only)\n");
		printf("- nofloppy = don't initialize floppy drives\n");
		return 0;
	}
	
	f = fopen(argv[1], "rb");
	if (!f) {
		printf("Couldn't open '%s'\n", argv[1]);
		return 0;
	}

	st = calloc(sizeof(struct uaestate), 1);
	if (!st) {
		printf("Out of memory.\n");
		return 0;
	}
	st->usemaprom = 1;
	st->canusemmu = 1;
	for(int i = 2; i < argc; i++) {
		if (!stricmp(argv[i], "debug"))
			st->debug = 1;
		if (!stricmp(argv[i], "test"))
			st->testmode = 1;
		if (!stricmp(argv[i], "nowait"))
			st->nowait = 1;
		if (!stricmp(argv[i], "nomaprom"))
			st->usemaprom = 0;
		if (!stricmp(argv[i], "nommu"))
			st->canusemmu = 0;
		if (!stricmp(argv[i], "mmu"))
			st->canusemmu = 2;
		if (!stricmp(argv[i], "nocache"))
			st->flags |= FLAGS_NOCACHE;
		if (!stricmp(argv[i], "nocache2"))
			st->flags |= FLAGS_NOCACHE2;
		if (!stricmp(argv[i], "pal"))
			st->flags |= FLAGS_FORCEPAL;
		if (!stricmp(argv[i], "ntsc"))
			st->flags |= FLAGS_FORCENTSC;
		if (!stricmp(argv[i], "pause"))
			st->flags |= FLAGS_PAUSE;
		if (!stricmp(argv[i], "nofloppy"))
			st->flags |= FLAGS_NOFLOPPY;
	}

	if ((SysBase->AttnFlags & AFF_68020) && !(SysBase->AttnFlags & AFF_68030) && SysBase->LibNode.lib_Version < 37) {
		detect030040();
	}
	if ((SysBase->AttnFlags & AFF_68040) && !(SysBase->AttnFlags & 0x80) && SysBase->LibNode.lib_Version < 45) {
		detect060();
	}
	
	UWORD attnFlags = SysBase->AttnFlags;

	if ((attnFlags & AFF_68030) && !(attnFlags & AFF_68040) && st->canusemmu == 1) {
		st->canusemmu = 0;
	}

	if (!(attnFlags & AFF_68030)) {
		st->canusemmu = 0;
	}

	if (st->canusemmu && (attnFlags & AFF_68040)) {
		if (!(detectmmu() & 0xc000)) {
			st->canusemmu = 0;
		}
	}
	
	if (!(attnFlags & AFF_68020)) {
		st->flags &= ~(FLAGS_NOCACHE | FLAGS_NOCACHE2);
	}
	
	if (st->canusemmu) {	
		if (!init_mmu(st)) {
			printf("ERROR: MMU page table allocation failed.\n");
			st->canusemmu = 0;
		} else {
			printf("MMU mode enabled.\n");
		}
	}

	if (!parse_pass_1(f, st)) {
		fseek(f, 0, SEEK_SET);
		if (!parse_pass_2(f, st)) {
			take_over(st);			
		} else {
			printf("Pass #2 failed.\n");
		}
	} else {
		printf("Pass #1 failed.\n");
	}
	
	free(st);
	
	fclose(f);

	free_allocations(st);

	return 0;
}
