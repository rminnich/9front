/*
 * Trap catcher for supervisor mode of RV64.
 * Includes early-trap handlers that print the exception and [SM]EPC.
 */

#include "mem.h"
#include "riscv64l.h"

TMP=9
UART0=10
TMP2=11
CAUSE=12
MMODE=13
EXCPC=14

/* side-effect: patches Ureg with regsave's R2 */
#define PUSHALLS \
	SUB	$UREGSIZE, R2;		/* room for pc, regs, csrs */\
	S(1);\
	JAL	LINK, pushr2_31(SB);\
	/* patch up Ureg on stack */\
	MOV	SAVESR2(R(MACH)), R9;\
	MOV	R9, (2*XLEN)(R2);	/* push saved R2 from regsave */\
	/* save CSRs */\
	MOV	CSR(SEPC), R9;		MOV R9, 0(R2);	/* push saved pc */\
	MOV	CSR(SSTATUS), R9;	MOV R9, (32*XLEN)(R2);\
	MOV	CSR(SIE), R9;		MOV R9, (33*XLEN)(R2);\
	MOV	CSR(SCAUSE), R9;	MOV R9, (34*XLEN)(R2);\
	MOV	CSR(STVAL), R9;		MOV R9, (35*XLEN)(R2);\
	MOV	$Mppsuper, R9;		MOV R9, (36*XLEN)(R2); /* curmode */ \
	MOV	CSR(SSCRATCH), R9;	MOV R9, (37*XLEN)(R2)

/*
 * strap copes with supervisor-mode faults, from user or kernel mode
 * (e.g., interrupt, system call, page fault during system call).
 * the trap mechanism saved the return address in CSR(SEPC), and masked
 * interrupts.  Upon entry, it may be necessary to save a few registers
 * in m->regsave[] to free them for use until all the registers are saved.
 * SP should be valid on entry from kernel mode, at least.
 *
 * The strategy is to exchange R(MACH) and CSR(SSCRATCH), which is pre-loaded
 * with this CPU's Mach pointer, save all registers and a few CSRs on a stack
 * (empty up->kstack if trap came from user mode), call the common trap()
 * function, restore most or all of the stacked registers and CSRs, exchange
 * R(MACH) and CSR(SSCRATCH), and return from trap via SRET to the address in
 * CSR(SEPC).  Depending upon trap()'s action, the interrupted program could
 * potentially continue unaware of the trap.
 *
 * Note well that the exact stack layout of a trap is known in various other
 * places too (e.g., main.c, mmu.c, trap.c).  The intended layout is: a Ureg
 * struct at the very end of up->kstack if trapped from user mode, or
 * somewhere on m->stack if trapped from kernel, with a pointer to that Ureg
 * just before (below) it, for the argument to trap().
 *
 * Upon return from trap(), be sure to pop user's registers from Ureg, so
 * that changes to this Ureg elsewhere (e.g. trap, sysexec) are reflected
 * back to the user process.
 */
TEXT strap(SB), 1, $-4
	/* this should be a no-op if we trapped from kernel mode. */
	CSRRW	CSR(SSCRATCH), R(MACH), R(MACH)	/* save old m, load new m */
	BEQ	R(MACH), trapaligned
	FENCE

	/* stash a few regs in m->regsave to free working registers */
	MOV	R2, SAVESR2(R(MACH))		/* save SP */
	MOV	R3, SAVESR3(R(MACH))		/* save SB */
	MOV	R4, SAVESR4(R(MACH))		/* save j[acl] temp */
	MOV	R9, SAVESR9(R(MACH))

	MOV	$setSB(SB), R3
	/* discharge any reservation we interrupted so we can use LR/SC */
	MOV	$dummysc(SB), R9
	SCW(0, 9, 0)

#ifdef TRAPDEBUG
	MOV	MCONSUART(R(MACH)), R(UART0)
	CONSPUT($'S')
//	JAL	R0, wfi(SB)		/* JMP only takes local labels */
#endif

	MOV	CSR(SSTATUS), R9
	AND	$Spp, R9
	BEQ	R9, fromuser			/* prev not super */

	/*
	 * trapped from kernel mode: R2, SB, MACH, and USER were all okay,
	 * in theory.  this can't be a system call.
	 * we shall use the current kernel stack.
	 */
	MOV	$recktrap(SB), R9
	MOV	R9, CSR(STVEC)		/* if we fault here, report it */

	/* if we fault here, it's probably due to a bad SP or SB */
	MOV	SAVESR4(R(MACH)), R4
	MOV	SAVESR9(R(MACH)), R9
	PUSHALLS			/* patches R2 from regsave into Ureg */

	MOV	$strap(SB), R9
	MOV	R9, CSR(STVEC)		/* restore trap vector */

	MOV	$setSB(SB), R3	/* should be redundant, barring disaster */
/**/	CALLTRAP  /* C compiler is free to write regs but R2, SB, USER, MACH */
	POPCSR_MOSTREGS(SEPC, SSTATUS)

	/* MACH, SB are still correct; USER might not be after resched. */
	MOV	MACHPROC(R(MACH)), R(USER)	/* up = m->proc */
	JMP	return			/* to kernel */

	/*
	 * trapped from user mode: SB, MACH, R2, USER all need(ed) reloading.
	 * old MACH value is in CSR(SSCRATCH).  R2 is saved in regsave.
	 */
fromuser:
	MOV	$setSB(SB), R3

	MOV	$recutrap(SB), R9
	MOV	R9, CSR(STVEC)		/* if we fault here, report it */

	MOV	SAVESR9(R(MACH)), R9

	MOV	R4, SAVESR4(R(MACH))	/* j[acl] temp */
	MOV	R6, SAVESR6(R(MACH))	/* save old R6 (now up) */
	MOV	MACHPROC(R(MACH)), R(USER) /*  up = m->proc; up is R6 */

	/*
	 * can't use user-mode stack.  switch to up->kstack.
	 */
	MOV	(2*XLEN)(R(USER)), R2	/* switch to empty up->kstack */
	ADD	$KSTACK, R2		/* must be multiple of sizeof(vlong) */

	PUSHALLS			/* patches R2 from regsave into Ureg */
	MOV	SAVESR3(R(MACH)), R9
	MOV	R9, (3*XLEN)(R2)	/* patch old R3 into Ureg */
	MOV	SAVESR4(R(MACH)), R4
	MOV	R4, (4*XLEN)(R2)	/* patch old R4 into Ureg */
	MOV	SAVESR6(R(MACH)), R9
	MOV	R9, (6*XLEN)(R2)	/* patch old R6 into Ureg */

	MOV	$strap(SB), R9
	MOV	R9, CSR(STVEC)		/* restore trap vector */

	/* sched could be called and processes switched (e.g, syscall) */
/**/	CALLTRAP  /* C compiler is free to write regs but R2, SB, MACH, USER */

syscallret:
	/* SP must point at our Ureg here.  up may have changed since entry. */
	/*
	 * before a syscall (ureg->cause == Envcalluser), the compiler pushed
	 * any registers it cared about (other than those it considers global,
	 * like SB, R2, MACH, USER), so we needn't save nor restore most of
	 * them before returning from a syscall.  We do need to propagate
	 * ureg->pc and ureg->sp back into user registers.
	 *
	 * Of course, other user traps need to have all registers restored.
	 */
	POPCSR_MOSTREGS(SEPC, SSTATUS)
	MOV	(3*XLEN)(R2), R3	/* restore user's SB */
	MOV	(6*XLEN)(R2), R6
return:
	MOV	(2*XLEN)(R2), R2	/* restore SP */
	FENCE
	CSRRW	CSR(SSCRATCH), R(MACH), R(MACH) /* restore R7, reload saved m */
	SRET				/* to CSR(SEPC) address */

/* rfork return for child processes */
TEXT sysrforkret(SB), 1, $-4
	SPLHI

	/* SP must point at our Ureg here.  up may have changed since entry. */
	MOV	R0, (ARG*XLEN)(R2)	/* zero saved R(ARG) in ureg */
	JMP	syscallret

/*
 * dump various fatal errors
 */
TEXT recmtrapalign(SB), 1, $-4
	MOV	$1, R(MMODE)
	MOV	CSR(MCAUSE), R(CAUSE)
	MOV	CSR(MEPC), R(EXCPC)
	JAL	R0, trapcomm(SB)

#define INITSTKSIZE 4096
#define SBIALIGN 8

	GLOBL	dummysc(SB), $4

TEXT pushr2_31(SB), 1, $-4
	S(2); S(3); S(4); S(5); S(6); S(7)
	S(8); S(9); S(10); S(11); S(12); S(13); S(14); S(15); S(16); S(17)
	S(18); S(19); S(20); S(21); S(22); S(23); S(24); S(25); S(26)
	S(27); S(28); S(29); S(30); S(31)
	RET
TEXT popr8_31(SB), 1, $-4
	P(8); P(9); P(10); P(11); P(12); P(13); P(14); P(15); P(16); P(17)
	P(18); P(19); P(20); P(21); P(22); P(23); P(24); P(25); P(26)
	P(27); P(28); P(29); P(30); P(31)
	RET

TEXT recktrap(SB), 1, $-4
TEXT recutrap(SB), 1, $-4
	/* fault in prologue: fix most-critical registers */
	MOV	$setSB(SB), R3
	MOV	$panicstk+(INITSTKSIZE)(SB), R2
	AND	$~(SBIALIGN-1), R2
TEXT rectrapalign(SB), 1, $-4
trapaligned:
	MOV	R0, R(MMODE)
	MOV	CSR(SCAUSE), R(CAUSE)
	MOV	CSR(SEPC), R(EXCPC)

	/*
	 * something seriously bad has happened at an inconvenient time.
	 * print the exception number and PC of the fault, then idle.
	 */
	/* jl bug? using a plain label here generates bad code */
TEXT trapcomm(SB), 1, $-4
	SPLHI
	MOV	CSR(SSCRATCH), R(MACH)
	MOV	MCONSUART(R(MACH)), R(UART0)
	MOV	$setSB(SB), R3
	MOVW	$(1ul<<31), R(TMP2)		/* sifive tx notready bit */

	CONSWAIT
	CONSOUT($'\r')
	CONSOUT($'\n')
	CONSOUT($'?')
	CONSOUT($'!')
	BEQ	R(MMODE), supermode
	CONSOUT($'m')
supermode:
	/* prepare to print fault cause */
	MOV	$'e', R(TMP)
	BGE	R(CAUSE), isexcept
	MOV	$'i', R(TMP)			/* it's an interrupt */
isexcept:
	CONSOUT(R(TMP))
	ADD	$'0', R(CAUSE), R(TMP)
	CONSOUT(R(TMP))
	CONSOUT($'@')

	/* print uintptr EXCPC manually in hex */
	CONSOUT($'0')
	CONSOUT($'x')
	CONSWAIT
	MOV	$(8*XLEN-4), R21
nextdig:
	SRL	R21, R(EXCPC), R(TMP)
	//AND	$MASK(4), R(TMP)
	ADD	$'0', R(TMP)
	MOV	$'9', R22
	BGE	R(TMP), R22, decimal		/* '9' >= R(TMP)? */
	ADD	$('a' - ('9'+1)), R(TMP)
decimal:
	CONSOUT(R(TMP))
	SUB	$4, R21
	BGE	R21, nextdig
	CONSOUT($'\r')
	CONSOUT($'\n')

#ifdef ATTEMPT_PANIC
	SUB	$(8*XLEN), R2
TEXT excalign(SB), 1, $-4
	MOV	$epcfmt(SB), R(ARG)
	MOV	R(EXCPC), arg-8(SP)
	JAL	LINK, prf(SB)
	MOV	R0, R(ARG)
	JAL	LINK, exit(SB)

	GLOBL	epcfmt(SB), $9
	DATA	epcfmt(SB)/8, $"epc %#p\n"
	DATA	epcfmt+8(SB)/1, $"\0"
#endif

	JAL	R0, wfi(SB)		/* JMP only takes local labels */

/*
 * switch to user mode with stack pointer from R(ARG), at start of text.
 * used to start process 1 (init).
 */
TEXT gtouser(SB), 1, $-4
	FENCE
	FENCE_I

	MOV	R0, LINK
	MOV	$(UTZERO+8*BY2WD), R12	/* skip unextended exec hdr of init */
	MOV	R12, CSR(SEPC)		/* new pc */
	MOV	R(ARG), R2		/* new sp */
	SRET				/* off to rv64 user mode */

GLOBL	panicstk(SB), $INITSTKSIZE
