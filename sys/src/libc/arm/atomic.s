#define ISH	(2<<2|3)

/* get variants */
TEXT agetl+0(SB),1,$0
TEXT agetp+0(SB),1,$0
	MOVW	(R0), R0
	DMB	$ISH
	RET

/* set variants */
TEXT aswapl+0(SB),1,$0
TEXT aswapp+0(SB),1,$0
	MOVW	new+4(FP), R1
	MOVW	R0, R2
_aswapl:
	LDREX	(R2), R0
	STREX	R1, (R2), R3
	CMP	$0, R3
	BNE	_aswapl
	DMB	$ISH
	RET

/* inc variants */
TEXT aincl+0(SB),1,$0
	MOVW	delta+4(FP), R1
	MOVW	R0, R2
_aincl:
	LDREX	(R2), R0
	ADD	R1, R0
	STREX	R0, (R2), R3
	CMP	$0, R3
	BNE	_aincl
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
	STREX	R2, (R0), R3
	CMP	$0, R3
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
