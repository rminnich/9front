/*
 * Memory and machine-specific definitions.  Used in C and assembler.
 */
#define KiB		1024u			/* Kibi 0x0000000000000400 */
#define MiB		1048576u		/* Mebi 0x0000000000100000 */
#define GiB		1073741824u		/* Gibi 000000000040000000 */

/*
 * Sizes:
 * 	L0	L1	L2	L3
 *	4K	2M	1G	512G
 *	16K	32M	64G	128T
 *	64K	512M	4T	-
 */
#define	PGSHIFT		12		/* log(BY2PG) */
#define	BY2PG		(1ULL<<PGSHIFT)	/* bytes per page */
#define	ROUND(s, sz)	(((s)+(sz-1))&~(sz-1))
#define	PGROUND(s)	ROUND(s, BY2PG)

/* effective virtual address space */
#define EVASHIFT	39
#define EVAMASK		((1ULL<<EVASHIFT)-1)

#define PTSHIFT		(PGSHIFT-3)
#define PTLEVELS	(((EVASHIFT-PGSHIFT)+PTSHIFT-1)/PTSHIFT)
/* PPN */
#define PTLX(v, l)	((((v) & EVAMASK) >> (PGSHIFT + (l)*PTSHIFT)) & ((1 << PTSHIFT)-1))
#define PGLSZ(l)	(1ULL << (PGSHIFT + (l)*PTSHIFT))

#define PTL1X(v, l)	(L1TABLEX(v, l) | PTLX(v, l))
#define L1TABLEX(v, l)	(L1TABLE(v, l) << PTSHIFT)
#define L1TABLES	((-KSEG0+PGLSZ(2)-1)/PGLSZ(2))
#define L1TABLE(v, l)	(L1TABLES - ((PTLX(v, 2) % L1TABLES) >> (((l)-1)*PTSHIFT)) + (l)-1)
#define L1TOPSIZE	(1ULL << (EVASHIFT - PTLEVELS*PTSHIFT))

#define	MAXMACH		8			/* max # cpus system can run */
#define	MACHSIZE	(8*KiB)

#define KSTACK		(8*KiB)
#define STACKALIGN(sp)	((sp) & ~7)		/* bug: assure with alloc */
#define TRAPFRAMESIZE	(38*8)

//#define VDRAM		(0x0000000100000000ULL)	/* 0x100000000 - */
#define VDRAM		(0x0000000080000000ULL)	/* 0x080000000 - */
#define	KTZERO		(VDRAM + 0x200000)	/* VDRAM - kernel text start */

#define	KZERO		(0x0000000000000000ULL)	/* 0x00000000 - kernel address space */
#define KLIMIT 		klimit
#define iskern(v) (((uintptr)v)<=UZERO)

/* shared kernel page table for TTBR1 */
#define L1		(L1TOP-L1SIZE)
#define L1SIZE		((L1TABLES+PTLEVELS-2)*BY2PG)
#define L1TOP		((MACHADDR(MAXMACH-1)-L1TOPSIZE)&-BY2PG)

#define MACHADDR(n)	(KTZERO-((n)+1)*MACHSIZE)

#define CONFADDR	(VDRAM + 0x10000)	/* 0x40010000 */

#define BOOTARGS	((char*)CONFADDR)
#define BOOTARGSLEN	0x10000

#define	REBOOTADDR	(VDRAM-KZERO + 0x20000)	/* 0x40020000 */

// Sv39 max address is 1<<38-1. Choose 1<<36. (1<<32 for now)
// This gives kernel a lot of memory, and over time,
// user gets even more.
#define	UZERO		(0x1000000000ULL)			/* user segment */
#define	UTZERO		(UZERO)		/* user text start */
#define	USTKTOP		((UZERO<<1)-BY2PG)	/* user segment end +1 */
#define	USTKSIZE	(16*1024*1024)		/* user stack size */

#define BLOCKALIGN	64			/* only used in allocb.c */

/*
 * Sizes
 */
#define BI2BY		8			/* bits per byte */
#define BY2SE		4
#define BY2WD		8
#define BY2V		8			/* only used in xalloc.c */

#define	PTEMAPMEM	(1024*1024)
#define	PTEPERTAB	(PTEMAPMEM/BY2PG)
#define	SEGMAPSIZE	8192
#define	SSEGMAPSIZE	16
#define	PPN(x)		((x)&~(BY2PG-1))

#define PTEVALID	(1<<0)
#define PTEREAD		(1<<1)
#define PTEWRITE	(1<<2)
#define PTEEXEC		(1<<3)
#define PTEUSER		(1<<4)
#define PTEGLOBAL	(1<<5)
#define PTEACCESSED	(1<<6)
#define PTEDIRTY	(1<<7)
#define PTERONLY	PTEREAD
#define PTEATTR		((uintptr)0xf)

#define PTEUSERREAD (PTEVALID | PTEREAD | PTEEXEC | PTEUSER | PTEACCESSED)
#define PTEUSERWRITE (PTEUSERREAD | PTEWRITE | PTEDIRTY)

/* bogus. */
#define PTECACHED 0
#define PTEUNCACHED 0

#define PA2PTE(pa)	((pa>>12)<<10)
#define PTE2PA(pte)	((pte>>10)<<12)
#define PTE2ATTR(pa)	((pa)&PTEATTR)

/*
 * Physical machine information from here on.
 *	PHYS addresses as seen from the cpu.
 *	BUS  addresses as seen from peripherals
 */
#define	PHYSDRAM	0

#define MIN(a, b)	((a) < (b)? (a): (b))
#define MAX(a, b)	((a) > (b)? (a): (b))
/* from Geoff */
#define MASK(w)	 ((1u  <<(w)) - 1)
#define VMASK(w) ((1ull<<(w)) - 1)
#define HARTMAX 32
#define SBIALIGN	16LL		/* stack alignment for SBI calls */
