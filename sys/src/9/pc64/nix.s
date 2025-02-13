#include "mem.h"
// NIX stuff, just keep it here.
#define M_PROC 16
#define PROC_DBGREG 2056
#define M_STACK 368
#define UREG_SS 176
#define UREG_SP 168
#define UREG_FLAGS 160
#define UREG_CS 152
#define UREG_PC 144
#define UREG_DS 120
#define UREG_ES 122
#define UREG_FS 124
#define UREG_GS 126
#define UREG_AX 0
#define UREG_BP 48
#define UREG_CX 16
#define UREG_R11 80

MODE $64

/*
 */
TEXT acsyscallentry(SB), 1, $-4
	SWAPGS
	BYTE $0x65; MOVQ 0, RMACH		/* m-> (MOVQ GS:0x0, R15) */
	MOVQ	M_PROC(RMACH), RUSER		/* m->proc */
	MOVQ	PROC_DBGREG(RUSER), R12		/* m->proc->dbgregs */

	/* save sp to r13; set up kstack so we can call acsyscall */
	MOVQ	SP, R13
	MOVQ	M_STACK(RMACH), SP			/* m->stack */
	ADDQ	$8192, SP // XXX WHOA this can't work.

	MOVQ	$UDSEG, BX		/* old stack segment */
	MOVQ	BX, UREG_SS(R12)				/* save ss */
	MOVQ	R13, UREG_SP(R12)				/* old sp */
	MOVQ	R11, UREG_FLAGS(R12)				/* old flags */
	MOVQ	$UESEG, BX		/* old code segment */
	MOVQ	BX, UREG_CS(R12)				/* save cs */
	MOVQ	CX, UREG_PC(R12)				/* old ip */

	MOVW	$UDSEG, UREG_DS(R12)
	MOVW	ES,  UREG_ES(R12)
	MOVW	FS,  UREG_FS(R12)
	MOVW	GS,  UREG_GS(R12)

	MOVQ	RARG, 	UREG_AX(R12)			/* system call number: up->dbgregs->ax  */
	CALL	acsyscall(SB)
NDNR:	JMP NDNR

TEXT _acsysret(SB), 1, $-4
	CLI
	SWAPGS

	MOVQ	PROC_DBGREG(RUSER), R12			/* m->proc->dbgregs */
	MOVQ	UREG_AX(R12), AX			/* m->proc->dbgregs->ax */
	MOVQ	UREG_BP(R12),	BP		/* m->proc->dbgregs->bp */

	MOVW	UREG_DS(R12), DS
	MOVW	UREG_ES(R12), ES
	MOVW	UREG_FS(R12), FS
	MOVW	UREG_GS(R12), GS

	MOVQ	UREG_PC(R12), CX			/* ip */
	MOVQ	UREG_FLAGS(R12), R11			/* flags */

	MOVQ	UREG_SP(R12), SP			/* sp */

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
 	MOVQ	PROC_DBGREG(RUSER), R12			/* m->proc->dbgregs */
	MOVQ	UREG_PC(R12), CX			/* old ip */
	MOVQ	UREG_AX(R12), BX				/* save AX */
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
