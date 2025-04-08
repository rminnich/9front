/*
 * atomic memory operations and fences for rv64a
 *
 *	assumes the standard A extension
 *	LR/SC only work on cached regions
 */
#define	Amoadd	0
#define	Amoswap	1
#define	Amolr	2
#define	Amosc	3
#define	Amoxor	4
#define	Amoor	010
#define	Amoand	014
#define	Amomin	020
#define	Amomax	024
#define	Amominu	030
#define	Amomaxu	034

/* AMO operand widths */
#define	Amow	2		/* long */
#define	Amod	3		/* vlong */

/* sifive e51 erratum rock-3 requires AQ and RL for correct operation */
#define AQ	(1<<26)		/* acquire */
#define RL	(1<<25)		/* release */

/* instructions unknown to the assembler */
/*
 * atomically (rd = (rs1); (rs1) = rd func rs2).
 * setting AQ and RL produces sequential consistency by acting as fences,
 * *but only for this AMO operand*, not in general.
 */
#define AMOW(func, opts, rs2, rs1, rd) \
	WORD $(((func)<<27)|((rs2)<<20)|((rs1)<<15)|(Amow<<12)|((rd)<<7)|057|(opts))
#define LRW(rs1, rd)		AMOW(Amolr, AQ, 0, rs1, rd)
#define SCW(rs2, rs1, rd)	AMOW(Amosc, AQ|RL, rs2, rs1, rd)

#define FNC_I	(1<<3)		/* mmio */
#define FNC_O	(1<<2)
#define FNC_R	(1<<1)		/* memory */
#define FNC_W	(1<<0)
#define FNC_RW	(FNC_R | FNC_W)
#define FNC_ALL	0xf		/* 0xf = all i/o, memory r & w ops */

#define FENCE	WORD $(0xf | FNC_ALL<<24 | FNC_ALL<<20)
#define FENCE_RW WORD $(0xf | FNC_RW<<24 | FNC_RW<<20)	/* memory, not mmio */
#define PAUSE	WORD $(0xf | FNC_W<<24)	/* FENCE pred=W, Zihintpause ext'n */
#define FENCE_I	WORD $(0xf | 1<<12)
/* as and vaddr are register numbers */
#define SFENCE_VMA(as, vaddr) WORD $(011<<25|(as)<<20|(vaddr)<<15|0<<7|SYSTEM)

#define SEXT_W(r) ADDW R0, r

/* rv64 instructions */
#define AMOD(func, opts, rs2, rs1, rd) \
	WORD $(((func)<<27)|((rs2)<<20)|((rs1)<<15)|(Amod<<12)|((rd)<<7)|057|(opts))
#define LRD(rs1, rd)		AMOD(Amolr, AQ, 0, rs1, rd)
#define SCD(rs2, rs1, rd)	AMOD(Amosc, AQ|RL, rs2, rs1, rd)

#define	SLLI64(rs2, rs1, rd) \
	WORD $((rs2)<<20 | (rs1)<<15 | 1<<12 | (rd)<<7 | 0x13)
#define	SRLI64(rs2, rs1, rd) \
	WORD $((rs2)<<20 | (rs1)<<15 | 5<<12 | (rd)<<7 | 0x13)
