/*
 *	risc-v test-and-set
 */
#include <atom.h>

#define ARG	8

/*
 * atomically set *keyp non-zero and return previous contents
 * (zero means that we have the lock, other values mean someone else does).
 * see atom(2) for the properties it must implement.
 */
TEXT _tas(SB), $-4			/* int _tas(ulong *keyp) */
	MOV	$1, R10
	FENCE_RW
	AMOW(Amoswap, AQ|RL, 10, ARG, ARG) /* R10->(R(ARG)), old (R(ARG))->ARG */
	FENCE_RW
	RET
