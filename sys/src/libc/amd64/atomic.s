/* get variants */
TEXT agetl+0(SB),1,$0
	MOVL	(RARG), AX
	RET
TEXT agetp+0(SB),1,$0
	MOVQ	(RARG), AX
	RET

/* set variants */
TEXT aswapl+0(SB),1,$0
	MOVL		v+8(FP), AX
	LOCK; XCHGL	(RARG), AX
	RET

TEXT aswapp+0(SB),1,$0
	MOVQ		v+8(FP), AX
	LOCK; XCHGQ	(RARG), AX
	RET

/* inc variants */
TEXT aincl+0(SB),1,$0
	MOVQ		v+8(FP), BX
	MOVQ		BX, AX
	LOCK; XADDL	AX, (RARG)
	ADDQ		BX, AX
	RET

/* cas variants */
TEXT acasl+0(SB),1,$0
	MOVL	c+8(FP), AX
	MOVL	v+16(FP), BX
	LOCK; CMPXCHGL	BX, (RARG)
	SETEQ	AX
	MOVBLZX	AX, AX
	RET

TEXT acasp+0(SB),1,$0
	MOVQ	c+8(FP), AX
	MOVQ	v+16(FP), BX
	LOCK; CMPXCHGQ BX, (RARG)
	SETEQ	AX
	MOVBLZX	AX, AX
	RET

/* barriers */
TEXT coherence+0(SB),1,$0
	MFENCE
	RET
