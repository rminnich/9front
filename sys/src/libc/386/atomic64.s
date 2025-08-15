/* get variants */
TEXT agetv+0(SB),1,$0
	MOVL	r+0(FP), AX
	MOVL	p+4(FP), BX
	FMOVD	(BX), F0
	FMOVDP	F0, (AX)
	RET

/* set variants */
TEXT aswapv+0(SB),1,$0
	MOVL	p+4(FP), DI
	MOVL	nv+8(FP), BX
	MOVL	nv+12(FP), CX
	MOVL	0(DI), AX
	MOVL	4(DI), DX
loop:
	LOCK;	CMPXCHG8B (DI)
	JNE	loop
	MOVL	p+0(FP),DI
	MOVL	AX, 0(DI)
	MOVL	DX, 4(DI)
	RET

/* inc variants */
TEXT aincv+0(SB),1,$0
	MOVL	p+4(FP), DI
retry:
	MOVL	0(DI), AX
	MOVL	4(DI), DX
	MOVL 	AX, BX
	MOVL	DX, CX
	ADDL	v+8(FP), BX
	ADCL	v+12(FP), CX
	LOCK; CMPXCHG8B (DI)
	JNE	retry
	MOVL	r+0(FP), DI
	MOVL	BX, 0x0(DI)
	MOVL	CX, 0x4(DI)
	RET

/* cas variants */
TEXT acasv+0(SB),1,$0
	MOVL	p+0(FP), DI
	MOVL	ov+4(FP), AX
	MOVL	ov+8(FP), DX
	MOVL	nv+12(FP), BX
	MOVL	nv+16(FP), CX
	LOCK; CMPXCHG8B (DI)
	JNE	fail64
	MOVL	$1,AX
	RET
fail64:
	MOVL	$0,AX
	RET
