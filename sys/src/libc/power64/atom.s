TEXT ainc(SB), 1, $0				/* long ainc(long *); */
	MOVD	$1, R5
	BR	alp
TEXT adec(SB), 1, $0				/* long adec(long*); */
	MOVD	$-1, R5
alp:
	MOVD	R3, R4
	LWSYNC
	LWAR	(R4), R3
	ADD	R5, R3
	STWCCC	R3, (R4)
	BNE	-3(PC)
	RETURN

/*
 * int cas(uint* p, int ov, int nv);
 */
TEXT cas(SB), 1, $0
	MOVWZ	old+8(FP), R4
	MOVWZ	new+16(FP), R5
	LWSYNC
casagain:
	LWAR	(RARG), R6
	CMPW	R6, R4
	BNE	casfail
	STWCCC	R5, (RARG)
	BNE	casagain
	MOVD	$1, RARG
	LWSYNC
	RETURN
casfail:
	LWSYNC
	AND	R0, RARG
	RETURN

/*
 * int casv(u64int* p, u64int ov, u64int nv);
 */
TEXT casv(SB), 1, $0
	MOVD	old+8(FP), R4
	MOVD	new+16(FP), R5
	LWSYNC
cas64again:
	LDAR	(RARG), R6
	CMP	R6, R4
	BNE	cas64fail
	STDCCC	R5, (RARG)
	BNE	cas64again
	MOVD	$1, RARG
	LWSYNC
	RETURN
cas64fail:
	LWSYNC
	AND	R0, RARG
	RETURN
