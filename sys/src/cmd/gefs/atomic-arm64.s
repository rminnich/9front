/*  get variants */
TEXT agetl+0(SB),1,$0
	MOVW	(R0), R0
	RETURN
TEXT agetv+0(SB),1,$0
TEXT agetp+0(SB),1,$0
	MOV	(R0), R0
	RETURN

/*  set variants */
TEXT asetl+0(SB),1,$0
	MOV	0x08(FP), R1
	MOV	R0, R2
_setl:
	LDAXRW	(R2), R0
	STLXRW	R1, (R2), R3
	CBNZW	R3, _setl
	RETURN
TEXT asetv+0(SB),1,$0
TEXT asetp+0(SB),1,$0
	MOV	0x08(FP), R1
	MOV	R0, R2
_setp:
	LDAXR	(R2), R0
	STLXR	R1, (R2), R3
	CBNZW	R3, _setp
	RETURN

/*  inc variants */
TEXT aincl+0(SB),1,$0
	MOV	0x08(FP), R1
	MOV	R0, R2
_incl:
	LDAXRW	(R2), R0
	ADDW	R1, R0, R3
	STLXRW	R3, (R2), R4
	CBNZW	R4, _incl
	RETURN
TEXT aincv+0(SB),1,$0
TEXT aincp+0(SB),1,$0
	MOV	0x08(FP), R1
	MOV	R0, R2
_incp:
	LDAXR	(R2), R0
	ADD	R1, R0, R3
	STLXR	R3, (R2), R4
	CBNZW	R4, _incp
	RETURN

/*  cas variants */
TEXT acasl+0(SB),1,$0
	MOV	0x08(FP), R1
	MOV	0x10(FP), R2
	LDAXRW	(R0), R3
	CMPW	R1, R3
	BNE	_casl
	STLXRW	R2, (R0), R4
	CMPW	$0, R4
_casl:
	CSETW	EQ, R0
	RETURN
TEXT acasv+0(SB),1,$0
TEXT acasp+0(SB),1,$0
	MOV	0x08(FP), R1
	MOV	0x10(FP), R2
	LDAXR	(R0), R3
	CMP	R1, R3
	BNE	_casp
	STLXR	R2, (R0), R4
	CMPW	$0, R4
_casp:
	CSETW	EQ, R0
	RETURN

/* barriers */
#define ISH	(2<<2 | 3)
TEXT coherence+0(SB),1,$0
	DMB	$ISH
	RETURN
