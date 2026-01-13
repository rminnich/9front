/* generated */
#include <atom.h>

#define SYSTEM		0x73		/* op code */
#define ECALLINST	SYSTEM
#define COMPBITS	2		/* low bits, indicate compression */
#define ISCOMPRESSED(inst) (((inst) & MASK(COMPBITS)) != 3)

#define LINK	R1
#define USER	6			/* up-> */
#define MACH	7			/* m-> */
#define ARG	8

#define UREGSIZE (XLEN*(32+6))	/* room for pc, regs, csrs, mode, scratch */

#define RBFLAGSTALL 1
#define RBFLAGSET (0xecce | RBFLAGSTALL)

#define Csrrw	1	/* add 4 for immediate mode using rs1 */
#define Csrrs	2
#define Csrrc	3

#define FFLAGS	1
#define FRM	2
#define FCSR	3

#define SUPERBASE	0x100
#define SSTATUS	0x100
#define SIE	0x104
#define STVEC	0x105
#define SCOUNTEREN	0x106
#define SENVCFG	0x10a	/* v1.12 */
#define SSCRATCH	0x140
#define SEPC	0x141
#define SCAUSE	0x142
#define STVAL	0x143
#define SIP	0x144
#define SATP	0x180	/* page table root, paging mode (was sptbr) */

#define MCHBASE	0x300
#define MSTATUS	0x300
#define MISA	0x301
#define MEDELEG	0x302
#define MIDELEG	0x303
#define MIE	0x304
#define MTVEC	0x305
#define MCOUNTEREN	0x306
#define MENVCFG	0x30a	/* v1.12 */
#define MSTATUSH	0x310	/* RV32 only */
#define MUCOUNTEREN	0x310	/* v1.9 only */
#define MSCOUNTEREN	0x311	/* v1.9 only */
#define MCOUNTINHIBIT	0x320	/* v1.12 */
#define MSCRATCH	0x340
#define MEPC	0x341
#define MCAUSE	0x342
#define MTVAL	0x343
#define MIP	0x344
#define PMPCFG	0x3a0	/* rv64 array; only even-numbered ones exist */
#define Pmpr	(1LL<<0)	/* per-byte flags for corr. pmpaddr */
#define Pmpw	(1LL<<1)
#define Pmpx	(1LL<<2)
#define Pmpatype	(3LL<<3)
#define Pmpnull=	
#define Pmptor	(1LL<<3)
#define Pmpnapot	(3LL<<3)
#define Pmplock	(1LL<<7)	/* locked entry; entry also applies to M mode */
#define PMPADDR	0x3b0	/* array */

#define MSECCFG	0x747	/* v1.12 */

#define HWPF0	0x7c3	/* sifive pre-fetcher */
#define HWPF1	0x7c4

#define CYCLO	0xc00	/* read/write, says lying doc. */
#define TIMELO	0xc01
#define INSTRETLO	0xc02	/* instructions retired */
#define CYCHI	0xc80	/* RV32 only */
#define TIMEHI	0xc81	/* RV32 only */
#define INSTRETHI	0xc82	/* RV32 only */

#define MVENDORID	0xf11
#define MARCHID	0xf12
#define MIMPLID	0xf13
#define MHARTID	0xf14

#define VSTART	0x8
#define VXSAT	0x9
#define VXRM	0xa
#define VCSR	0xf
#define VL	0xc20
#define VTYPE	0xc21
#define VLENB	0xc22

#define Off	0LL	/* fp: disabled, to detect uses */
#define Initial	1LL	/* fp: on but initialised */
#define Clean	2LL	/* fp: loaded (from fpusave) but not changed */
#define Dirty	3LL	/* fp: fregs or fcsr differ from fpusave */
#define Fsmask	3LL

#define Mxlen32	1LL
#define Mxlen64	2LL
#define Mxlen128	3LL
#define Mxlenmask	3LL

#define Sie	(1LL<<1)	/* supervisor-mode intr enable */
#define Mie	(1LL<<3)	/* machine-mode intr enable */
#define Spie	(1LL<<5)	/* previous Sie */
#define Ube	(1LL<<6)	/* user big endian */
#define Mpie	(1LL<<7)	/* previous Mie (machine only) */
#define Spp	(1LL<<8)	/* previous priv. mode (super) */
#define Vsshft	9	/* V (vector) extension analogues of Fs* */
#define Vsst	(Fsmask<<Vsshft)
#define Mpp	(3LL<<11)	/* previous privilege mode (machine only) */
#define Mppuser	(0LL<<11)
#define Mppsuper	(1LL<<11)
#define Mpphype	(2LL<<11)
#define Mppmach	(3LL<<11)
#define Fsshft	13
#define Fsst	(Fsmask<<Fsshft)	/* fpu state */
#define Xsshft	15
#define Xs	(Fsmask<<Xsshft)	/* additional extension state */
#define Mprv	(1LL<<17)	/* use Mpp priv.s for memory access (machine) */
#define Sum	(1LL<<18)	/* super access to user memory (super) */
#define Mxr	(1LL<<19)	/* make executable readable (super) */
#define Tvm	(1LL<<20)	/* trap vm instrs. (machine) */
#define Tw	(1LL<<21)	/* timeout wfi (machine) */
#define Tsr	(1LL<<22)	/* trap SRET in super mode */
#define Vm1_9shft	24
#define Vm1_9	(VMASK(5)<<Vm1_9shft)	/* paging mode (priv 1.9 only) */
#define Sd32	(1ULL<<31)	/* dirty state in Fsst, Vsst, or Xs (RV32) */
#define Uxlshft	32
#define Uxl	(Mxlenmask<<Uxlshft)	/* user xlen */
#define Sxlshft	34
#define Sxl	(Mxlenmask<<Sxlshft)	/* super xlen (machine) */
#define Sbe64	(1LL<<36)	/* super big endian */
#define Mbe64	(1LL<<37)	/* machine big endian */
#define Gva	(1LL<<38)	/* guest virtual address */
#define Mpv	(1LL<<39)	/* machine previous virtualization mode */
#define Sd64	(1ULL<<63)	/* dirty state in Fsst, Vsst, or Xs (RV64) */

#define Sbe	(1<<4)	/* super big endian */
#define Mbe	(1<<5)	/* machine big endian */

#define Defssts	(Mxlen64<<Uxlshft|Sum|Mxr)
#define Defmsts	(Mxlen64<<Sxlshft|Defssts|Mppsuper)

#define Rv64intr	(1ull<<63)	/* interrupt, not exception */

#define Supswintr	1
#define Vsupswintr	2
#define Mchswintr	3
#define Suptmrintr	5
#define Vsuptmrintr	6
#define Mchtmrintr	7
#define Supextintr	9	/* global intr */
#define Vsupextintr	10	/* global intr */
#define Mchextintr	11	/* global intr */
#define Supgextintr	12	/* global intr */
#define Hpmctrovflintr	13
#define Debugintr	14
#define Local0intr	16
#define Xtperfovflintr	17
#define Nlintr	64	/* # of local interrupts */
#define Msdiff	2	/* "M* - S*" bit */

#define Ssie	(1<<Supswintr)	/* super sw */
#define Msie	(1<<Mchswintr)	/* machine sw */
#define Stie	(1<<Suptmrintr)	/* super timer */
#define Mtie	(1<<Mchtmrintr)	/* machine timer */
#define Seie	(1<<Supextintr)	/* super ext (sie); global (mie) */
#define Meie	(1<<Mchextintr)	/* machine external */
#define Li0ie	(1<<Local0intr)	/* local intr 0 enable */

#define Superie	(Seie|Stie|Ssie)
#define Machie	(Meie|Mtie|Msie)

#define Instaddralign	0
#define Instaccess	1	/* e.g., executing no-X page */
#define Illinst	2	/* e.g., unknown csr or extension */
#define Breakpt	3
#define Loadaddralign	4
#define Loadaccess	5
#define Storeaddralign	6
#define Storeaccess	7	/* e.g., writing read-only page */
#define Envcalluser	8	/* system call from (virt.) user mode */
#define Envcallsup	9	/* environment call from (hyper) super mode */
#define Envcallvsup	10	/* environment call from virt. super mode */
#define Envcallmch	11	/* environment call from machine mode */
#define Instpage	12	/* page faults: no pte present */
#define Loadpage	13
#define Debugexc	14
#define Storepage	15
#define Doubletrap	16
#define Swchk	18
#define Hwerr	19
#define Instgpage	20	/* guest page faults: no pte present */
#define Loadgpage	21
#define Virtinstr	22	/* virtual instruction */
#define Storegpage	23

#define Satpasidshft	44
#define Satpasidmask	(VMASK(16)<<Satpasidshft)
#define Svmask	(VMASK(4)<<60)	/* paging enable modes */
#define Svshft	60
#define Svnone	(0ULL<<Svshft)
#define Sv39	(8ULL<<Svshft)
#define Sv48	(9ULL<<Svshft)
#define Sv57	(10ULL<<Svshft)	/* for future use */
#define Sv64	(11ULL<<Svshft)

#define Sv39_1_9	(9LL<<Vm1_9shft)	/* priv. spec 1.9 */

#define PteP	0x01LL	/* Present/valid */
#define PteR	0x02LL	/* Read */
#define PteW	0x04LL	/* Write */
#define PteX	0x08LL	/* Execute */
#define PteU	0x10LL
#define PteG	0x20LL	/* Global; used for upper addresses */
#define PteA	0x40LL	/* Accessed */
#define PteD	0x80LL	/* Dirty */

#define PteRSW	(VMASK(2)<<8)	/* reserved for sw use */

#define PteLeaf	(PteR|PteW|PteX)	/* if 0, next level; non-0 leaf */
#define PteNonleaf0	(PteU|PteA|PteD)	/* zero on non-leaf pte */

#define PtePma	0	/* PMA default, often cacheable */
#define PteNc	(1LL<<61)	/* non-cacheable memory */
#define PteIo	(2LL<<61)	/* non-cacheable i/o space */
#define Ptemtmask	(3LL<<61)	/* memory type mask */
#define PteN	(1ULL<<63)	/* Svnapot extension */

#ifdef XUANTIE_MMU
#define PteXattrs	(VMASK(5)<<59)
#define PteXtSec	(1LL<<59)	/* for trusted world (TEE) */
#define PteXtShare	(1LL<<60)	/* shareable: cacheable coherency in hw */
#define PteXtBuf	(1LL<<61)	/* bufferable */
#define PteXtCache	(1LL<<62)	/* cacheable */
#define PteXtStrong	(1ULL<<63)	/* strong order (a la x86), dev. regs */

#define Pteleafvalid	(PteP|PteA|PteD|PteXtShare|PteXtBuf|PteXtCache)
#else
#define Pteleafvalid	(PteP|PteA|PteD)
#endif

#define Ngintr	221
#define Firstirq	1	/* plic intr id 0 means "no interrupt" */

#define Havepbmt	(1ull<<62)

#define HSM	0x48534dLL
#define HSMSUSP	3
#define SRST	0x53525354	/* ('S'<<24|'R'<<16|'S'<<8|'T') */
#define SRSTSHUT 0
#define SRSTCOLD 1		/* cold reboot */
#define SRSTWARM 2		/* warm reboot */
/*
 * riscv64 assembler definitions, including multi-line macros that ../mk/mkenum
 * can't cope with.  appended to mkenum output to make riscv64l.h.
 */

#undef DEBUG

/* instructions not yet known to ia and ja */
#define ECALL	SYS $0
#define EBREAK	SYS $1
#define URET	SYS $2
#define SRET	SYS $0x102
#define WFI	SYS $0x105
#define MRET	SYS $0x302

#define SPLHI	CSRRC CSR(SSTATUS), $Sie, R0; FENCE
#define SPLLO	FENCE; CSRRS CSR(SSTATUS), $Sie, R0

/* -1 to knock user/kernel bit off */
#define ENSURELOW(r)	SLL $(64-(VMBITS-1)), r; SRL $(64-(VMBITS-1)), r
#define ENSUREHIGH(r)	MOV $KZERO, R(MCHTMP); OR R(MCHTMP), r

/*
 * model-specific definitions
 */
#ifdef C910
#include "rvb/riscv64ladd.h"
#else
/* FENCE before loading SATP ensures changes to new page table are in memory */
#define BARR_SFENCE_VMA	SFENCE_VMA
#define LOADSATP(reg)	FENCE; FENCE_I; \
	SFENCE_VMA(0, 0); \
	MOV reg, CSR(SATP); \
	SFENCE_VMA(0, 0); \
	FENCE_I; FENCE
#endif

/*
 * strap and mtrap macros
 */
#define S(n)	MOV R(n), ((n)*XLEN)(R2)  /* save (push) Rn on stack */
#define P(n)	MOV ((n)*XLEN)(R2), R(n)  /* restore (pop) Rn from stack */

/*
 * emergency or debug printing
 */

/* always prints */
#define PRINT(c) \
	MOV	c, R(TMP); \
	MOVW	R(TMP), (R(UART0)); \
	FENCE

#ifndef DEBUG
#define CONSOUT(c)
#define CONSPUT(c)
#define	CONSWAIT
#else					/* DEBUG */
#define CONSOUT(c) PRINT(c)

#ifdef SIFIVEUART
#define CONSPUT(c) \
	FENCE; \
	MOVWU	(0*4)(R(UART0)), R(TMP); /* reg 0 is txdata */ \
	MOVW	$(1ul<<31), R(TMP2); \
	AND	R(TMP2), R(TMP);	/* notready bit */ \
	BNE	R(TMP), -4(PC); \
	MOV	c, R(TMP); \
	MOVW	R(TMP), (R(UART0)); \
	FENCE
#define CONSWAIT \
	FENCE; \
	MOVWU	(0*4)(R(UART0)), R(TMP); /* reg 0 is txdata */ \
	MOVW	$(1ul<<31), R(TMP2); \
	AND	R(TMP2), R(TMP);	/* notready bit */ \
	BNE	R(TMP), -4(PC)
#else					/* SIFIVEUART */
/* i8250 */
#define CONSPUT(c) \
	FENCE; \
	MOVWU	(5*4)(R(UART0)), R(TMP); /* 8250 Lsr is 5 */ \
	AND	$0x20, R(TMP);		/* Thre (Thr empty) is 0x20 */ \
	BEQ	R(TMP), -3(PC); \
	MOV	c, R(TMP); \
	MOVW	R(TMP), (R(UART0));	/* reg 0 is Thr */ \
	FENCE
#define CONSWAIT \
	FENCE; \
	MOVWU	(5*4)(R(UART0)), R(TMP); /* 8250 Lsr is 5 */ \
	AND	$0x20, R(TMP);		/* Thre (Thr empty) is 0x20 */ \
	BEQ	R(TMP), -3(PC)
#endif					/* SIFIVEUART */
#endif					/* DEBUG */

#define CALLTRAP \
	MOV	R2, R(ARG);		/* Ureg* */ \
	SUB	$(2*XLEN), R2; /* leave needed room (return PC & dummy arg?) */\
/**/	JAL	LINK, trap(SB);		/* to C: trap(sp) */ \
	ADD	$(2*XLEN), R2; \
	SPLHI

/*
 * pop from Ureg regs except MACH, USER, SB, R2; and pop a few CSRs.
 *
 * mips ports restore only M(STATUS) and M(EPC), but have no *IE.
 * there should be no need (or good reason) to restore SIE
 * (and SSCRATCH, STVAL, and SCAUSE).
 */
#define POPCSR_MOSTREGS(EPC, STATUS) \
	MOV	0(R2), R4; MOV R4, CSR(EPC); \
	MOV (32*XLEN)(R2), R4; MOV R4, CSR(STATUS);	/**/ \
	JAL	LINK, popr8_31(SB); \
	MOV	(1*XLEN)(R2), R1;	/* restore link reg */ \
	MOV	(4*XLEN)(R2), R4;	/* restore saved j[acl] temporary */ \
	MOV	(5*XLEN)(R2), R5

/*
 * byte offsets into Mach (m) mainly for m->regsave, for assembler,
 * where XLEN is defined.
 */
#define MMACHNO		0
#define SPLPC		XLEN
#define MACHPROC	(2*XLEN)
#define SAVESR2		(3*XLEN)
#define SAVESR3		(4*XLEN)
#define SAVESR4		(5*XLEN)
#define SAVESR6		(6*XLEN)
#define SAVESR9		(7*XLEN)
#define SAVEMR2		(8*XLEN)
#define SAVEMR3		(9*XLEN)
#define SAVEMR4		(10*XLEN)
#define SAVEMR6		(11*XLEN)
#define SAVEMR9		(12*XLEN)
// #define SAVEMR5		(13*XLEN)
#define MTIMECMP	(14*XLEN)	/* physical addr., for mtrap.s */
#define ONLINE		(15*XLEN)
#define MACHHARTID	(ONLINE+4)
#define MCLINT		(MACHHARTID+4)
#define MCONSUART	(MCLINT+8)
#define MMACHMODE	(MCONSUART+8)
#define MPROBING	(MMACHMODE+1)
#define MPROBEBAD	(MPROBING+1)

/*
 * byte offsets into Sys->Reboot.
 */
#define RBTRAMP	0			/* symbol unused */
#define RBENTRY XLEN
#define RBCODE	(2*XLEN)
#define RBSIZE	(3*XLEN)
#define RBSTALL	(4*XLEN)		/* symbol unused */
#define RBSATP	(5*XLEN)		/* unused */
