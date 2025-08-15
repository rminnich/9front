#define ISH	(2<<2|3)

/* get variants */
TEXT agetl+0(SB),1,$0
	LDARW	(R0), R0
	DMB	$ISH
	RETURN

TEXT agetp+0(SB),1,$0
	LDAR	(R0), R0
	DMB	$ISH
	RETURN

/* set variants */
TEXT aswapl+0(SB),1,$0
	MOV	0x08(FP), R1
	MOV	R0, R2
_setl:
	LDXRW	(R2), R0
	STXRW	R1, (R2), R3
	CBNZW	R3, _setl
	DMB	$ISH
	RETURN

TEXT aswapp+0(SB),1,$0
	MOV	0x08(FP), R1
	MOV	R0, R2
_setp:
	LDXR	(R2), R0
	STXR	R1, (R2), R3
	CBNZW	R3, _setp
	DMB	$ISH
	RETURN

/* inc variants */
TEXT aincl+0(SB),1,$0
	MOV	0x08(FP), R1
	MOV	R0, R2
_incl:
	LDXRW	(R2), R0
	ADDW	R1, R0, R3
	STXRW	R3, (R2), R4
	CBNZW	R4, _incl
	DMB	$ISH
	MOVW	R3, R0
	RETURN

/* cas variants */
TEXT acasl+0(SB),1,$0
	MOV	0x08(FP), R1
	MOV	0x10(FP), R2
	DMB	$ISH
_casl:
	LDXRW	(R0), R3
	CMPW	R1, R3
	BNE	_caslf
	STXRW	R2, (R0), R4
	CBNZ	R4, _casl
	MOV	$1, R0
	DMB	$ISH
	RETURN
_caslf:
	CLREX
	MOV	$0, R0
	DMB	$ISH
	RETURN

TEXT acasp+0(SB),1,$0
	MOV	0x08(FP), R1
	MOV	0x10(FP), R2
	DMB	$ISH
_casp:
	LDXR	(R0), R3
	CMP	R1, R3
	BNE	_caspf
	STXR	R2, (R0), R4
	CBNZW	R4, _casp
	MOV	$1, R0
	DMB	$ISH
	RETURN
_caspf:
	CLREX
	MOV	$0, R0
	DMB	$ISH
	RETURN

/* barriers */
TEXT coherence+0(SB),1,$0
	DMB	$ISH
	RETURN
