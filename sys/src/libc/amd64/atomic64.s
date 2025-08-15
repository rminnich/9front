/* get variants */
TEXT agetv+0(SB),1,$0
	MOVQ	(RARG), AX
	RET

/* set variants */
TEXT aswapv+0(SB),1,$0
	MOVQ		v+8(FP), AX
	LOCK; XCHGQ	(RARG), AX
	RET

/* inc variants */
TEXT aincv+0(SB),1,$0
	MOVQ		v+8(FP), BX
	MOVQ		BX, AX
	LOCK; XADDQ	AX, (RARG)
	ADDQ		BX, AX
	RET

/* cas variants */
TEXT acasv+0(SB),1,$0
	MOVQ	c+8(FP), AX
	MOVQ	v+16(FP), BX
	LOCK; CMPXCHGQ BX, (RARG)
	SETEQ	AX
	MOVBLZX	AX, AX
	RET
