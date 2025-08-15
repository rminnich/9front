/* get variants */
TEXT agetv+0(SB),1,$0
	SYNC
	LDAR	(RARG), RARG
	CMP	RARG, RARG
	BNE	-1(PC)
	ISYNC
	RETURN

/* set variants */
TEXT aswapv+0(SB),1,$0
	MOVD	RARG, R4
	MOVD	val+8(FP), R5
	SYNC
_aswapv:
	LDAR	(R4), RARG
	STDCCC	R5, (R4)
	BNE	_aswapv
	RETURN

/* inc variants */
TEXT aincv+0(SB),1,$0
	MOVD	RARG, R4
	MOVD	delta+8(FP), R5
	LWSYNC
_aincl:
	LDAR	(R4), RARG
	ADD	R5, RARG
	STDCCC	RARG, (R4)
	BNE	_aincl
	RETURN

/* cas variants */
TEXT acasv+0(SB),1,$0
	MOVD	old+8(FP), R4
	MOVD	new+16(FP), R5
	LWSYNC
_casv:
	LDAR	(RARG), R6
	CMP	R6, R4
	BNE	_casvf
	STDCCC	R5, (RARG)
	BNE	_casv
	MOVD	$1, RARG
	LWSYNC
	RETURN
_casvf:
	LWSYNC
	AND	R0, RARG
	RETURN
