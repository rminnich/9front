/* memset(void *p, int c, uintptr n) - clear vlongs */
	TEXT	memset(SB),$(3*XLEN)
	MOV	R8, p+0(FP)
	MOV	R8, R11			/* R11 is pointer */
	MOVWU	c+XLEN(FP), R12		/* R12 is char */
	MOV	n+(2*XLEN)(FP), R10	/* R10 is count.  NB: uintptr */
	ADD	R10,R11, R13		/* R13 is end pointer */

/*
 * if not at least XLEN chars,
 * dont even mess around.
 * XLEN-1 chars to guarantee any
 * rounding up to a doubleword
 * boundary and XLEN characters
 * to get at least maybe one
 * full doubleword store.
 */
	SLT	$XLEN,R10, R8
	BNE	R8, out

/*
 * turn R12 into a doubleword of characters
 */
	AND	$0xff, R12
	SLL	$8,R12, R8
	OR	R8, R12
	SLL	$16,R12, R8
	OR	R8, R12
	SLL	$32,R12, R8
	OR	R8, R12

/*
 * store one byte at a time until pointer
 * is aligned on a doubleword boundary
 */
l1:
	AND	$(XLEN-1),R11, R8
	BEQ	R8, l2
	MOVB	R12, 0(R11)
	ADD	$1, R11
	JMP	l1

/*
 * turn R10 into end pointer-(4*XLEN-1)
 * store 4*XLEN at a time while there's room
 */
l2:
	ADD	$-(4*XLEN-1),R13, R10
l3:
	BGEU	R10,R11, l4
	MOV	R12, 0(R11)
	MOV	R12, XLEN(R11)
	MOV	R12, (2*XLEN)(R11)
	MOV	R12, (3*XLEN)(R11)
	ADD	$(4*XLEN), R11
	JMP	l3

/*
 * turn R10 into end pointer-(XLEN-1)
 * store XLEN at a time while there's room
 */
l4:
	ADD	$-(XLEN-1),R13, R10
l5:
	BGEU	R10,R11, out
	MOV	R12, 0(R11)
	ADD	$XLEN, R11
	JMP	l5

/*
 * last loop, store byte at a time
 */
out:
	BGEU	R13,R11, ret
	MOVB	R12, 0(R11)
	ADD	$1, R11
	JMP	out

ret:
	MOV	p+0(FP), R8
	RET
