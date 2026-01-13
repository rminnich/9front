/*
 * assembly-language machine assist for riscv rv64
 */
#include "mem.h"
#include "riscv64l.h"

/* Zbb extension, as found in sifive u74 */
#define CLZ(rs1, rd) \
	WORD $(0x30<<25 | 0<<20 | (rs1)<<15 | 1<<12 | (rd)<<7 | 0x13)

/* xuantie c910 adds sfence.vmas */
/* as and vaddr are register numbers */
//#define BARR_SFENCE_VMAS(as, vaddr) WORD $(2<<25|(as)<<20|(vaddr)<<15|0<<7|013)

/* registers */
MCHTMP=15
TMP=26
TMP2=27
UART0=29

	GLOBL	dummysc(SB), $4

TEXT pushr2_31(SB), 1, $-4
	S(2); S(3); S(4); S(5); S(6); S(7)
	S(8); S(9); S(10); S(11); S(12); S(13); S(14); S(15); S(16); S(17)
	S(18); S(19); S(20); S(21); S(22); S(23); S(24); S(25); S(26)
	S(27); S(28); S(29); S(30); S(31)
	RET
TEXT popr8_31(SB), 1, $-4
	P(8); P(9); P(10); P(11); P(12); P(13); P(14); P(15); P(16); P(17)
	P(18); P(19); P(20); P(21); P(22); P(23); P(24); P(25); P(26)
	P(27); P(28); P(29); P(30); P(31)
	RET

/*
 * Read & write various system registers.
 */

/* likewise for S-mode exceptions */
TEXT badsinst(SB), 1, $-4
	SUB	$(2*XLEN), R2			/* allocate local(s) */
	MOV	R9, XLEN(R2)			/* save r9 */

	MOV	CSR(SEPC), R9
	ADD	$4, R9				/* advance past bad instr */
	MOV	R9, CSR(SEPC)

	MOV	$1, R9
	MOVB	R9, MPROBEBAD(R(MACH))
	FENCE

	MOV	XLEN(R2), R9			/* restore r9 */
	ADD	$(2*XLEN), R2
	SRET

/*
 * the cycle counters are per core (not per hart) and may stop during WFI;
 * avoid them when measuring elapsed time.
 */
TEXT rdtsc(SB), 1, $-4				/* Time Stamp Counter */
	FENCE
	MOV	CSR(CYCLO), R(ARG)
	RET


/* the clint timer runs at a constant (slower) rate and always */
TEXT grdtime(SB), 1, $-4				/* Clint's time counter */
	FENCE
	MOV	CSR(TIMELO), R(ARG)
	RET

/* machine-mode registers */
TEXT getmie(SB), 1, $-4
	MOV	CSR(MIE), R(ARG)
	RET
TEXT putmie(SB), 1, $-4
	MOV	R(ARG), CSR(MIE)
	FENCE
	RET
TEXT getmip(SB), 1, $-4
	MOV	CSR(MIP), R(ARG)
	RET
TEXT putmip(SB), 1, $-4
	MOV	R(ARG), CSR(MIP)
	FENCE
	RET
TEXT getmsts(SB), 1, $-4
	MOV	CSR(MSTATUS), R(ARG)
	RET
TEXT putmsts(SB), 1, $-4
	BARR_SFENCE_VMA(0, 0)
	MOV	R(ARG), CSR(MSTATUS)
	BARR_SFENCE_VMA(0, 0)
	FENCE
	FENCE_I
	RET
TEXT getmtvec(SB), 1, $-4
	MOV	CSR(MTVEC), R(ARG)
	RET
TEXT putmtvec(SB), 1, $-4
TEXT swapmtvec(SB), 1, $-4
	CSRRW	CSR(MTVEC), R(ARG), R(ARG)
	FENCE
	RET

/* supervisor-mode registers */
TEXT getsatp(SB), 1, $-4
	MOV	CSR(SATP), R(ARG)
	RET

TEXT getsb(SB), 1, $-4
	MOV	R3, R(ARG)
	RET

TEXT getsie(SB), 1, $-4
	MOV	CSR(SIE), R(ARG)
	RET
TEXT putsie(SB), 1, $-4
	MOV	R(ARG), CSR(SIE)
	FENCE
	RET
TEXT clrsie(SB), 1, $-4				/* returns old SIE */
	CSRRC	CSR(SIE), R(ARG), R(ARG)
	RET
TEXT setsie(SB), 1, $-4				/* returns old SIE */
	CSRRS	CSR(SIE), R(ARG), R(ARG)
	RET

TEXT getsip(SB), 1, $-4
	MOV	CSR(SIP), R(ARG)
	RET
TEXT putsip(SB), 1, $-4
	MOV	R(ARG), CSR(SIP)
	FENCE
	RET
TEXT getsp(SB), 1, $-4
	MOV	R2, R(ARG)
	RET
TEXT setsp(SB), 1,  $-4
	MOV	R(ARG), R2
TEXT setspalign(SB), 1, $-4
	RET
TEXT getsts(SB), 1, $-4
	MOV	CSR(SSTATUS), R(ARG)
	RET
TEXT putsts(SB), 1, $-4
	FENCE
	FENCE_I
	MOV	R(ARG), CSR(SSTATUS)
	/* fall through */
/* wait for loads & stores (including instruction writes) to finish */
TEXT cacheflush(SB), 1, $-4
	FENCE
	FENCE_I
	RET
TEXT putsscratch(SB), 1,  $-4
	MOV	R(ARG), CSR(SSCRATCH)
	RET
TEXT getstvec(SB), 1, $-4
	MOV	CSR(STVEC), R(ARG)
	RET
TEXT putstvec(SB), 1, $-4
TEXT swapstvec(SB), 1, $-4
	CSRRW	CSR(STVEC), R(ARG), R(ARG)
	FENCE
	RET

TEXT clockenable(SB), 1, $-4
	MOV	$Stie, R9			/* super timer intr enable */
	CSRRS	CSR(SIE), R9, R0
	FENCE
	RET

TEXT clrstie(SB), 1, $-4
	MOV	$Stie, R(ARG)			/* super timer intr enable */
	CSRRC	CSR(SIE), R(ARG), R0
TEXT clrsipbit(SB), 1, $-4			/* clrsipbit(ulong bit) */
	CSRRC	CSR(SIP), R(ARG), R0
	FENCE
	RET

TEXT mret(SB), 1, $-4
	MOV	LINK, CSR(MEPC)
	FENCE
	MRET		/* leap to (MEPC) in MSTATUS.Mpp's mode (super) */

/* read vlong with previous mode's privileges */
TEXT readmprv(SB), 1, $-4		/* readmprv(addr) */
	MOV	$Mprv, R(MCHTMP)
	FENCE				/* complete accesses before Mprv */
	CSRRS	CSR(MSTATUS), R(MCHTMP), R0
	FENCE				/* complete accesses before Mprv */
	FENCE_I				/* invalidate i cache */
	MOVWU	(R(ARG)), R(ARG)	/* read addr with S mode access */
	FENCE				/* complete accesses before Mprv end */
	CSRRC	CSR(MSTATUS), R(MCHTMP), R0
	FENCE
	RET

/* write long with previous mode's privileges */
TEXT writelmprv(SB), 1, $(2*XLEN)	/* writelmprv(ulong *addr, ulong val) */
	MOVWU	val+XLEN(FP), R10
	MOV	$Mprv, R(MCHTMP)
	FENCE				/* complete accesses before Mprv */
	CSRRS	CSR(MSTATUS), R(MCHTMP), R0
	FENCE				/* complete accesses before Mprv */
	FENCE_I				/* invalidate i cache */
	MOVW	R10, (R(ARG))		/* write addr with S mode access */
	FENCE				/* complete accesses before Mprv end */
	CSRRC	CSR(MSTATUS), R(MCHTMP), R0
	FENCE
	RET

/*
 * jump to upper address range and adjust addresses in registers to match.
 * upper->lower map must already be in effect.
 */
TEXT jumphigh(SB), 1, $-4
	FENCE
	FENCE_I
	MOV	$KZERO, R(MCHTMP)	/* lowest upper address */
	OR	R(MCHTMP), LINK		/* return to upper space */
	OR	R(MCHTMP), R2		/* sp */
	OR	R(MCHTMP), R3		/* sb */
TEXT highalign(SB), 1, $-4
	OR	R(MCHTMP), R(MACH)	/* m */
	BEQ	R(USER), jumpdone
	OR	R(MCHTMP), R(USER)	/* up */
	/* jump into upper space */
jumpdone:
	FENCE
	FENCE_I
	RET

/*
 * jump to lower (id) address range and adjust addresses in registers.
 * identity map must already be in effect.
 */
TEXT jumplow(SB), 1, $-4
	FENCE
	MOV	$~KZERO, R(MCHTMP)
	AND	R(MCHTMP), LINK		/* return to lower space */
	AND	R(MCHTMP), R2		/* sp */
	AND	R(MCHTMP), R3		/* sb */
TEXT lowalign(SB), 1, $-4
	AND	R(MCHTMP), R(MACH)	/* m */
	AND	R(MCHTMP), R(USER)	/* up */
	JMP	jumpdone		/* jump into lower space */

/*
 * cache or tlb invalidation
 */
/* approximate intel's wbinvd instruction: i & d fences, flush mmu state */
TEXT wbinvd(SB), 1, $-4
	/* wait for all data loads & stores to finish, before starting new ones */
	FENCE
	FENCE_I			/* make writes of instructions visible */
	BARR_SFENCE_VMA(0, 0)	/* invalidate mmu state, reload from memory */
	RET

TEXT invlpg(SB), 1, $-4
	FENCE
	FENCE_I
#ifdef ERRATUM_CIP_1200
	/* sifive u74 erratum cip-1200: can't do a non-global SFENCE. */
	/* u54 too? */
	BARR_SFENCE_VMA(0, 0)
#else
	BARR_SFENCE_VMA(0, ARG)	/* only fences pt leaf accesses for va in ARG */
#endif
	RET

/*
 * Serialisation.
 */
TEXT pause(SB), 0, $-4
	PAUSE			/* can be a no-op */
	RET
TEXT gcoherence(SB), 0, $-4
	FENCE			/* the lot; caller might need anything fenced */
	RET

/*
 * discharge any lingering LR/SC reservation we hold. should only be needed
 * on change of mmu mappings.
 */
TEXT clrreserv(SB), 1, $-4
	MOV	$dummysc(SB), R12
	SCW(0, 12, 0)
	RET

/*
 * Turn the Sie bit of SSTATUS on or off; leave the individual enables alone.
 * NB: the spl* functions contain barriers and it is safe to rely upon that.
 */

TEXT gsplhi(SB), 1, $-4
_splhi:
	CSRRC	CSR(SSTATUS), $Sie, R(ARG) /* disable super intrs */
	AND	$Sie, R(ARG)		/* return old state */
	BEQ	R(ARG), _spldone	/* intrs were disabled? */
	MOV	LINK, SPLPC(R(MACH)) 	/* save PC in m->splpc for kprof */
	FENCE
_spldone:
	RET

TEXT gspllo(SB), 1, $-4			/* marker for devkprof */
_spllo:
	MOV	CSR(SSTATUS), R(ARG)
	AND	$Sie, R(ARG)		/* return old state */
	BNE	R(ARG), _spldone	/* Sie=1, intrs are enabled? */
	/* they are disabled, so enable them */
	FENCE
	/*
	 * strictly speaking, SPLLO should be after zeroing SPLPC,
	 * but since even the profiling clock can't interrupt splhi,
	 * we get more meaningful profiling results with SPLLO before it.
	 */
	SPLLO				/* enable super intrs; expect intr. */
	MOV	R0, SPLPC(R(MACH))	/* enabling; clear m->splpc */
	RET

/* assumed between spllo and spldone by devkprof */
TEXT gsplx(SB), 1, $-4
	MOV	CSR(SSTATUS), R9
	AND	$Sie, R9
	AND	$Sie, R(ARG), R10
	BEQ	R9, R10, _spldone	/* already in desired state? */
	BEQ	R10, _splhi		/* want intrs disabled? */
	JMP	_spllo			/* want intrs enabled */

TEXT spldone(SB), 1, $-4		/* marker for devkprof */
	RET

TEXT gislo(SB), 1, $-4
	MOV	CSR(SSTATUS), R(ARG)
	AND	$Sie, R(ARG)
	RET

/*
 * atomic operations
 */

/*
 * AQ and RL AMO bits act as fences for the memory operand.
 * sifive e51 rock-3 erratum requires AQ and RL for correct operation.
 */

/* see libc/riscv64/atom.s for ainc & adec */
TEXT aadd(SB), 1, $-4			/* int aadd(int*, int addend); */
	MOVW	addend+XLEN(FP), R9
	AMOW(Amoadd, AQ|RL, 9, ARG, 10) /* *R(ARG) += R9; old *R(ARG) -> R10 */
	ADDW	R9, R10, R(ARG)		/* old value + addend */
	SEXT_W(R(ARG))
	RET

TEXT amoswapw(SB), 0, $(2*XLEN)	/* ulong amoswapw(ulong *a, ulong nv) */
	MOVWU	nv+XLEN(FP), R9	/* new value */
	AMOW(Amoswap, AQ|RL, 9, ARG, ARG) /* *R(ARG)=R9; old *R(ARG) ->R(ARG) */
	RET

TEXT amoorw(SB), 0, $(2*XLEN)	/* ulong amoorw(ulong *a, ulong bits) */
	MOVWU	bits+XLEN(FP), R9
	AMOW(Amoor, AQ|RL, 9, ARG, ARG) /* *R(ARG)|=R9; old *R(ARG) -> R(ARG) */
	RET

TEXT amoandw(SB), 0, $(2*XLEN)	/* ulong amoandw(ulong *a, ulong bits) */
	MOV	R0, R10
	JMP	xor
TEXT amoandnw(SB), 0, $(2*XLEN)	/* ulong amoandnw(ulong *a, ulong bits) */
	MOV	$-1, R10
xor:
	MOVWU	bits+XLEN(FP), R9
	XOR	R10, R9
	AMOW(Amoand, AQ|RL, 9, ARG, ARG) /* *R(ARG)&=R9; old *R(ARG) ->R(ARG) */
	RET

/*
 * oddball instruction.
 */
TEXT ecall(SB), 0, $-4
	ECALL
	RET

/*
 * idle until an interrupt.  call it splhi().
 *
 * WFI will be awakened by an intr, even if disabled.
 # WFI may pause the core cycle counter.
 */
TEXT halt(SB), 0, $-4
	WFI			/* WFI may be a no-op or hint */
	RET

/*
 * Label consists of a stack pointer and a programme counter.
 */
TEXT ggotolabel(SB), 1, $-4			/* gotolabel(Label *) */
	MOV	0(R(ARG)), R2			/* restore sp */
	MOV	XLEN(R(ARG)), LINK		/* restore return pc */
	MOV	$1, R(ARG)
	RET

TEXT gsetlabel(SB), 1, $-4			/* setlabel(Label *) */
	MOV	R2, 0(R(ARG))			/* store sp */
	MOV	LINK, XLEN(R(ARG))		/* store return pc */
	MOV	R0, R(ARG)
	RET

/*
 * mul64fract(uvlong *r, uvlong a, uvlong b)
 * Multiply two 64-bit numbers, return the middle 64 bits of the 128-bit result.
 *
 * use 64-bit multiply with 128-bit result
 */
TEXT gmul64fract(SB), 1, $-4
	/* r, the address of the result, is in RARG */
	MOV	a+XLEN(FP), R9
	MOV	b+(2*XLEN)(FP), R11

	MULHU	R9, R11, R13			/* high unsigned(al*bl) */
	MUL	R9, R11, R14			/* low unsigned(al*bl) */

	SRL	$32, R14			/* lose lowest long */
	SLL	$32, R13  /* defeat sign extension & position low of hi as hi */
	OR	R13, R14

	MOV	R14, (R(ARG))
	RET

/* Zbb extension, as found in sifive u74 */
TEXT clzzbb(SB), 1, $-4				/* int clzzbb(Clzuint) */
	CLZ(ARG, ARG)
	RET

#define CBOINVAL 0
#define CBOCLEAN 1		/* write back iff dirty */
#define CBOFLUSH 2		/* clean then invalidate */

#define CBOZERO 4		/* Zicboz */

/* Zicbom extension; takes virtual addresses */
TEXT cboflush(SB), 1, $-4			/* void cboflush(uintptr) */
	WORD $(0xf | 2<<12 | ARG<<15 | CBOFLUSH<<20)
	RET

/* Zicbom extension; takes virtual addresses */
TEXT cboinval(SB), 1, $-4			/* void cboinval(uintptr) */
	WORD $(0xf | 2<<12 | ARG<<15 | CBOINVAL<<20)
	RET

/* Xuantie C910 instruction */
TEXT sync_is(SB), 1, $-4
	WORD $0x01b0000b /* SYNC_IS: synchronise data and instruction memory */
	RET
