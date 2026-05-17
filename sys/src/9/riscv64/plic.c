#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "ureg.h"
#include "sysreg.h"
#include "riscv64.h"
#include "../port/error.h"

enum {
	PLICBASE	= 0x0c000000,
	PLICSIZE	= 0x04000000,

	Suctxt		= 1,		/* S-mode context for hart 0 on qemu-virt */
	Defprio		= 1,
};

extern uintptr setsie(uintptr);

static Lock vctllock;
static Vctl *vctl[Ngintr];
static Plic *plic;

void
intrinit(void)
{
	uint i;

	plic = (Plic*)vmap(PLICBASE, PLICSIZE);
	if(plic == nil)
		panic("intrinit: plic vmap");

	/* mask every source */
	for(i = Firstirq; i < Ngintr; i++){
		plic->prio[i] = 0;
		plic->enabits[Suctxt][i/32] &= ~(1u << (i & 31));
	}
	plic->context[Suctxt].priothresh = 0;
	coherence();

	/* enable S-mode external interrupts in SIE CSR */
	setsie(Seie);
}

/*
 *  called by trap.c to handle external interrupts from the plic.
 *  returns 0; the plic never delivers clock interrupts.
 */
int
irq(Ureg *ureg)
{
	Vctl *v;
	uint id;

	m->intr++;
	while((id = plic->context[Suctxt].claimcompl) != 0){
		if(id < Ngintr){
			for(v = vctl[id]; v != nil; v = v->next)
				v->f(ureg, v->a);
		}
		plic->context[Suctxt].claimcompl = id;
		coherence();
	}
	return 0;
}

void
intrenable(int irq, void (*f)(Ureg*, void*), void *a, int tbdf, char *name)
{
	Vctl *v;

	if(BUSTYPE(tbdf) == BusPCI){
		pciintrenable(tbdf, f, a);
		return;
	}

	if(tbdf != BUSUNKNOWN)
		return;
	if((uint)irq < Firstirq || (uint)irq >= Ngintr)
		return;

	v = mallocz(sizeof(Vctl), 1);
	if(v == nil)
		panic("intrenable: no mem");
	v->isintr = 1;
	v->irq = irq;
	v->vno = irq;
	v->tbdf = tbdf;
	v->f = f;
	v->a = a;
	v->cpu = -1;
	strncpy(v->name, name, KNAMELEN-1);

	lock(&vctllock);
	v->next = vctl[irq];
	vctl[irq] = v;

	plic->prio[irq] = Defprio;
	plic->enabits[Suctxt][irq/32] |= 1u << (irq & 31);
	coherence();
	unlock(&vctllock);
}

void
intrdisable(int, void (*f)(Ureg*, void*), void *a, int tbdf, char*)
{
	if(BUSTYPE(tbdf) == BusPCI){
		pciintrdisable(tbdf, f, a);
		return;
	}
}
