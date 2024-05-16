#define CMPXCHG	/* (CX) */\
	BYTE $0x0F; BYTE $0xB1; BYTE $0x11
#define CMPXCHG64 /* (DI) */\
	BYTE $0x0F; BYTE $0xC7; BYTE $0x0F
#define XADDL /* BX, (AX) */ \
	BYTE $0x0F; BYTE $0xC1; BYTE $0x03
#define XADDLSP /* AX, (SP) */ \
	BYTE $0x0F; BYTE $0xC1; BYTE $0x04; BYTE $0x24

/*  get variants */
TEXT ageti+0(SB),1,$0
TEXT agetl+0(SB),1,$0
TEXT agetp+0(SB),1,$0
	MOVL	p+0(FP), AX
	MOVL	0(AX), AX
	RET

TEXT agetv+0(SB),1,$0
	MOVL	r+0(FP), AX
	MOVL	p+4(FP), BX
	FMOVD	(BX), F0
	FMOVDP	F0, (AX)
	RET

/*  set variants */
TEXT aseti+0(SB),1,$0
TEXT asetl+0(SB),1,$0
TEXT asetp+0(SB),1,$0
	MOVL		p+0(FP), BX
	MOVL		v+4(FP), AX
	LOCK; XCHGL	(BX), AX
	RET

TEXT asetv+0(SB),1,$0
	MOVL	p+4(FP), DI
	MOVL	nv+8(FP), BX
	MOVL	nv+12(FP), CX
	MOVL	0(DI), AX
	MOVL	4(DI), DX
loop:
	LOCK;	CMPXCHG64
        JNE     loop
	MOVL	p+0(FP),DI
	MOVL	AX, 0(DI)
	MOVL	DX, 4(DI)
	RET

/*  inc variants */
TEXT ainci+0(SB),1,$0
TEXT aincl+0(SB),1,$0
TEXT aincp+0(SB),1,$0
	MOVL	p+0(FP), BX
	MOVL	v+4(FP), CX
	MOVL	CX, AX
	LOCK; XADDL
	ADDL	CX, AX
	RET

TEXT aincv+0(SB),1,$0
	MOVL	p+4(FP), DI
retry:
	MOVL	0(DI), AX
	MOVL	4(DI), DX
	MOVL 	AX, BX
	MOVL	DX, CX
	ADDL	v+8(FP), BX
	ADCL	v+12(FP), CX
	LOCK; CMPXCHG64
	JNE	retry
	MOVL	r+0(FP), DI
	MOVL	BX, 0x0(DI)
	MOVL	CX, 0x4(DI)
	RET

/*  cas variants */
TEXT acasi+0(SB),1,$0
TEXT acasl+0(SB),1,$0
TEXT acasp+0(SB),1,$0
	MOVL	p+0(FP), CX
	MOVL	ov+4(FP), AX
	MOVL	nv+8(FP), DX
	LOCK; CMPXCHG
	JNE	fail32
	MOVL	$1,AX
	RET
fail32:
	MOVL	$0,AX
	RET

TEXT acasv+0(SB),1,$0
	MOVL	p+0(FP), DI
	MOVL	ov+4(FP), AX
	MOVL	ov+8(FP), DX
	MOVL	nv+12(FP), BX
	MOVL	nv+16(FP), CX
	LOCK; CMPXCHG64
	JNE	fail64
	MOVL	$1,AX
	RET
fail64:
	MOVL	$0,AX
	RET

/* barriers (do we want to distinguish types?) */
TEXT coherence+0(SB),1,$0
	/* this is essentially mfence but that requires sse2 */
	XORL	AX, AX
	LOCK; XADDLSP
	RET
