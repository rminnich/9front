/* get variants */
TEXT ageti+0(SB),1,$0
TEXT agetl+0(SB),1,$0
TEXT agetp+0(SB),1,$0
	MOVL	p+0(FP), AX
	MOVL	0(AX), AX
	RET

/* set variants */
TEXT aseti+0(SB),1,$0
TEXT aswapl+0(SB),1,$0
TEXT aswapp+0(SB),1,$0
	MOVL		p+0(FP), BX
	MOVL		v+4(FP), AX
	LOCK; XCHGL	(BX), AX
	RET

/* inc variants */
TEXT aincl+0(SB),1,$0
	MOVL	p+0(FP), BX
	MOVL	v+4(FP), CX
	MOVL	CX, AX
	LOCK; XADDL AX, (BX)
	ADDL	CX, AX
	RET

/* cas variants */
TEXT acasl+0(SB),1,$0
TEXT acasp+0(SB),1,$0
	MOVL	p+0(FP), CX
	MOVL	ov+4(FP), AX
	MOVL	nv+8(FP), DX
	LOCK; CMPXCHGL DX, (CX)
	JNE	fail32
	MOVL	$1,AX
	RET
fail32:
	MOVL	$0,AX
	RET

/* barriers */
TEXT coherence+0(SB),1,$0
	/* this is essentially mfence but that requires sse2 */
	XORL	AX, AX
	LOCK; XADDL AX, (SP)
	RET
