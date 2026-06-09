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
	CONSPUT(c)

#define DEBUG
#ifndef DEBUG
#define CONSOUT(c)
#define CONSPUT(c)
#define	CONSWAIT
#else					/* DEBUG */
#define CONSOUT(c) PRINT(c)
#define CONSPUT(c) MOV c, R10;\
				MOV $0, R16;\
				MOV $1, R17;\
				ECALL
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
