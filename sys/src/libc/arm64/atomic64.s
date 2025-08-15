#define ISH	(2<<2|3)

/* get variants */
TEXT agetv+0(SB),1,$0
	LDAR	(R0), R0
	DMB	$ISH
	RETURN

/* set variants */
TEXT aswapv+0(SB),1,$0
	MOV	0x08(FP), R1
	MOV	R0, R2
_setv:
	LDXR	(R2), R0
	STXR	R1, (R2), R3
	CBNZW	R3, _setv
	DMB	$ISH
	RETURN

/* inc variants */
TEXT aincv+0(SB),1,$0
	MOV	0x08(FP), R1
	MOV	R0, R2
_incv:
	LDXR	(R2), R0
	ADD	R1, R0, R3
	STXR	R3, (R2), R4
	CBNZW	R4, _incv
	DMB	$ISH
	MOV	R3, R0
	RETURN

/* cas variants */
TEXT acasv+0(SB),1,$0
	MOV	0x08(FP), R1
	MOV	0x10(FP), R2
	DMB	$ISH
_casv:
	LDXR	(R0), R3
	CMP	R1, R3
	BNE	_casvf
	STXR	R2, (R0), R4
	CBNZW	R4, _casv
	MOV	$1, R0
	DMB	$ISH
	RETURN
_casvf:
	CLREX
	MOV	$0, R0
	DMB	$ISH
	RETURN
