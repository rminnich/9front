#define ISH	(2<<2|3)

/* get variants */
TEXT agetl+0(SB),1,$0
TEXT agetp+0(SB),1,$0
	MOVW	$0, R5
	MOVW	(R0), R0
	DMB	$ISH
	RET

/* set variants */
TEXT aswapl+0(SB),1,$0
TEXT aswapp+0(SB),1,$0
	MOVW	new+4(FP), R1
	MOVW	(R0), R2
	DMB	$ISH
	MOVW	R1, (R0)
	MOVW	R2, R0
	DMB	$ISH
	RET

/* inc variants */
TEXT aincl+0(SB),1,$0
	MOVW	delta+4(FP), R1
_aincl:
	LDREX	(R0), R3
	ADD	R1, R3
	STREX	R3, (R0), R4
	CMP	$0, R4
	BNE	_aincl
	MOVW	R3, R0
	DMB	$ISH
	RET

/* cas variants */
TEXT acasl+0(SB),1,$0
TEXT acasp+0(SB),1,$0
	MOVW	old+4(FP), R1
	MOVW	new+8(FP), R2
	DMB	$ISH
_acasl:
	LDREX	(R0), R3
	CMP	R1, R3
	BNE	_acaslf
	STREX	R2, (R0), R4
	CMP	$0, R4
	BNE	_acasl
	MOVW	$1, R0
	DMB	$ISH
	RET
_acaslf:
	CLREX
	MOVW	$0, R0
	DMB	$ISH
	RET

/* barriers */
TEXT coherence+0(SB),1,$0
	DMB	$ISH
	RET
