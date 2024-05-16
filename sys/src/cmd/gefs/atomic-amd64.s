/*  get variants */
TEXT agetl+0(SB),1,$0
	MOVL	(RARG), AX
	RET
TEXT agetv+0(SB),1,$0
TEXT agetp+0(SB),1,$0
	MOVQ	(RARG), AX
	RET

/*  set variants */
TEXT asetl+0(SB),1,$0
	MOVL		v+8(FP), AX
	LOCK; XCHGL	(RARG), AX
	RET

TEXT asetv+0(SB),1,$0
TEXT asetp+0(SB),1,$0
	MOVQ		v+8(FP), AX
	LOCK; XCHGQ	(RARG), AX
	RET

/*  inc variants */
TEXT aincl+0(SB),1,$0
	MOVQ		v+8(FP), BX
	MOVQ		BX, AX
	LOCK; XADDL	AX, (RARG)
	ADDQ		BX, AX
	RET

TEXT aincv+0(SB),1,$0
TEXT aincp+0(SB),1,$0
	MOVQ		v+8(FP), BX
	MOVQ		BX, AX
	LOCK; XADDQ	AX, (RARG)
	ADDQ		BX, AX
	RET

/*  cas variants */
TEXT acasl+0(SB),1,$0
	MOVL	c+8(FP), AX
	MOVL	v+16(FP), BX
	LOCK; CMPXCHGL	BX, (RARG)
	SETEQ	AX
	MOVBLZX	AX, AX
	RET

TEXT acasv+0(SB),1,$0
TEXT acasp+0(SB),1,$0
	MOVQ	c+8(FP), AX
	MOVQ	v+16(FP), BX
	LOCK; CMPXCHGQ BX, (RARG)
	SETEQ	AX
	MOVBLZX	AX, AX
	RET

/* barriers (do we want to distinguish types?) */
TEXT coherence+0(SB),1,$0
	MFENCE
	RET
