/*
 * riscv64 cpu capacity bits in _tos->cpucap.
 */
enum {
	Capclz	= 1<<0,		/* have CLZ instruction (from Zbb ext.) */
	Capvec	= 1<<1,		/* have vector 1.0 or later extension */
};
