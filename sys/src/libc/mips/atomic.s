/* get variants */
TEXT agetl+0(SB),1,$0
TEXT agetp+0(SB),1,$0
	SYNC
	LL	(R1), R1
	SYNC
	RET

/* set variants */
TEXT aswapl+0(SB),1,$0
TEXT aswapp+0(SB),1,$0
	MOVW	new+4(FP), R2
	SYNC
_aswapl:
	LL	(R1), R3
	MOVW	R2, R4
	SC	R4, (R1)
	BEQ	R4, _aswapl
	SYNC
	MOVW	R3, R1
	RET

/* inc variants */
TEXT aincl+0(SB),1,$0
	MOVW	delta+4(FP), R2
	SYNC
_aincl:
	LL	(R1), R3
	ADD	R3, R2, R4
	MOVW	R4, R5
	SC	R5, (R1)
	BEQ	R5, _aincl
	SYNC
	MOVW	R4, R1
	RET

/* cas variants */
TEXT acasl+0(SB),1,$0
TEXT acasp+0(SB),1,$0
	MOVW	old+4(FP), R2
	MOVW	new+8(FP), R3
	SYNC
_acasl:
	LL	(R1), R4
	BNE	R4, R2, _acaslf
	MOVW	R3, R5
	SC	R5, (R1)
	BEQ	R5, _acasl
	SYNC
	MOVW	$1, R1
	RET
_acaslf:
	SYNC
	MOVW	$0, R1
	RET
