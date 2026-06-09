/*
 * 64-bit risc-v definitions
 *
 * mkenum turns enums into defines for the assemblers in riscv64l.h
 */
#include <atom.h>

#define SYSTEM		0x73		/* op code */
#define ECALLINST	SYSTEM
#define COMPBITS	2		/* low bits, indicate compression */
#define ISCOMPRESSED(inst) (((inst) & MASK(COMPBITS)) != 3)

/* registers */
#define LINK	R1
#define USER	6			/* up-> */
#define MACH	7			/* m-> */
#define ARG	8

#define UREGSIZE (XLEN*(32+6))	/* room for pc, regs, csrs, mode, scratch */

/*
 * flag bit for sys->secstall, sys->rebooting: stall secondary cpus at
 * start of main.
 */
#define RBFLAGSTALL 1
/*
 * magic number for sys->secstall, sys->rebooting, newkern: stall secondary cpus
 * until sys->Reboot is ready, before calling reboot trampoline.  also tells
 * new kernel that it was loaded via reboot, thus secondaries are stalled.
 */
#define RBFLAGSET (0xecce | RBFLAGSTALL)

enum {
	Csrrw = 1,		/* add 4 for immediate mode using rs1 */
	Csrrs = 2,
	Csrrc = 3,
};

enum {				/* CSRs */
	FFLAGS	= 1,
	FRM	= 2,
	FCSR	= 3,

	SUPERBASE = 0x100,
	SSTATUS	= 0x100,
	/* SEDELEG & SIDELEG are gone as of v1.12 */
	SIE	= 0x104,
	STVEC	= 0x105,
	SCOUNTEREN = 0x106,
	SENVCFG	= 0x10a,	/* v1.12 */
	SSCRATCH = 0x140,
	SEPC	= 0x141,
	SCAUSE	= 0x142,
	STVAL	= 0x143,
	SIP	= 0x144,
	SATP	= 0x180,	/* page table root, paging mode (was sptbr) */

	MCHBASE	= 0x300,
	MSTATUS	= 0x300,
	MISA	= 0x301,
	MEDELEG	= 0x302,
	MIDELEG	= 0x303,
	MIE	= 0x304,
	MTVEC	= 0x305,
	MCOUNTEREN = 0x306,
	MENVCFG	= 0x30a,	/* v1.12 */
	MSTATUSH = 0x310,	/* RV32 only */
	MUCOUNTEREN = 0x310,	/* v1.9 only */
	MSCOUNTEREN = 0x311,	/* v1.9 only */
	MCOUNTINHIBIT = 0x320,	/* v1.12 */
	MSCRATCH = 0x340,
	MEPC	= 0x341,
	MCAUSE	= 0x342,
	MTVAL	= 0x343,
	MIP	= 0x344,
	/*
	 * PMP grants access only to S and U modes but may restrict access
	 * to M mode.
	 */
	PMPCFG	= 0x3a0,	/* rv64 array; only even-numbered ones exist */
	 Pmpr	= (1LL<<0),	/* per-byte flags for corr. pmpaddr */
	 Pmpw	= (1LL<<1),
	 Pmpx	= (1LL<<2),
	 Pmpatype = (3LL<<3),
	 Pmpnull= (0LL<<3),
	 Pmptor = (1LL<<3),
	 Pmpnapot = (3LL<<3),
	 Pmplock = (1LL<<7),	/* locked entry; entry also applies to M mode */
	PMPADDR	= 0x3b0,	/* array */

	MSECCFG	= 0x747,	/* v1.12 */

	HWPF0	= 0x7c3,	/* sifive pre-fetcher */
	HWPF1	= 0x7c4,

	CYCLO	= 0xc00,	/* read/write, says lying doc. */
	TIMELO	= 0xc01,
	INSTRETLO = 0xc02,	/* instructions retired */
	CYCHI	= 0xc80,	/* RV32 only */
	TIMEHI	= 0xc81,	/* RV32 only */
	INSTRETHI = 0xc82,	/* RV32 only */

	MVENDORID = 0xf11,
	MARCHID	= 0xf12,
	MIMPLID	= 0xf13,
	MHARTID	= 0xf14,

	/* vector csrs, available in all priv. modes */
	VSTART	= 0x8,
	VXSAT	= 0x9,
	VXRM	= 0xa,
	VCSR	= 0xf,
	VL	= 0xc20,
	VTYPE	= 0xc21,
	VLENB	= 0xc22,
};

enum {				/* xs & fs values in ?status */
	Off	= 0LL,		/* fp: disabled, to detect uses */
				/* will cause ill instr traps on fp use */
	Initial	= 1LL,		/* fp: on but initialised */
	Clean	= 2LL,		/* fp: loaded (from fpusave) but not changed */
	Dirty	= 3LL,		/* fp: fregs or fcsr differ from fpusave */
	Fsmask	= 3LL,
};

/*
 * starting with privileged isa 1.13, MISA.Mxl (high 2 bits of MISA) will be
 * fixed, not changeable.
 */
enum {				/* Mxl, Uxl and Sxl values */
	Mxlen32	= 1LL,
	Mxlen64 = 2LL,
	Mxlen128 = 3LL,
	Mxlenmask = 3LL,
};

enum {				/* [ms]status bits for RV32 & RV64 */
	/* user-mode interrupt service, thus Uie and Upie, have been removed */
	Sie	= (1LL<<1),	/* supervisor-mode intr enable */
	Mie	= (1LL<<3),	/* machine-mode intr enable */
	Spie	= (1LL<<5),	/* previous Sie */
	Ube	= (1LL<<6),	/* user big endian */
	Mpie	= (1LL<<7),	/* previous Mie (machine only) */
	Spp	= (1LL<<8),	/* previous priv. mode (super) */
	Vsshft	= 9,		/* V (vector) extension analogues of Fs* */
	Vsst	= (Fsmask<<Vsshft),
	Mpp	= (3LL<<11),	/* previous privilege mode (machine only) */
	Mppuser	= (0LL<<11),
	Mppsuper = (1LL<<11),
	Mpphype	= (2LL<<11),
	Mppmach	= (3LL<<11),
	Fsshft	= 13,
	Fsst	= (Fsmask<<Fsshft),	/* fpu state */
	Xsshft	= 15,
	Xs	= (Fsmask<<Xsshft),	/* additional extension state */
	Mprv	= (1LL<<17),	/* use Mpp priv.s for memory access (machine) */
	Sum	= (1LL<<18),	/* super access to user memory (super) */
	Mxr	= (1LL<<19),	/* make executable readable (super) */
	Tvm	= (1LL<<20),	/* trap vm instrs. (machine) */
	Tw	= (1LL<<21),	/* timeout wfi (machine) */
	Tsr	= (1LL<<22),	/* trap SRET in super mode */
	Vm1_9shft = 24,
//	Vm1_9	= (VMASK(5)<<Vm1_9shft), /* paging mode (priv 1.9 only) */
	Sd32	= (1ULL<<31),	/* dirty state in Fsst, Vsst, or Xs (RV32) */
	Uxlshft	= 32,
	Uxl	= (Mxlenmask<<Uxlshft), /* user xlen */
	Sxlshft	= 34,
	Sxl	= (Mxlenmask<<Sxlshft), /* super xlen (machine) */
	Sbe64	= (1LL<<36),	/* super big endian */
	Mbe64	= (1LL<<37),	/* machine big endian */
	Gva	= (1LL<<38),	/* guest virtual address */
	Mpv	= (1LL<<39),	/* machine previous virtualization mode */
	Sd64	= (1ULL<<63),	/* dirty state in Fsst, Vsst, or Xs (RV64) */

	/* mstatush (rv32 only) */
	Sbe	= (1<<4),	/* super big endian */
	Mbe	= (1<<5),	/* machine big endian */
};

enum {
	Defssts	= (Mxlen64<<Uxlshft|Sum|Mxr),
	Defmsts	= (Mxlen64<<Sxlshft|Defssts|Mppsuper),
};

enum {
	Rv64intr	= (1ull<<63), /* interrupt, not exception */
};

enum {				/* cause exception codes for interrupts */
	Supswintr	= 1,
	Vsupswintr	= 2,
	Mchswintr	= 3,
	Suptmrintr	= 5,
	Vsuptmrintr	= 6,
	Mchtmrintr	= 7,
	Supextintr	= 9,		/* global intr */
	Vsupextintr	= 10,		/* global intr */
	Mchextintr	= 11,		/* global intr */
	Supgextintr	= 12,		/* global intr */
	Hpmctrovflintr	= 13,
	Debugintr	= 14,
	Local0intr	= 16,
	Xtperfovflintr	= 17,
//	Luartintr	= Local0intr + 11,	/* this hart's uart */
	Nlintr		= 64,		/* # of local interrupts */
	Msdiff		= 2,		/* "M* - S*" bit */
};

/* local interrupt enables */
enum {			/* [ms]i[ep] bits for RV64.  only S* bits in si[ep] */
	Ssie	= (1<<Supswintr),	/* super sw */
	Msie	= (1<<Mchswintr),	/* machine sw */
	Stie	= (1<<Suptmrintr),	/* super timer */
	Mtie	= (1<<Mchtmrintr),	/* machine timer */
	Seie	= (1<<Supextintr),	/* super ext (sie); global (mie) */
	Meie	= (1<<Mchextintr),	/* machine external */
	Li0ie	= (1<<Local0intr),	/* local intr 0 enable */
//	Luie	= (1<<Luartintr),	/* this hart's uart */

	Superie	= (Seie|Stie|Ssie),
	Machie	= (Meie|Mtie|Msie),
};

enum {				/* cause exception codes for traps (?CAUSE) */
	Instaddralign	= 0,
	Instaccess	= 1,	/* e.g., executing no-X page */
	Illinst		= 2,	/* e.g., unknown csr or extension */
	Breakpt		= 3,
	Loadaddralign	= 4,
	Loadaccess	= 5,
	Storeaddralign	= 6,
	Storeaccess	= 7,	/* e.g., writing read-only page */
	Envcalluser	= 8,	/* system call from (virt.) user mode */
	Envcallsup	= 9,	/* environment call from (hyper) super mode */
	Envcallvsup	= 10,	/* environment call from virt. super mode */
	Envcallmch	= 11,	/* environment call from machine mode */
	Instpage	= 12,	/* page faults: no pte present */
	Loadpage	= 13,
	Debugexc	= 14,
	Storepage	= 15,
				/* hypervisor stuff from here down */
	Doubletrap	= 16,
	Swchk		= 18,
	Hwerr		= 19,
	Instgpage	= 20,	/* guest page faults: no pte present */
	Loadgpage	= 21,
	Virtinstr	= 22,	/* virtual instruction */
	Storegpage	= 23,
	Nexc,
};

enum {					/* CSR(SATP) */
	/* low bits are phys page table addr / PGSZ */
	/* optional ASID is in bits 59-44 */
	Satpasidshft	= 44,
//	Satpasidmask	= (VMASK(16)<<Satpasidshft),
//	Svmask		= (VMASK(4)<<60), /* paging enable modes */
	Svshft		= 60,
	Svnone		= (0ULL<<Svshft),
	Sv39		= (8ULL<<Svshft),
	Sv48		= (9ULL<<Svshft),
	Sv57		= (10ULL<<Svshft),	/* for future use */
	Sv64		= (11ULL<<Svshft),

	Sv39_1_9	= (9LL<<Vm1_9shft),	/* priv. spec 1.9 */
};

enum {					/* PTE; see mem.h PTE* */
	PteP		= 0x01LL,	/* Present/valid */
	PteR		= 0x02LL,	/* Read */
	PteW		= 0x04LL,	/* Write */
	PteX		= 0x08LL,	/* Execute */
	/* page accessible to user mode; only usable on leaves */
	PteU		= 0x10LL,
	PteG		= 0x20LL,	/* Global; used for upper addresses */
	PteA		= 0x40LL,	/* Accessed */
	PteD		= 0x80LL,	/* Dirty */

//	PteRSW		= (VMASK(2)<<8),/* reserved for sw use */

	PteLeaf		= (PteR|PteW|PteX), /* if 0, next level; non-0 leaf */
	PteNonleaf0	= (PteU|PteA|PteD),	/* zero on non-leaf pte */

	/* standard extension bits: Svpbmt extension in Sv39, Sv48 and Sv57 */
	PtePma		= 0,		/* PMA default, often cacheable */
	PteNc		= (1LL<<61),	/* non-cacheable memory */
	PteIo		= (2LL<<61),	/* non-cacheable i/o space */
	Ptemtmask	= (3LL<<61),	/* memory type mask */
	PteN		= (1ULL<<63),	/* Svnapot extension */

#ifdef XUANTIE_MMU
	/*
	 * xuantie bits, incompatible with priv. spec.
	 */
	PteXattrs	= (VMASK(5)<<59),
	PteXtSec	= (1LL<<59),	/* for trusted world (TEE) */
	PteXtShare	= (1LL<<60),	/* shareable: cacheable coherency in hw */
	PteXtBuf	= (1LL<<61),	/* bufferable */
	PteXtCache	= (1LL<<62),	/* cacheable */
	PteXtStrong	= (1ULL<<63),	/* strong order (a la x86), dev. regs */

	/* some implementations require A & D in leaf ptes to avoid faults */
//	Pteleafvalid	= (PteP|PteA|PteD|PteXtShare|PteXtBuf|PteXtCache|PteXtStrong),
	Pteleafvalid	= (PteP|PteA|PteD|PteXtShare|PteXtBuf|PteXtCache),
#else
	Pteleafvalid	= (PteP|PteA|PteD),
#endif
};

enum {
	/*
	 * there are 1024 possible interrupt sources, but nothing uses them
	 * all.  p550/eic7700x allows 520, uses irq 220.
	 */
	Ngintr		= 221,
	Firstirq	= 1,	/* plic intr id 0 means "no interrupt" */
};

enum {				/* [MS]ENVCFG */
	Havepbmt	= (1ull<<62),
};

/* SBI stuff */
#define HSM	0x48534dLL
#define HSMSUSP	3
#define SRST	0x53525354	/* ('S'<<24|'R'<<16|'S'<<8|'T') */
#define SRSTSHUT 0
#define SRSTCOLD 1		/* cold reboot */
#define SRSTWARM 2		/* warm reboot */
