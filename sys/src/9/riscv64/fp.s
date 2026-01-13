/*
 * floating point machine assist for riscv
 */
#include "mem.h"
#include "riscv64l.h"

#define SV(n)	MOVD F(n), ((n)*8)(R9)		/* save Fn in the array at R9 */
#define RST(n)	MOVD ((n)*8)(R9), F(n)

/*
 * enable this cpu's fpu, set rounding, set last fp regs,
 * leave fp in Initial state.
 */
TEXT _fpuinit(SB), $-4
	/* Off would cause ill instr traps on fp use */
	MOV	CSR(SSTATUS), R10
	MOV	$~Fsst, R9
	AND	R9, R10
	MOV	$(Initial<<Fsshft), R9
	OR	R9, R10
	MOV	R10, CSR(SSTATUS)	/* enable FPU */
	FENCE_RW

	MOV	R0, CSR(FCSR)		/* mostly to set default rounding */
	/* fall through */

TEXT fpconstset(SB), $-4
	/* set initial values to those assumed by the compiler */
	MOVD	fpzero+0(SB), F28
	MOVD	fphalf+0(SB), F29
	MOVD	fpone+0(SB), F30
	MOVD	fptwo+0(SB), F31
	FENCE_RW
	RET

TEXT getfcsr(SB), $-4
	MOV	CSR(FCSR), R(ARG)
	RET

TEXT setfcsr(SB), $-4
	MOV	R(ARG), CSR(FCSR)
	RET

TEXT fpusave(SB), $-4
	FENCE_RW				/* sifive u74 erratum cip-930 */
	MOV	CSR(SSTATUS), R9
	MOV	$Fsst, R11
	AND	R11, R9
	MOV	$(Off<<Fsshft), R10
	BEQ	R9, R10, noop		/* off, so nothing to do */
	MOV	$(Dirty<<Fsshft), R10
	BNE	R9, R10, clean		/* on but clean, just turn off */
dirty:
	/* store fp registers into argument double array before turning off */
	MOV	R(ARG), R9
	SV(0); SV(1); SV(2); SV(3); SV(4); SV(5); SV(6); SV(7); SV(8); SV(9)
	SV(10); SV(11); SV(12); SV(13); SV(14); SV(15); SV(16); SV(17); SV(18)
	SV(19); SV(20); SV(21); SV(22); SV(23); SV(24); SV(25); SV(26); SV(27)
	SV(28); SV(29); SV(30); SV(31)
clean:
	/* fall through */

	/* fp state is now clean (registers match argument array), turn off */
TEXT fpoff(SB), 1, $-4
	FENCE_RW				/* sifive u74 erratum cip-930 */
	/* Off will cause ill instr traps on fp use */
	MOV	CSR(SSTATUS), R10
	MOV	$~Fsst, R9
	AND	R9, R10
	MOV	$(Off<<Fsshft), R9	/* regs will be restored on next use */
	OR	R9, R10
	MOV	R10, CSR(SSTATUS)	/* disable FPU */
	FENCE_RW
noop:
	RET

TEXT fpon(SB), 1, $-4
	MOV	CSR(SSTATUS), R9
	MOV	$Fsst, R11
	AND	R9, R11
	MOV	$(Off<<Fsshft), R12
	BNE	R11, R12, ondone	/* on, so nothing to do */

	MOV	$~Fsst, R11
	AND	R9, R11
	MOV	$(Dirty<<Fsshft), R9	/* don't know, Dirty is safe */
	OR	R9, R11
	MOV	R11, CSR(SSTATUS)	/* enable FPU */
	FENCE_RW
ondone:
	RET

/* fpu is assumed at entry to be off, thus clean */
TEXT fpurestore(SB), $-4
	MOV	CSR(SSTATUS), R10
	MOV	$~Fsst, R9
	AND	R9, R10
	MOV	$(Clean<<Fsshft), R9
	OR	R9, R10
	MOV	R10, CSR(SSTATUS)	/* enable FPU */
	FENCE_RW

	/* load fp registers from argument array */
	MOV	R(ARG), R9
	RST(0); RST(1); RST(2); RST(3); RST(4); RST(5); RST(6); RST(7); RST(8)
	RST(9); RST(10); RST(11); RST(12); RST(13); RST(14); RST(15); RST(16)
	RST(17); RST(18); RST(19); RST(20); RST(21); RST(22); RST(23); RST(24)
	RST(25); RST(26); RST(27); RST(28); RST(29); RST(30); RST(31)

	/* set status to Clean again, after loading fregs */
	FENCE_RW
	MOV	R10, CSR(SSTATUS)
	FENCE_RW
	RET
