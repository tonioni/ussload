
CPU_CHUNK = 4
FPU_CHUNK = CPU_CHUNK+4
CIAA_CHUNK = FPU_CHUNK+4
CIAB_CHUNK = CIAA_CHUNK+4
CUSTOM_CHUNK = CIAB_CHUNK+4
AGA_COLORS_CHUNK = CUSTOM_CHUNK+4
FLOPPY_CHUNK = AGA_COLORS_CHUNK+4
AUDIO_CHUNK = FLOPPY_CHUNK+4*4
SPRITE_CHUNK = AUDIO_CHUNK+4*4
MMU_TABLE = SPRITE_CHUNK+4*8

	.text
	.globl _runit
	.globl _killsystem
	.globl _callinflate
	.globl _inflate
	.globl _flushcache
	.globl _detect060
	.globl _detect030040
	.globl _detectmmu

_flushcache:
	move.l a6,-(sp)
	move.l 4.w,a6
	jsr -0x27c(a6) | CacheClearU
	move.l (sp)+,a6
	rts

_detect030040:
	movem.l a5-a6,-(sp)
	move.l 4.w,a6
	lea .super030040(pc),a5
	jsr -0x1e(a6)
	movem.l (sp)+,a5-a6
	rts
.super030040:
	or.w #0x700,sr
	movec vbr,a0
	movem.l d2/d3,-(sp)
	move.l sp,a5
	subq.l #4,sp
	move.l 0x10(a0),d1
	move.l 0x2c(a0),d2
	lea .is68040(pc),a1
	move.l a1,0x10(a0)
	lea .atleast030(pc),a1
	move.l a1,0x2c(a0)
	btst #2,297(a6)
	bne.s .atleast030
	| pmove tt0,(sp)
	dc.w 0xf017,0x0a00
	| it is 68030
	bset #2,297(a6)
.atleast030:
	move.b 297(a6),d3
	or.b #0x4+0x8,297(a6) | AttnFlags (68030 + 68040)
	| movec CAAR,d0 (68020-68030 only)
	dc.w 0x4e7a,0x0802
	| it is not 68040
	move.b d3,297(a6) | AttnFlags
.is68040:
	move.l a5,sp
	move.l d1,0x10(a0)
	move.l d2,0x2c(a0)
	movem.l (sp)+,d2/d3
	rte

_detect060:
	movem.l a5-a6,-(sp)
	move.l 4.w,a6
	lea .super060(pc),a5
	jsr -0x1e(a6)
	movem.l (sp)+,a5-a6
	rts
.super060:
	or.w #0x700,sr
	movec vbr,a0
	move.l sp,a5
	move.l 0x10(a0),d1
	lea .not68060(pc),a1
	move.l a1,0x10(a0)
	| movec PCR,d0
	dc.w 0x4e7a,0x0808
	| it is 68060
	bset #7,297(a6) | AttnFlags
.not68060:
	move.l d1,0x10(a0)
	move.l a5,sp
	rte

_detectmmu:
	movem.l a5-a6,-(sp)
	move.l 4.w,a6
	lea .supermmu(pc),a5
	jsr -0x1e(a6)
	movem.l (sp)+,a5-a6
	rts
.supermmu:
	or.w #0x700,sr
	movec tc,d0
	btst #15,d0
	bne.s .done
	move.w d0,d1
	or.w #0x4000,d0
	movec d0,tc
	movec tc,d0
	btst #14,d0
	beq.s .done
	movec d1,tc
.done:
	rte

_callinflate:
	movem.l a4-a5,-(sp)
	move.l 4+2*4(sp),a4
	move.l 8+2*4(sp),a5
	bsr _inflate
	movem.l (sp)+,a4-a5
	rts

	| params: new stack 4, uaestate 8, func(uaestate) 12
_killsystem:
	move.l 8(sp),a0 | uaestate
	move.l a6,a1
	move.l 4.w,a6
	move.w 0x128(a6),(a0) | AttFlags
	move.l sp,d1
	move.l a0,d0
	move.l a5,a0
	lea .super(pc),a5
	jsr -0x1e(a6) | Supervisor
.super:
	move.w #0x2700,sr
	move.l a0,a5
	move.l a1,a6
	move.l d0,a0

	| debug trigger
	| move.w #0x1234,0xef0006

	| CIA: stop timers, clear interrupts
	bclr #0,0xbfde00
	bclr #0,0xbfdf00
	bclr #0,0xbfee01
	bclr #0,0xbfef01
	move.b #0x7f,0xbfdd00
	move.b #0x7f,0xbfed01
	tst.b 0xbfdd00
	tst.b 0xbfed01
	
	lea 0xdff000,a0
	move.w #0x7fff,d0
	move.w d0,0x96(a0)
	move.w d0,0x9a(a0)
	move.w d0,0x9c(a0)
	move.w d0,0x9e(a0)
	move.l d1,a0
	
	move.l 8(a0),d0
	bsr.w mmu_enable
	
	move.l 4(a0),sp | new temp super stack
	move.l 8(a0),-(sp) | uaestate
	move.l 12(a0),a0 | func
	jsr (a0) | func(uaestate)
	| never returns

_runit:
	move.l 4(sp),a4	| get pointer to struct uaestate
	subq.l #8,sp
	lea 0xdff000,a6
	
	| debug trigger
	| move.w #0x1234,0xef000a

	| restore FPU
	move.l FPU_CHUNK(a4),d0
	beq.s .nofpu
	move.l d0,a0
	| system has FPU?
	move.w (a4),d1
	btst #4,d1
	beq.s .nofpu
	clr.l -(sp)
	| FPU -> NULL state
	frestore (sp)+
	btst #6,4(a0) | flag bit 30 (=old FPU state)
	bne.s .oldfpustate
	btst #5,4(a0) | flag bit 29 (=new FPU state)
	beq.s .loadfpu
	btst #0,4+8*10+4+4+4+4+4+3(a0)
	beq.s .nofpu
	bra.s .loadfpu
.oldfpustate:
	btst #0,4+3(a0) | flag bit 0: 1 = NULL
	bne.s .nofpu	
.loadfpu:
	move.l (a0),a0 | model points to pre-processed data
	fmovem.x (a0)+,fp0-fp7
	fmove.l (a0)+,fpcr
	fmove.l (a0)+,fpsr
	fmove.l (a0)+,fpiar
.nofpu:	
	
	| find last line
	moveq #0,d5
	lea 0x6(a6),a2
.wait1:
	cmp.b #250,(a2)
	bne.s .wait1
.wait1a:
	cmp.b #2,(a2)
	bne.s .wait1a
.wait1b:
	move.w 4(a6),d0
	btst #0,d0
	beq.s .wait1c
	move.b (a2),d0
	cmp.b d0,d5
	bcc.s .wait1b
	move.b d0,d5
	bra.s .wait1b
.wait1c:
	cmp.b #160,(a2)
	bne.s .wait1c

	| last line - 15
	sub.b #15,d5
.wait3:
	cmp.b (a2),d5
	bne.s .wait3

	| debug trigger
	| move.w #0x1234,0xef0004

 	| don't play annoying sounds if pause
	btst #3,3(a4)
	bne.s .pausemode1
	bsr begin_audio
.pausemode1:

	| restore possible side-effect causing
	| custom bits as late as possible
	move.l CIAA_CHUNK(a4),(sp)
	clr.l 4(sp)
	bsr _set_cia_final
	move.l CIAB_CHUNK(a4),(sp)
	addq.l #1,4(sp)
	bsr _set_cia_final
	move.l CUSTOM_CHUNK(a4),(sp)
	bsr _set_custom_final

	| pause
	btst #3,3(a4)
	beq.s .pausemode2
.pausemode3:
	btst #6,0xbfe001
	bne.s .pausemode3
	| debounce delay
.pausemode4:
	cmp.b 6(a6),d5
	bne.s .pausemode4
	subq.b #1,d5
.pausemode5:
	cmp.b 6(a6),d5
	bne.s .pausemode5
	addq.b #1,d5
	btst #6,0xbfe001
	beq.s .pausemode4
	bsr begin_audio	
.pausemode2:

	| restore CPU state
	move.w (a4),d1
	move.l CPU_CHUNK(a4),a0
	| stack pointers
	move.l 4+4+60+4+2+2+4(a0),SP
	move.l 4+4+60+4+2+2(a0),a1 | USP
	move.l a1,USP

.cpu68020:
	| 68020 (or higher)
	cmp.l #68020,(a0)
	bcs.s .cpu68010
	btst #1,d1
	beq.s .cpu68010
	lea 4+4+60+4+2+2+4+4+2+4+(4+4+4)(a0),a1
	move.l (a1)+,d0
	btst #3,d1
	bne.s .cpu68020_040
	| CAAR is 68020-68030 only. Skip if 68040+
	| movec d0,CAAR
	dc.w 0x4e7b,0x0802
.cpu68020_040:
	move.l (a1)+,d0
	movec d0,CACR
	move.l (a1)+,d0
	btst #7,d1
	bne.s .cpu68010
	| MSP is 68020-68040 only. Skip if 68060.
	movec d0,MSP
.cpu68010:

	| 68010 (or higher)
	cmp.l #68010,(a0)
	bcs.s .cpu68000
	btst #0,d1
	beq.s .cpu68000	
	lea 4+4+60+4+2+2+4+4+2+4(a0),a1
	move.l (a1)+,d0
	movec d0,DFC
	move.l (a1)+,d0
	movec d0,SFC
	move.l (a1)+,d0
	movec d0,VBR
.cpu68000:

	btst #0,d1
	beq.s .nocpu68010
	btst #1,d1
	beq.s .nocpu68020
	btst #0,3(a4) | nocache?
	beq.s .nocpu68020
	moveq #0,d0
	movec d0,CACR
.nocpu68020:
	| 68010+ stack frame fix
	move.w #0x0020,-(sp) | Format 0, Trap #0
.nocpu68010:

	| prepare CIA timer control values
	move.l CIAA_CHUNK(a4),a1
	move.b 15(a1),d6
	swap d6
	move.b 14(a1),d6
	move.l CIAB_CHUNK(a4),a1
	move.b 15(a1),d7
	swap d7
	move.b 14(a1),d7

	| PC, SR, registers
	move.l 4+4+60(a0),-(sp) | PC
	move.w 4+4+60+4+2+2+4+4(a0),-(sp) | SR
	movem.l 4+4(a0),d0-d5
	movem.l 4+4+8*4+4+4(a0),a2-a6
	add.w #4+4+6*4,a0

	| debug trigger
	| move.w #0x1234,0xef0000

	lea 0xbfde00,a1

	| wait last line
.wait2:
	tst.b 0xdff006
	bne.s .wait2

	| start timers
	move.b d6,0xbfee01-0xbfde00(a1)
	swap d6
	move.b d6,0xbfef01-0xbfde00(a1)
	move.b d7,(a1)
	swap d7
	move.b d7,0x100(a1)
	
	| restore remaining registers
	movem.l (a0),d6-d7/a0-a1
	
	rte | GO! GO! GO!

mmu_enable:
	movem.l d0-d2/a0-a2,-(sp)
	move.l d0,a2

	| debug trigger
	| move.w #0x1234,0xef0008

	move.l MMU_TABLE(a2),d2
	beq.s .mmuend

	move.w (a2),d1
	btst #3,d1
	beq.s .no68040

	| 68040/060
	movec d2,urp
	movec d2,srp
	cpusha dc
	cinva dc
	pflusha
	move.l #0x8000,d0
	movec d0,tc
	moveq #0,d0
	movec d0,itt0
	movec d0,itt1
	movec d0,dtt0
	movec d0,dtt1
	bra.s .mmuend

.no68040:
	| 68030?
	btst #2,d1
	beq.s .mmuend

	sub.w #12,sp
	move.l #0x00c07760,(sp)
	| pmove (sp),tc
	dc.w 0xf017,0x4000
	move.l #0x80000002,4(sp)
	move.l d2,8(sp)
	| pmove 4(sp),crp
	dc.w 0xf02f,0x4c00,0x0004
	bset #7,(sp)
	| pmove (sp),tc
	dc.w 0xf017,0x4000
	clr.l (sp)
	| pmove (sp),tt0
	dc.w 0xf017,0x0800
	| pmove (sp),tt1
	dc.w 0xf017,0x0c00
	add.w #12,sp

.mmuend:
	movem.l (sp)+,d0-d2/a0-a2
	rts

begin_audio:
	move.l CUSTOM_CHUNK(a4),a0
	move.w 4+0x96(a0),d0 | DMACON
	and.w #15,d0
	or.w #0x8200,d0
	| enable audio DMA
	move.w d0,0x96(a6)
	| wait few lines, enough time for audio channels to enter normal 2/3 states
	addq.b #4,d5
.audwait:
	cmp.b (a2),d5
	bne.s .audwait
	| restore latched AUDxLEN and AUDxPT
	move.l a4,-(sp)
	bsr _set_audio_final
	addq.l #4,sp
	rts
