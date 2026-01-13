#define BUSUNKNOWN (-1)
#define PCIWINDOW	0
#define	PCIWADDR(x)	(PADDR(x)+PCIWINDOW)

typedef struct Vctl Vctl;

typedef struct Vctl {			/* vector handlers list */
	Vctl*	next;			/* more handlers on this vector */
	void	(*f)(Ureg*, void*);	/* handler to call */
	void*	a;			/* argument to call it with */
	Vctl*	pollnxt;		/* next in poll chain, never remove */

	uchar	isintr;			/* flag: interrupt, else fault/trap */
	uchar	ismsi;			/* flag: is msi interrupt? */
	uchar	type;			/* exception or some form of intr? */
	ushort	irq;
	short	vno;
	int	tbdf;

	/* precise interrupt accounting */
	uvlong	count;			/* interrupt count */
	ulong	unclaimed;		/* # of interrupts unclaimed */
	ulong	intrunknown;		/* # of interrupts via intrunknown */
	short	cpu;			/* targetted cpu */

	char	name[KNAMELEN];		/* of driver */
} Vctl;
