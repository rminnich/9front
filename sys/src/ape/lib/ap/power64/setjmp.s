TEXT	setjmp(SB), 1, $-8
	MOVD	LR, R4
	MOVD	R1, (R3)
	MOVD	R4, 4(R3)
	MOVW	$0, R3
	RETURN

TEXT	sigsetjmp(SB), 1, $-4
	MOVW	savemask+4(FP), R4
	MOVW	R4, 0(R3)
	MOVW	$_psigblocked(SB), R4
	MOVW	R4, 4(R3)
	MOVW	LR, R4
	MOVW	R1, 8(R3)
	MOVW	R4, 12(R3)
	MOVW	$0, R3
	RETURN

TEXT	longjmp(SB), 1, $-8
	MOVD	R3, R4
	MOVW	r+12(FP), R3
	CMP	R3, $0
	BNE	ok		/* ansi: "longjmp(0) => longjmp(1)" */
	MOVW	$1, R3		/* bless their pointed heads */
ok:	MOVD	(R4), R1
	MOVD	4(R4), R4
	MOVD	R4, LR
	BR	(LR)

/*
 * trampoline functions because the kernel smashes r3
 * in the uregs given to notejmp
 */
TEXT	__noterestore(SB), 1, $-8
	MOVD	R4, R3
	MOVD	R5, LR
	BR	(LR)
