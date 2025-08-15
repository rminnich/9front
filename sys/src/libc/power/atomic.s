/* get variants */
TEXT agetl+0(SB),1,$0
TEXT agetp+0(SB),1,$0
	SYNC
	LWAR	(R3), R3
	CMP	R3, R3
	BNE	-1(PC)
	ISYNC
	RETURN

/* set variants */
TEXT aswapl+0(SB),1,$0
TEXT aswapp+0(SB),1,$0
	MOVW	R3, R4
	MOVW	val+4(FP), R5
	SYNC
_aswapl:
	LWAR	(R4), R3
	STWCCC	R5, (R4)
	BNE	_aswapl
	RETURN

/* inc variants */
TEXT aincl+0(SB),1,$0
	MOVW	R3, R4
	MOVW	delta+4(FP), R5
	SYNC
_aincl:
	LWAR	(R4), R3
	ADD	R5, R3
	STWCCC	R3, (R4)
	BNE	_aincl
	RETURN

/* cas variants */
TEXT acasl+0(SB),1,$0
TEXT acasp+0(SB),1,$0
	MOVW	old+4(FP), R4
	MOVW	new+8(FP), R5
	SYNC
_casl:
	LWAR	(R3), R6
	CMP	R6, R4
	BNE	_caslf
	STWCCC	R5, (R3)
	BNE	_casl
	MOVW	$1, R3
	SYNC
	RETURN
_caslf:
	SYNC
	AND	R0, R3
	RETURN

/* barriers */
TEXT coherence+0(SB),1,$0
	SYNC
	RETURN
