/* get variants */
TEXT agetl+0(SB),1,$0
	SYNC
	LWAR	(RARG), RARG
	CMPW	RARG, RARG
	BNE	-1(PC)
	ISYNC
	RETURN

TEXT agetp+0(SB),1,$0
	SYNC
	LDAR	(RARG), RARG
	CMP	RARG, RARG
	BNE	-1(PC)
	ISYNC
	RETURN

/* set variants */
TEXT aswapl+0(SB),1,$0
	MOVD	RARG, R4
	MOVW	val+8(FP), R5
	SYNC
_aswapl:
	LWAR	(R4), RARG
	STWCCC	R5, (R4)
	BNE	_aswapl
	RETURN

TEXT aswapp+0(SB),1,$0
	MOVD	RARG, R4
	MOVD	val+8(FP), R5
	SYNC
_aswapp:
	LDAR	(R4), RARG
	STDCCC	R5, (R4)
	BNE	_aswapp
	RETURN

/* inc variants */
TEXT aincl+0(SB),1,$0
	MOVD	RARG, R4
	MOVW	delta+8(FP), R5
	LWSYNC
_aincl:
	LWAR	(R4), RARG
	ADD	R5, RARG
	STWCCC	RARG, (R4)
	BNE	_aincl
	RETURN

/* cas variants */
TEXT acasl+0(SB),1,$0
	MOVWZ	old+8(FP), R4
	MOVWZ	new+16(FP), R5
	LWSYNC
_casl:
	LWAR	(RARG), R6
	CMPW	R6, R4
	BNE	_caslf
	STWCCC	R5, (RARG)
	BNE	_casl
	MOVD	$1, RARG
	LWSYNC
	RETURN
_caslf:
	LWSYNC
	AND	R0, RARG
	RETURN

TEXT acasp+0(SB),1,$0
	MOVD	old+8(FP), R4
	MOVD	new+16(FP), R5
	LWSYNC
_casp:
	LDAR	(RARG), R6
	CMP	R6, R4
	BNE	_caspf
	STDCCC	R5, (RARG)
	BNE	_casp
	MOVD	$1, RARG
	LWSYNC
	RETURN
_caspf:
	LWSYNC
	AND	R0, RARG
	RETURN

/* barriers */
TEXT coherence+0(SB),1,$0
	SYNC
	RETURN
