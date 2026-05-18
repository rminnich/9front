/*
 * Time.
 *
 * HZ should divide 1000 evenly, ideally.
 * 100, 125, 200, 250 and 333 are okay.
 */
#define	HZ		100			/* clock frequency */
#define MHZ		(1000*1000)
#define	MS2HZ		(1000/HZ)		/* millisec per clock tick */
#define	TK2SEC(t)	((t)/HZ)		/* ticks to seconds */

enum {
	Mhz	= 1000 * 1000,

	GpioLow = 0,
	GpioHigh,
	GpioRising,
	GpioFalling,
	GpioEdge,
};

typedef struct Clint Clint;
typedef struct Conf	Conf;
typedef struct Confmem	Confmem;
typedef struct FPalloc	FPalloc;
typedef struct FPsave	FPsave;
typedef struct PFPU	PFPU;
typedef struct ISAConf	ISAConf;
typedef struct Label	Label;
typedef struct Lock	Lock;
typedef struct Memcache	Memcache;
typedef struct MMMU	MMMU;
typedef struct Mach	Mach;
typedef struct Page	Page;
typedef struct PhysUart	PhysUart;
typedef struct Pcidev	Pcidev;
typedef struct PMMU	PMMU;
typedef struct Proc	Proc;
typedef u64int		PTE;
typedef struct Soc	Soc;
typedef struct Uart	Uart;
typedef struct Ureg	Ureg;
typedef uvlong		Tval;
typedef void		KMap;

typedef struct Cpu Cpu;
#pragma incomplete Cpu

typedef struct Mallocs Mallocs;

#pragma incomplete Pcidev
#pragma incomplete Ureg

#define MAXSYSARG	5	/* for mount(fd, mpt, flag, arg, srv) */

/*
 *  parameters for sysproc.c
 */
// This does not seem to work. 
//#define AOUT_MAGIC	(Y_MAGIC)
// as a friend from BL once said: give it a fucking rest
#define AOUT_MAGIC (0x8e17)

struct Lock
{
	ulong	key;
	u32int	sr;
	uintptr	pc;
	Proc*	p;
	Mach*	m;
	int	isilock;
};

struct Label
{
	uintptr	sp;
	uintptr	pc;
};

struct FPsave
{
	uvlong	regs[32][2];

	ulong	control;
	ulong	status;
};

struct FPalloc
{
	FPsave;

	FPalloc	*link;	/* when context nests */
};

#define KFPSTATE

struct PFPU
{
	int	fpstate;
	int	kfpstate;
	FPalloc	*fpsave;
	FPalloc	*kfpsave;
};

enum
{
	/* these names are known to port/. They're not quite right 
	 * for RISC-V but we have to use them. */
	/* In this state, FP use will cause a trap. */
	FPinit,
	/* In this state, the FP is on but has not been used */
	FPidle,
	/* In this state, the FP is on but "clean". It is not completely
	 * clear why this state was needed, but it is in the spec. */
	FPclean,
	/* In this state, the FP has been changed in some way and
	 * must be saved off before it can be used by kernel, note handler,
	 * or other process. */
	FPdirty,
	FPinactive = FPidle,

	/* bits or'd with the state */
	FPnotify = 0x100,
};

struct Confmem
{
	uintptr	base;
	ulong	npage;
	uintptr	limit;
	uintptr	kbase;
	uintptr	klimit;
};

struct Conf
{
	ulong	nmach;		/* processors */
	ulong	nproc;		/* processes */
	Confmem	mem[3];		/* physical memory */
	ulong	npage;		/* total physical pages of memory */
	ulong	upages;		/* user page pool */
	ulong	copymode;	/* 0 is copy on write, 1 is copy on reference */
	ulong	ialloc;		/* max interrupt time allocation in bytes */
	ulong	pipeqsize;	/* size in bytes of pipe queues */
	ulong	nimage;		/* number of page cache image headers */
	ulong	nswap;		/* number of swap pages */
	int	nswppo;		/* max # of pageouts per segment pass */
	int	monitor;	/* flag */
};

/*
 * CLINT is Core-Local INTerrupt controller
 */

/* Clint arrays are indexed by hart (cpu) id */
struct Clint {
	/* non-zero lsb generates sw intr. unless SBI has neutered it */
	ulong	msip[4096];		/* aclint mswi */
	/* aclint mtimer */
	uvlong	mtimecmp[4095];		/* < mtime generates clock intr. */

	/*
	 * the XuanTie duplicates the above for supervisor mode access when the
	 * Clintee bit is set, which it is initially on the c910.
	 * also, mtime is missing and one has to instead read CSR(TIMELO).
	 */
	uvlong	mtime;
};

/*
 *  MMU stuff in Mach.
 */
struct MMMU
{
	PTE*	mmutop;		/* first level user page table */
};

/*
 *  MMU stuff in proc
 */
#define NCOLOR	1		/* 1 level cache, don't worry about VCE's */

struct PMMU
{
	union {
	Page	*mmufree;	/* mmuhead[0] is freelist head */
	Page	*mmuhead[PTLEVELS];
	};
	Page	*mmutail[PTLEVELS];
	int	asid;
	uintptr	tpidr;
};

#include "../port/portdat.h"

/*
 * Per processor information.
 *
 * The offsets of the first elements may be known
 * to low-level assembly code, so do not re-order nor channge type:
 *	machno, bootmachmode - rebootcode
 *	splpc		- splhi, spllo, splx
 *	proc, regsave	- strap, mtrap
 *	online, hartid	- start
 * changes here must be synchronised with byte offsets in riscv64ladd.h.
 */
struct Mach
{
	int	machno;			/* physical id of processor */
	uintptr	splpc;			/* pc of last caller to splhi */
	Proc*	proc;			/* current process on this processor */
	uintptr	regsave[2*5+1];	/* 24 (super mach)×(r2 r3 r4 r6 r9), mach r5 */
				/* saved @ trap entry */
	/* to be safe, include other stuff Geoff thought mattered.*/
	union {
		uvlong	*mtimecmp; /* 112 clint's mtimecmp for this hart */
		uvlong	*stimecmp; /* 112 clint's stimecmp for this hart */
	};
	int	online;		/* 120 flag: actually enabled */
	int	hartid;		/* 124 riscv cpu id; often not machno */
	Clint	*clint;		/* 128 currently-valid address */
	uintptr	_consuart;	/* 136 " */
	uchar	bootmachmode;	/* 144 flag: machine mode at boot time */
	uchar	probing;	/* 145 flag: probing an address */
	uchar	probebad;	/* 146 flag: probe failed: bad address */

	/* end of offsets known to asm */

	MMMU;

	PMach;

	int	fpstate;
	FPalloc	*fpsave;

	int	cputype;
	ulong	delayloop;

	/* from Geoff's port. */
	uvlong boottsc;
	uint plicctxt;
	uchar	clockintrsok;	/* flag: safe to call timerintr */
	int	clockintrdepth;
	int machmode;
	uint cpumhz;
	/* current value of this hart's clint->mtimecmp, in case unreadable */
	uvlong	timecmp;

	int	stack[1];
};

struct
{
	char	machs[MAXMACH];		/* active CPUs */
	int	exiting;		/* shutdown */
}active;

#define MACHP(n)	((Mach*)MACHADDR(n))

extern register Mach* m;			/* R7 */
extern register Proc* up;			/* R6 */
extern int normalprint;

/*
 *  a parsed plan9.ini line
 */
#define NISAOPT		8

struct ISAConf {
	char	*type;
	uvlong	port;
	int	irq;
	ulong	dma;
	ulong	mem;
	ulong	size;
	ulong	freq;

	int	nopt;
	char	*opt[NISAOPT];
};

/*
 * Horrid. But the alternative is 'defined'.
 */
#ifdef _DBGC_
#define DBGFLG		(dbgflg[_DBGC_])
#else
#define DBGFLG		(0)
#endif /* _DBGC_ */

int vflag;
extern char dbgflg[256];

#define dbgprint	print		/* for now */

/*
 *  hardware info about a device
 */
typedef struct {
	ulong	port;
	int	size;
} Devport;

struct DevConf
{
	ulong	intnum;			/* interrupt number */
	char	*type;			/* card type, malloced */
	int	nports;			/* Number of ports */
	Devport	*ports;			/* The ports themselves */
};

/* from Geoff's port */
typedef struct Intrstate Intrstate;
typedef struct Soc Soc;
typedef struct Sys Sys;
typedef struct Syspercpu Syspercpu;
typedef uvlong Mpl;
typedef uvlong Mreg;
typedef struct Clint Clint;
typedef struct Sbiret Sbiret;

struct Soc {
	/* physical addresses, vmapped to virtual addresses during start-up */
	uchar	*clint;		/* local intr ctlr also a timer */
	uchar	*plic;		/* external intr ctlr */
	uchar	*l2cache;	/* sifive l2 cache controller */
//	uchar	*pl2caches[HARTMAX]; /* sifive per-hart l2 cache controllers */
	uchar	*l3cache;	/* sifive l3 cache controller */
	uchar	*wdog0;		/* watchdog timer */
	uchar	*uart;		/* console */
	uchar	*ether[2];
	uchar	*pci;		/* ecam config space */
	uchar	*pcictl;	/* bridge & ctrl regs */
	uchar	*pciess;	/* soft reset, etc. */
	uchar	*sdmmc;
	uchar	*sdiosel;	/* for icicle */
//	uchar	*kramend;	/* if non-0, max kernel ram end. */
				/* when kern addr spc for ram < ram size */

	/* hardware and firmware choices and bugs */
	/*
	 * plic contexts are machine-dependent, but are usually hartid*2 +
	 * mode.  if there's a cut-down management hart at hartid 0 (i.e.,
	 * it's hobbled), they may instead be 1 + 2*(hartid-1) + mode, as
	 * on the icicle.
	 */
	uchar	hobbled; /* # puny visible mgmt harts (often only M context) */
	uchar	context0;	/* M context of first non-mgmt hart */
	uchar	dwuart;		/* flag: workaround synopsys 8250 */
	uchar	xscaleuart;	/* flag: workaround xscale uart */
	uchar	c910;		/* flag; workaround c910 bugs */

	/* hardware bugs discovered by probing */
	uchar	nodevamo;	/* flag: amo fails on device registers */
	uchar	clintlongs;	/* flag: clint accesses can't be vlong (bug) */
	uchar	clintlongsset;	/* flag: clintlongs is valid */

	/* hardware extensions discovered by probing */
	uchar	havesbihsm;
	uchar	havesbisrst;
	uchar	havecbo;	/* flag: have cache ops */

	/* other hardware extensions */
	uchar	svpbmt;		/* Svpbmt extension present */

	/* software choices */
	uchar	idlewake;	/* flag: wake idling cpus with ipis */
	uchar	ipiclint;	/* flag: use clint (not sbi) to send ipis */
	uchar	allintrs;	/* flag: enable all interrupts */
	uchar	poll;		/* flag: continual polling for i/o done */
	uchar	newmach;	/* flag: do new-machine tests & prints */
};

Soc soc;

#define PAGINGMODE 8
uvlong pagingmode;

int	bootmachmode; /* flag: machine mode at boot? same on all non-hobbled harts? */
uvlong	clintsperµs;	/* needed in delay before m is set */
uvlong	cpuhz;		/* from kernel config */
int	early;		/* flag: not ready for traps yet */
uchar	ether0mac[];
int	gotipi;
int	hartcnt;
uintptr	memtotal;	/* sum of all banks */
uintptr	mideleg, medeleg;
uintptr	misa;
int	nosbi;
int	nuart;	/* from kernel config; number of uarts in use in uartregs */
void	*origmtvec;
uvlong	pagingmode; /* set from PAGINGMODE, not really selectable at run time */
int	probehartid;
int	probingharts;
void	(*prstackmax)(void);
void	(*rvreset)(void);
/* server soc spec. requires 1 GHz clint timer, thus timebase */
uvlong	timebase;	/* from kernel config */
vlong	uartfreq;	/* from kernel config */
uintptr	uartregs[]; /* from kernel config; not yet used for anything significant */

/*
 * PLIC is Platform-Level Interrupt Controller.
 * Any interrupt connected here also shows up as Seie in SIP CSR.
 */

enum {
	Ncontexts = 31*512,
};
enum Cpumodes {			/* plic context offsets for priv modes */
	Machine, Super,		/* order matters, see plic contexts */
	Hyper, User,		/* these are optional */
};
typedef struct Plic Plic;
typedef struct Plictxt Plictxt;
struct Plic {				/* 64 MB */
	union {
		struct {
			/* intr source index. WARL in priv 1.9 */
			ulong	prio[1024];
			ulong	pendbits[1024];	/* 1st 32: bitmap of sources */
			/* index by context, 32 are bitmaps of sources */
			ulong	enabits[Ncontexts][32];
			/* 56K gap here */
		};
		struct {
			/* eic7700x has plic clock disable; on by default.  */
			uchar	_2_[2*MB - BY2PG];
			char	eicclkdisable;
		};
		struct {
			/*
			 * c910 has privilege control at 2MB-4,
			 * needed to allow S mode access
			 */
			uchar	_1_[2*MB - sizeof(ulong)];
			ulong	c910privctl;
		};
	};
	struct Plictxt {		/* at +2MB */
		ulong	priothresh; /* (lower bound of accepted prios)-1 WARL */
		ulong	claimcompl;	/* claim/complete */
		uchar	_2_[4096 - 2*sizeof(ulong)];
	} context[Ncontexts];		/* index by context */
};

struct Mallocs {
	void	*base;
	void	*use;
};

struct Sbiret {
	uvlong	status;
	uvlong	error;
};

struct Intrstate {
	uintptr	osts;		/* mach mode state to restore */
	Mpl	pl;		/* super " " " " */
	uchar	machmode;	/* flag: cpu in machine mode? */
};

enum Riscv_vendorid {
	Vsifive	= 0x489,
	Vthead	= 0x5b7,
};

struct Sys {
			/* computed constants to avoid mult. & div. */
			uvlong	clintsperµs;
			uvlong	clintsperhz;	/* clint ticks per HZ */
			uvlong	nsthresh;  /* ipi: min ns to next clock intr */
			uvlong	minclints;	/* min. interval until intr */
};

Sys asys;
Sys *sys;
