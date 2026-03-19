#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include <tos.h>
#include "ureg.h"
#include "../riscv64/sysreg.h"
#include "riscv64.h"
#include "io.h"

enum {
	Read = 0,
	Write = 1,
	Intrdebug = 1,
};

/*
 * these are potential constant expressions.  local clock & ipi
 * interrupts are most frequent, then exceptions (page faults & system
 * calls), then global interrupts.
 *
 * these are not safe macros; args may be evaluated multiple times.
 */
#define vctlidx(intrno, type) ( \
	(type) == Localintr? Ngintr+(intrno): \
	(type) == Exception? Ngintr+Nlintr+(intrno): \
	(type) == Globalintr? (intrno): -1)

#define intrtype(vno) ( \
	((uint)vno) < Ngintr? Globalintr: \
	((uint)vno) < Ngintr + Nlintr? Localintr: \
	((uint)vno) < Ngintr + Nlintr + Nexc? Exception: Unknownflt)

Vctl *
newvec(void (*f)(Ureg*, void*), void* a, int tbdf, char *name)
{
	Vctl *v;

	v = smalloc(sizeof(Vctl));
	v->tbdf = tbdf;
	v->f = f;
	v->a = a;
	v->cpu = -1;			/* could be any cpu until overridden */
	strncpy(v->name, name, KNAMELEN);
	v->name[KNAMELEN-1] = 0;
	return v;
}

void
trapsclear(void)
{
//	clockoff();
	coherence();
//	clearipi();
}

void
trapvecs(void)
{
// done in touser	putsscratch((uintptr)m);	/* high virtual */
// done in early startup	putstvec(strap);		/* " */
	putsie(Superie);		/* individual enables; still splhi */
}

/* map cpu (machno) to plic context for Super mode */
int
cpu2context(uint cpu)
{
	Mach *mach;

	if (cpu >= nelem(active.machs))
		panic("cpu2context: cpu %d >= %d", cpu, nelem(active.machs));
	mach = MACHP(cpu);
	if (mach == nil || !mach->online)
		return -1;	/* cpu nil mach or offline; shouldn't happen */
	return mach->plicctxt + Super;
}

enum {
	Nopri,
	Lowpri,
	Highpri = 1,		/* empirically, 7 on icicle */
};

/* probe WARL register by writing wr to it, reading and restoring */
ulong
warl(ulong *addr, ulong wr)
{
	ulong old, new;

	old = *addr;		/* save current value */
	*addr = wr;		/* probe WARL register */
	coherence();

	new = *addr;		/* read legal value back */
	*addr = old;		/* restore */
	coherence();
	return new;
}

static int
gotamoplic(int)
{
	if (soc.plic)
		amoorw((ulong *)soc.plic, 0);
	else
		iprint("gotamoplic: zero soc.plic\n");
	return 0;
}

static Lock pliclock;

int
ismem(ulong *wd)
{
	ulong owd, nwd;
	Mpl pl;
	int ok;

	pl = splhi();
	owd = *wd;
	nwd = owd ^ (VMASK(4) << 28);	/* test high bits at least */
	*wd = nwd;
	coherence();
	ok = *wd == nwd;
	*wd = owd;			/* restore orig value */
	splx(pl);
	return ok;
}

static void
plicdisctxtirq(Plic *plic, int cpu, uint wd, uint bit)
{
	int ctxt;

	ctxt = cpu2context(cpu);
	if (ctxt >= 0) {
		if (soc.nodevamo) {
			lock(&pliclock);
			plic->enabits[ctxt][wd] &= ~bit;
			unlock(&pliclock);
		} else
	panic("			amoandnw(&plic->enabits[ctxt][wd], bit);");
		/* leave plic->context[ctxt].priothresh alone */
	}
}

void
plicdisable(uint irq)
{
	int cpu, wd, bit;
	Plic *plic = (Plic *)soc.plic;

	if (plic == nil || irq >= Ngintr)
		return;
	plic->prio[irq] = Nopri;
	wd = irq>>5; //BITMAPWD(irq);
	bit = (1 << (irq&0x1f)); // BITMAPBIT(irq);
	// just vector them on to cpu0. 
	cpu = 0;
	//for (cpu = 0; cpu < sys->nonline; cpu++)
		plicdisctxtirq(plic, cpu, wd, bit);

}
void
plicoff(void)
{
	int irq;
	Plic *plic = (Plic *)soc.plic;

	if (plic == nil)
		return;
	if (Intrdebug)
		iprint("plic off\n");
	for (irq = Ngintr-1; irq >= Firstirq; irq--)
		plic->prio[irq] = Nopri;	/* don't interrupt on irq */
}

/* returns interrupt id (irq); 0 is none */
uint
plicclaim(uint ctxt)
{
	Plic *plic = (Plic *)soc.plic;

	return (plic? plic->context[ctxt].claimcompl: 0);
}

void
plicinit(void)
{

	uint irq, maxpri, newpri, ctxt;
	ulong *wd;
	Mpl s;
	Plic *plic = (Plic *)soc.plic;

	if (plic == nil)
		return;
	if (probeulong((ulong *)plic, Read) < 0) {
		iprint("no response from plic at %#p\n", plic);
		soc.plic = 0;
		return;
	}
#ifdef U84FUCKTHAT
	if (plic->eicclkdisable) {
		plic->eicclkdisable = 0;
		coherence();
		delay(10);
		iprint("plic: cleared clock gate disable (eswin feature)\n");
	}
#endif
	if (!soc.nodevamo)	/* test only if user said devamo works */
		soc.nodevamo = 1; //!haveinstr(gotamoplic, 0);
	if (soc.nodevamo)
		iprint("plic: no device amo accesses allowed\n");
	maxpri = warl(&plic->context[cpu2context(0)].priothresh, ~0);
	if (maxpri == 0) {
		s = splhi();
		for (irq = Firstirq; irq < Ngintr && maxpri == 0; irq++) {
			newpri = warl(&plic->prio[irq], ~0);
			if (newpri > maxpri)
				maxpri = newpri;
		}
		splx(s);
	}
	if (maxpri == 0) {
		plicoff();
		iprint("plic at %#p: max priority 0, ignoring plic\n", plic);
		soc.plic = 0;
		return;
	}
	sys->maxplicpri = maxpri;
	if (soc.newmach)
		iprint("plic at %#p: max prio %#ux\n", plic, maxpri);

	/* don't leave any irqs enabled */
	for (irq = Firstirq; irq < Ngintr; irq++)
		plicdisable(irq);

	/* enumerate contexts; could inform soc.hobbled, etc. */
	if (!soc.newmach)
		return;
	iprint("plic: testing enables; valid contexts:");
	for (ctxt = 0; ctxt < 128; ctxt++) {
		wd = &plic->enabits[ctxt][0];
		if (probeulong(wd, Write) >= 0 && ismem(wd))
			iprint(" %d", ctxt);
	}
	iprint("\n");
}

