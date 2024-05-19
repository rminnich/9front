TEXT	setjmp(SB), 1, $-4
	MOV	R2, (RARG)	/* store sp in jmp_buf */
	MOV	LR, XLEN(RARG) /* store return pc */
	MOV	R0, RARG	/* return 0 */
	RET

TEXT	longjmp(SB), 1, $-4
	MOVW	r+XLEN(FP), R13
	BNE	R13, ok		/* ansi: "longjmp(0) => longjmp(1)" */
	MOV	$1, R13		/* bless their pointed heads */
ok:	MOV	(RARG), R2	/* restore sp */
	MOV	XLEN(RARG), LR /* restore return pc */
	MOV	R13, RARG
	RET			/* jump to saved pc */
