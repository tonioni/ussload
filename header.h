
#define TEMP_STACK_SIZE 6000

#define ALLOCATIONS 64

struct Allocation
{
	struct MemHeader *mh;
	UBYTE *addr;
	ULONG size;
};

struct MemoryBank
{
	UBYTE *addr;
	ULONG flags;
	UBYTE *targetaddr;
	ULONG size;
	ULONG targetsize;
	ULONG offset;
	UBYTE chunk[5];
};

// CHIP, SLOW, FAST
#define MEMORY_REGIONS 3
#define MB_CHIP 0
#define MB_SLOW 1
#define MB_FAST 2

#define MAPROM_ACA500 1
#define MAPROM_ACA500P 2
#define MAPROM_ACA1221EC 3
#define MAPROM_ACA12xx 4
#define MAPROM_GVP 5
#define MAPROM_BLIZZARD12x0 6
#define MAPROM_ACA1233N 7
#define MAPROM_ACA1221LC 8
#define MAPROM_ACA1234 9
#define MAPROM_MMU 255

#define FLAGS_NOCACHE 1
#define FLAGS_FORCEPAL 2
#define FLAGS_FORCENTSC 4
#define FLAGS_PAUSE 8
#define FLAGS_NOCACHE2 16
#define FLAGS_NOFLOPPY 32

struct mapromdata
{
	UWORD type;
	ULONG config;
	ULONG addr;
	APTR board;
	UBYTE *memunavailable_start;
	UBYTE *memunavailable_end;
};

#define MAX_EXTRARAM 4

struct extraram
{
	UBYTE *base;
	UBYTE *ptr;
	ULONG size;
	struct MemHeader *head;
};

#define HWTYPE_GENERIC 0
#define HWTYPE_CDTV 1
#define HWTYPE_CD32 2

struct uaestate
{
	ULONG flags;
	UBYTE *cpu_chunk;
	UBYTE *fpu_chunk;
	UBYTE *ciaa_chunk, *ciab_chunk;
	UBYTE *custom_chunk;
	UBYTE *aga_colors_chunk;
	UBYTE *floppy_chunk[4];
	UBYTE *audio_chunk[4];
	UBYTE *sprite_chunk[8];
	UBYTE *cd32_chunk;
	UBYTE *cdtv_chunk, *cdtv_dmac_chunk;
	ULONG *MMU_Level_A;
	UBYTE *vbr;
	UBYTE *debug_entry;

	UBYTE *maprom;
	ULONG mapromsize;
	ULONG maprom_memlimit;
	struct mapromdata mrd[2];

	ULONG errors;

	struct MemHeader *mem_allocated[MEMORY_REGIONS];
	struct MemoryBank membanks[MEMORY_REGIONS];

	WORD num_allocations;
	struct Allocation allocations[ALLOCATIONS];

	struct extraram eram[MAX_EXTRARAM];
	
	WORD hwtype;
	UWORD attnflags;
	WORD mmutype;
	UBYTE *page_ptr;
	ULONG page_free;
	
	UWORD romver, romrev;
	ULONG exceptionmask;
	UBYTE agastate;
	UBYTE usemaprom;
	UBYTE debug;
	UBYTE testmode;
	UBYTE nowait;
	UBYTE canusemmu;
	UBYTE mmuused;
};

UBYTE *extra_allocate(ULONG size, ULONG alignment, struct uaestate *st);

BOOL map_region(struct uaestate *st, void *addr, void *physaddr, ULONG size, BOOL invalid, BOOL writeprotect, BOOL supervisor, UBYTE cachemode);
BOOL unmap_region(struct uaestate *st, void *addr, ULONG size);
BOOL init_mmu(struct uaestate *st);

