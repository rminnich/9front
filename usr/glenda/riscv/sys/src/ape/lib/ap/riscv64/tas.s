/*
 *	risc-v test-and-set
 *	assumes the standard A extension
 */

#include "/riscv64/include/atom.h"

#define ARG	8

#define MASK(w)	((1<<(w))-1)

/* atomically set *keyp non-zero and return previous contents */
TEXT tas(SB), $-4			/* int _tas(ulong *keyp) */
	MOV	$1, R10
	FENCE_RW
	AMOW(Amoswap, AQ|RL, 10, ARG, ARG) /* R10->(R(ARG)), old (R(ARG))->ARG */
	FENCE_RW
	RET
