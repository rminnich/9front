#include "mem.h"
// NIX stuff, just keep it here.
#define M_PROC 16
#define PROC_DBGREG 2056
/* On AC, we have to have a stack.
 * We can not use the Proc stack; that's 
 * already in use on the TC.
 * For acsyscallentry, we need to use the Mach stack, not the Proc stack. */
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
	MOVQ	RMACH, SP			/* use the Mach stack, not the proc stack. */
	ADDQ 	$MACHSIZE, SP // yuck
	MOVQ	$UDSEL, BX		/* old stack segment */
	MOVQ	BX, UREG_SS(R12)				/* save ss */
	MOVQ	R13, UREG_SP(R12)				/* old sp */
	MOVQ	R11, UREG_FLAGS(R12)				/* old flags */
	MOVQ	$UESEL, BX		/* old code segment */
	MOVQ	BX, UREG_CS(R12)				/* save cs */
	MOVQ	CX, UREG_PC(R12)				/* old ip */

	MOVW	$UDSEL, UREG_DS(R12)
	MOVW	ES,  UREG_ES(R12)
	MOVW	FS,  UREG_FS(R12)
	MOVW	GS,  UREG_GS(R12)

	MOVQ	BP, 	UREG_BP(R12)			/* system call number: up->dbgregs->bp  */

	MOVQ	R12, RARG 						// ureg

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
	CLI
	BYTE $0x65; MOVQ 0, RMACH		/* m-> (MOVQ GS:0x0, R15) */
	MOVQ	16(RMACH), RUSER		/* m->proc */
 	MOVQ	PROC_DBGREG(RUSER), R12			/* m->proc->dbgregs */
	MOVQ	UREG_PC(R12), CX			/* old ip */
	MOVQ	UREG_AX(R12), BX				/* save AX */
	SWAPGS
	MOVQ	$UDSEL, AX
	MOVW	AX, DS
	MOVW	AX, ES
	MOVW	AX, FS
	MOVW	AX, GS
loop:	CMPQ AX,$0
	//JNE loop useful if you are debugging in qemu.
	MOVQ	BX, AX			/* restore AX */
	MOVQ	$0x00000200, R11			/* Interrupt flags */

	MOVQ	RARG, SP			/* sp */

	BYTE $0x48; SYSRET			/* SYSRETQ */

/*
 * Interrupt/exception handling.
 */

MODE $64

TEXT _acintrp<>(SB), 1, $-4			/* no error code pushed */
	PUSHQ	AX				/* save AX */
	MOVQ	8(SP), AX			/* idthandlers(SB) PC */
	JMP	_acintrcommon

TEXT _acintre<>(SB), 1, $-4			/* error code pushed */
	XCHGQ	AX, (SP)
_acintrcommon:
	MOVBQZX	(AX), AX
	XCHGQ	AX, (SP)

	SUBQ	$24, SP				/* R1[45], [DEFG]S */
	CMPW	48(SP), $KESEL	/* old CS */
	JEQ	_acintrnested

	MOVQ	RUSER, 0(SP)
	MOVQ	RMACH, 8(SP)
	MOVW	DS, 16(SP)
	MOVW	ES, 18(SP)
	MOVW	FS, 20(SP)
	MOVW	GS, 22(SP)

	SWAPGS
	BYTE $0x65; MOVQ 0, RMACH		/* m-> (MOVQ GS:0x0, R15) */
	MOVQ	16(RMACH), RUSER		/* up */

_acintrnested:
	PUSHQ	R13
	PUSHQ	R12
	PUSHQ	R11
	PUSHQ	R10
	PUSHQ	R9
	PUSHQ	R8
	PUSHQ	BP
	PUSHQ	DI
	PUSHQ	SI
	PUSHQ	DX
	PUSHQ	CX
	PUSHQ	BX
	PUSHQ	AX

	MOVQ	SP, RARG
	PUSHQ	SP
	CALL	actrap(SB)

TEXT _acintrr<>(SB), 1, $-4			/* so ktrace can pop frame */
	POPQ	AX

	POPQ	AX
	POPQ	BX
	POPQ	CX
	POPQ	DX
	POPQ	SI
	POPQ	DI
	POPQ	BP
	POPQ	R8
	POPQ	R9
	POPQ	R10
	POPQ	R11
	POPQ	R12
	POPQ	R13

	CMPQ	48(SP), $KESEL
	JEQ	_aciretnested

	SWAPGS
	MOVW	22(SP), GS
	MOVW	20(SP), FS
	MOVW	18(SP), ES
	MOVW	16(SP), DS
	MOVQ	8(SP), RMACH
	MOVQ	0(SP), RUSER

_aciretnested:
	ADDQ	$40, SP
	IRETQ

