#include "mem.h"
// NIX stuff, just keep it here.
MODE $64

/*
 */
TEXT acsyscallentry(SB), 1, $-4
	SWAPGS
	BYTE $0x65; MOVQ 0, RMACH		/* m-> (MOVQ GS:0x0, R15) */
	MOVQ	16(RMACH), RUSER		/* m->proc */
FIXME	MOVQ	24(RUSER), R12		/* m->proc->dbgregs */

	/* save sp to r13; set up kstack so we can call acsyscall */
	MOVQ	SP, R13
FIXME	MOVQ	24(RMACH), SP			/* m->stack */
CHECKME	ADDQ	$8192, SP // XXX

	MOVQ	$UDSEG, BX		/* old stack segment */
FIXME	MOVQ	BX, 176(R12)				/* save ss */
FIXME	MOVQ	R13, 168(R12)				/* old sp */
FIXME	MOVQ	R11, 160(R12)				/* old flags */
	MOVQ	$UESEG, BX		/* old code segment */
FIXME	MOVQ	BX, 152(R12)				/* save cs */
FIXME	MOVQ	CX, 144(R12)				/* old ip */

FIXME	MOVW	$UDSEG, 120(R12)
FIXME	MOVW	ES,  122(R12)
FIXME	MOVW	FS,  124(R12)
FIXME	MOVW	GS,  126(R12)

FIXME	MOVQ	RARG, 	0(R12)			/* system call number: up->dbgregs->ax  */
	CALL	acsyscall(SB)
NDNR:	JMP NDNR

TEXT _acsysret(SB), 1, $-4
	CLI
	SWAPGS

FIXME	MOVQ	24(RUSER), R12			/* m->proc->dbgregs */
FIXME	MOVQ	0(R12), AX			/* m->proc->dbgregs->ax */
FIXME	MOVQ	(6*8)(R12),	BP		/* m->proc->dbgregs->bp */
FIXME	ADDQ	$(15*8), R12			/* after ax--r15, 8 bytes each */

FIXME	MOVW	0(R12), DS
FIXME	MOVW	2(R12), ES
FIXME	MOVW	4(R12), FS
FIXME	MOVW	6(R12), GS
FIXME
FIXME	MOVQ	24(R12), CX			/* ip */
FIXME	MOVQ	40(R12), R11			/* flags */
FIXME
FIXME	MOVQ	48(R12), SP			/* sp */

	BYTE $0x48; SYSRET			/* SYSRETQ */

/*
 * Return from an exec() system call that we never did,
 * DX is ar0->p by the time we call it. See syscall()
 */
TEXT xactouser(SB), 1, $-4
loop:
// in case of debug break glass.	JMP loop /* for qemu. */
	CLI
	BYTE $0x65; MOVQ 0, RMACH		/* m-> (MOVQ GS:0x0, R15) */
	MOVQ	16(RMACH), RUSER		/* m->proc */
FIXME 	MOVQ	24(RUSER), R12			/* m->proc->dbgregs */
FIXME	MOVQ	144(R12), CX			/* old ip */
FIXME	MOVQ	0(R12), BX				/* save AX */
	SWAPGS
	MOVQ	$UDSEG, AX
	MOVW	AX, DS
	MOVW	AX, ES
	MOVW	AX, FS
	MOVW	AX, GS

	MOVQ	BX, AX			/* restore AX */
	MOVQ	$0x00000200, R11			/* Interrupt flags */

	MOVQ	RARG, SP			/* sp */

	BYTE $0x48; SYSRET			/* SYSRETQ */
