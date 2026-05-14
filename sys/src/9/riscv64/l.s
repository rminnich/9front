#include "riscv64l.h"

#include "mem.h"

#undef SFENCE
#undef WFI

#define SFENCE	WORD	$0x12000073
#define WFI	WORD	$0x10500073

#define SATP_SV39	(8ULL<<60)
#define MAKE_SATP(x)	(SATP_SV39|((x)>>12))

/*
	MOV	$_start(SB), R12
	MOV $0, R16
	MOV $1, R17
	MOV $65, R10
	ECALL
	JMP (R12)

*/
// THIS MUST BE FIRST FUNCTION IN THIS FILE
TEXT	_start(SB), 1, $-4
	JAL R1, SRC
SRC:
	ADD $-4, R1, R1
	MOV R1, R9
	MOV $KTZERO, R3
	SUB R3, R1, R3
	BEQ R3, RELOCATED
	MOV $KTZERO, R2
	MOV R2, R10
	MOV $0x1000000, R4
	SUB R2, R1, R5
	BEQ R5, DONECOPY
LOOP:
	MOV 0(R1), R3
	MOV R3, 0(R2)
	ADD $8, R2, R2
	ADD $8, R1, R1
	ADD $-8, R4, R4
	BNE R4, LOOP
ENDLOOP:
	JAL R1, DONECOPY
DONECOPY:
	ADD $10, R1
	SUB R9, R1, R1
	ADD R10, R1, R1
	JAL R1, 0(R1)
RELOCATED:
	MOV	$setSB(SB), R3
	JAL	R1, mmudisable<>(SB)
	MOV $MACHADDR(1), R2
	MOV	$setSB(SB), R3
	// clear out UP and set MACH. 
	MOV $0, R6
	MOV $MACHADDR(0), R7
	// allow kernel to read/write user memory.
	MOV CSR(SSTATUS), R8
	OR $0x40000, R8, R8
	MOV R8, CSR(SSTATUS)
	
	JAL	R1, main(SB)
NDNR:
	MOV $0x30, R10
		MOV $1, R17
		MOV $0, R16
		ECALL
	JMP NDNR

TEXT	stop<>(SB), 1, $-4
_stop:
	WFI
	JMP	_stop


TEXT 	soft(SB), 1, $-4
	RET

//#define SATP 0x180
TEXT	rsatp(SB), 1, $-4
	MOV	CSR(SATP), R8
	RET

TEXT	wsatp(SB), 1, $-4
	LOADSATP(RARG)
	RET

TEXT	rseed(SB), 1, $-4
	MOV	CSR(SEED), R8
	RET

TEXT	wstvec(SB), 1, $-4
	SFENCE
	MOV	RARG, CSR(STVEC)
	SFENCE
	RET

TEXT	mmudisable<>(SB), 1, $-4
	SFENCE
	MOV	R0, CSR(SATP)
	SFENCE
	RET

TEXT	mmuenable<>(SB), 1, $-4
//	MOV	$MAKE_SATP(L1-KZERO), R8
	SFENCE
	MOV	R8, CSR(SATP)
	SFENCE
	RET

TEXT flushalltlb(SB), 1, $-4
	WORD $0x12000073
	RET

TEXT flushvatlb(SB), 1, $-4
	WORD $0x12040073
	RET

TEXT	cas(SB), 1, $-4
TEXT	cmpswap(SB), 1, $-4
	MOVWU	ov+8(FP), R12
	MOVWU	nv+(8+4)(FP), R13
	MOV	R0, R11
	FENCE_RW
_spincas:
	LRW(ARG, 14)
	SLL	$32, R14
	SRL	$32, R14
	BNE	R12, R14, _fail
	FENCE_RW
	SCW(13, ARG, 14)
	BNE	R14, _spincas
	MOV	$1, R11
_fail:
	FENCE_RW
	MOV	R11, R(ARG)
	RET

// This version of tas stores RA (caller PC) in the location.
// More useful than just '1' and the code tests for it being
// non-zero, not just '1'.
TEXT	tas(SB), 1, $-4
TEXT	_tas(SB), 1, $-4
	FENCE_RW
	AMOW(Amoswap, AQ|RL, 1, ARG, ARG)
	FENCE_RW
	RET

TEXT	setlabel(SB), 1, $-4
	MOV	R2, 0(R8)
	MOV	R1, 8(R8)
	MOV	R0, R8
	RET

// There is a spurious u64int pushed here, not sure why?
// only on forkret. goddam it.
TEXT	gotolabel(SB), 1, $-4			/* gotolabel(Label *) */
	MOV	0(R(ARG)), R2			/* restore sp */
	MOV	XLEN(R(ARG)), R1		/* restore return pc */
	MOV	$1, R(ARG)
	RET

// NOTE: this is the SHADOW register of the memory-mapped
// MTIME register. We will now use STIMECMP for interrupts.
#define TIME	0xc01
TEXT	rdtime(SB), 1, $-4
	FENCE
	MOV	CSR(TIME), R8
	RET

#define STIMECMP 0x14D
TEXT wrstimecmp(SB), 1, $-4
	FENCE
	MOV R(ARG), CSR(STIMECMP)
	RET

TEXT rdstimecmp(SB), 1, $-4
	FENCE
	MOV CSR(STIMECMP), R(ARG)
	RET
	
TEXT	coherence(SB), 1, $-4
	FENCE
	RET

TEXT	wfi(SB), 1, $-4
	RET

TEXT	flushtlb(SB), 1, $-4
	RET
TEXT	flushasidvall(SB), 1, $-4
	RET
TEXT	flushasidva(SB), 1, $-4
	RET
TEXT	flushasid(SB), 1, $-4
	RET

TEXT	vcycles(SB), 1, $-4
	RET
TEXT	perfticks(SB), 1, $-4
	RET

TEXT	idlehands(SB), 1, $-4
	FENCE
	MOVW	nrdy(SB), R8
	BNE	R8, R0, _ready
	WFI
_ready:
	RET

/*
 * switch to user mode with stack pointer from R(ARG), at start of text.
 * used to start process 1 (init).
 */
TEXT touser(SB), 1, $-4
	//JAL R1, _fpuinit(SB)
	FENCE
	FENCE_I
	CSRRW	CSR(SSCRATCH), R(MACH), R(MACH) /* restore R7, reload saved m */
	/*
	 * Drop sstatus.SPP to 0 so SRET returns to user mode (not supervisor),
	 * and set SPIE so SIE = 1 after SRET (user can be preempted by clock).
	 * Read-modify-write the whole register — same idiom as _start uses
	 * for setting SUM.
	 */
	MOV	CSR(SSTATUS), R9
	MOV	$~0x100, R10		/* ~Spp */
	AND	R10, R9, R9		/* clear SPP */
	OR	$0x20, R9, R9		/* set SPIE */
	MOV	R9, CSR(SSTATUS)
	MOV	$(UTZERO+0x28), R12	/* skip unextended exec hdr of init */
	MOV	R12, CSR(SEPC)		/* new pc */
	MOV	RARG, R2		/* new sp */
	WORD $0x10200073 //SRET

// Courtesy of Geoff (I think?)
/*
 * Turn the Sie bit of SSTATUS on or off; leave the individual enables alone.
 * NB: the spl* functions contain barriers and it is safe to rely upon that.
 * Do we need these? Not sure yet. Leave it here for now.
 */

TEXT splhi(SB), 1, $-4
TEXT gsplhi(SB), 1, $-4
_splhi:
	CSRRC	CSR(SSTATUS), $Sie, R(ARG) /* disable super intrs */
	AND	$Sie, R(ARG)		/* return old state */
	BEQ	R(ARG), _spldone	/* intrs were disabled? */
	MOV	LINK, SPLPC(R(MACH)) 	/* save PC in m->splpc for kprof */
	FENCE
_spldone:
	RET

TEXT spllo(SB), 1, $-4
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
TEXT splx(SB), 1, $-4
	MOV	CSR(SSTATUS), R9
	AND	$Sie, R9
	AND	$Sie, R(ARG), R10
	BEQ	R9, R10, _spldone	/* already in desired state? */
	BEQ	R10, _splhi		/* want intrs disabled? */
	JMP	_spllo			/* want intrs enabled */

TEXT spldone(SB), 1, $-4		/* marker for devkprof */
	RET

TEXT gislo(SB), 1, $-4
TEXT islo(SB), 1, $-4
	MOV	CSR(SSTATUS), R(ARG)
	AND	$Sie, R(ARG)
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

// laterz
TEXT	cacheiinvse(SB), 1, $-4
	RET

TEXT clockenable(SB), 1, $-4
	MOV	$Stie, R9			/* super timer intr enable */
	CSRRS	CSR(SIE), R9, R0
	FENCE
	RET

TEXT clrstie(SB), 1, $-4
	MOV	$Stie, R(ARG)			/* super timer intr enable */
	CSRRC	CSR(SIE), R(ARG), R0
TEXT clrsipbit(SB), 1, $-4			/* uvlong clrsipbit(ulong bit) */
	CSRRC	CSR(SIP), R(ARG), R0
	FENCE
	MOV	CSR(SIP), R(ARG)
	RET

// The 9k port is amazing, it supports M mode. 

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

/*
 * the cycle counters are per core (not per hart) and may stop during WFI;
 * avoid them when measuring elapsed time.
 */
TEXT rdtsc(SB), 1, $-4				/* Time Stamp Counter */
	FENCE
	MOV	CSR(CYCLO), R(ARG)
	RET

/* from Geoff */
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
