#include <atom.h>
#define ARG 8

#include "mem.h"
#include "sysreg.h"

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
	MOV	$setSB(SB), R3
	JAL	R1, mmudisable<>(SB)

	MOV	$(MACHADDR(0)-KZERO), R7
	AND	$(MAXMACH-1), R10
	MOVW	$MACHSIZE, R11
	MULW	R10, R11, R11
	SUB	R11, R7

	ADD	$(MACHSIZE-16), R7, R11
	MOV	R11, R2

	MOV	$(L1-KZERO), R11
	MOV	$(MACHADDR(-1)-KZERO), R12
_zerol1:
	MOV	R0, (R11)
	ADD	$8, R11
	BNE	R11, R12, _zerol1

	MOV	$edata(SB), R11
	MOV	$end(SB), R12
_zerobss:
	MOV	R0, (R11)
	ADD	$8, R11
	BNE	R11, R12, _zerobss

	MOV	$(L1-KZERO), R8
	JAL	R1, mmu0init(SB)

	JAL	R1, mmuenable<>(SB)

	MOV	$KTZERO, R11
	MOV	$zoinked<>(SB), R12
	AND	$(BY2PG-1), R12
	ADD	R11, R12, R12
	JMP	(R12)

TEXT	zoinked<>(SB), 1, $-4
	MOV	$_start(SB), R12
	MOV $0, R16
	MOV $1, R17
	MOV $65, R10
	ECALL
	JMP (R12)
	MOV	$setSB(SB), R3
	JAL	R1, main(SB)

TEXT	sbiputc(SB), 1, $-4
		MOV $1, R17
		MOV $0, R16
		ECALL
		RET

TEXT	stop<>(SB), 1, $-4
_stop:
	WFI
	JMP	_stop

TEXT	mmudisable<>(SB), 1, $-4
	SFENCE
	MOV	R0, CSR(SATP)
	SFENCE
	RET

TEXT	mmuenable<>(SB), 1, $-4
	MOV	$MAKE_SATP(L1-KZERO), R8
	SFENCE
	MOV	R8, CSR(SATP)
	SFENCE
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

TEXT	tas(SB), 1, $-4
TEXT	_tas(SB), 1, $-4
	MOV	$1, R10
	FENCE_RW
	AMOW(Amoswap, AQ|RL, 10, ARG, ARG)
	FENCE_RW
	RET

TEXT	setlabel(SB), 1, $-4
	MOV	R2, (R8)
	MOV	R1, 8(R8)
	MOV	R0, R8
	RET

TEXT	gotolabel(SB), 1, $-4
	MOVW	r+8(FP), R13
	BNE	R13, _ok
	MOV	$1, R13
_ok:	MOV	(R8), R2
	MOV	8(R8), R1
	MOV	R13, R8
	RET

TEXT	rdtime(SB), 1, $-4
	FENCE
	MOV	CSR(TIME), R8
	RET

TEXT	coherence(SB), 1, $-4
	FENCE
	RET

TEXT	islo(SB), 1, $-4
	RET

TEXT	splhi(SB), 1, $-4
	RET

TEXT	spllo(SB), 1, $-4
	RET

TEXT	splx(SB), 1, $-4
	RET

TEXT	flushtlb(SB), 1, $-4
	RET
TEXT	flushasidvall(SB), 1, $-4
	RET
TEXT	flushasidva(SB), 1, $-4
	RET
TEXT	flushasid(SB), 1, $-4
	RET

TEXT	cachedwbinvse(SB), 1, $-4
	RET
TEXT	cacheiinvse(SB), 1, $-4
	RET

TEXT	vcycles(SB), 1, $-4
	RET
TEXT	perfticks(SB), 1, $-4
	RET

	/* rename as we don't have ttbr */
TEXT	setttbr(SB), 1, $-4
	RET


TEXT	idlehands(SB), 1, $-4
	FENCE
	MOVW	nrdy(SB), R8
	BNE	R8, R0, _ready
	WFI
_ready:
	RET
