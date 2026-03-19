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
#include "trap.h"

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

int	printenables = 1;		/* debugging control */

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

#define BITMAPWD(x) ((x)>>5)
#define BITMAPBIT(x) (1 << ((x)&0x1f))
void
plicdisable(uint irq)
{
	int cpu, wd, bit;
	Plic *plic = (Plic *)soc.plic;

	if (plic == nil || irq >= Ngintr)
		return;
	plic->prio[irq] = Nopri;
	wd = BITMAPWD(irq);
	bit = BITMAPBIT(irq);
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

/* enabits[ctxt][wd] & bit corresponds to the irq & cpu context in question */
static void
plicenactxtirq(Plic *plic, int cpu, uint wd, uint bit)
{
	int ctxt;
	ulong *enawd;

	ctxt = cpu2context(cpu);
	if (ctxt < 0)
		return;
	enawd = &plic->enabits[ctxt][wd];
	if (ctxt >= 0 && (*enawd & bit) == 0) {
		/* PL: allow all intrs of all priorities on this cpu */
		plic->context[ctxt].priothresh = Nopri;
		coherence();
		/*
		 * AMO causes an access fault on Jupiter, apparently because
		 * its PMA unhelpfully disallows atomic (A) accesses to
		 * device registers.
		 *
		 * From: guoqun.ma@spacemit.com
		 * Subject: Re: odd PMA in Jupiter board with Spacemit X60
		 *
		 * I would like to clarify that this is the original design of
		 * our chip(as a feature), not a mistake.
		 * [referring to AMO access faults on PLIC registers.]
		 */
		if (soc.nodevamo) {
			lock(&pliclock);
			*enawd |= bit;
			unlock(&pliclock);
		} else
			panic("amoorw(enawd, bit);");
	}
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

/*
 * we had enabled the interrupt in all S contexts, and let the fastest cpu
 * service it, but routing them all to cpu0 seems to be about the same speed.
 */
int
plicenable(uint irq)
{
	int cpu, initcpu, wd, bit, ctxt;
	Mpl s;
	Plic *plic = (Plic *)soc.plic;

	if (plic == nil || irq >= Ngintr)
		return -1;
	wd = BITMAPWD(irq);
	bit = BITMAPBIT(irq);
	// we are using sbi uart, it's drained.	drainuart();			/* in case we are enabling the uart */
	s = splhi();
	/* sys->nonline will often be 1 here */
	initcpu = cpu = 0;
	ctxt = cpu2context(cpu);
	if (ctxt < 0)
		panic("plicenable: negative ctxt %d for cpu%d", ctxt, cpu);
	print("only enabling cpu 0; fix me\n"); //	for (; cpu == 0 || Tryallcpus && cpu < sys->nonline; cpu++)
	cpu = 0;
		plicenactxtirq(plic, cpu, wd, bit);
	coherence();
	plic->prio[irq] = sys->maxplicpri? sys->maxplicpri: Highpri;
	splx(s);
	if (Trapdebug || Intrdebug)
		iprint("plic: enabled irq %d at prio %ld context %d for cpu %d\n",
			irq, plic->prio[irq], ctxt, initcpu);
	USED(cpu, initcpu);
	return 0;
}

/* enable or disable all global interrupts on secondary cpus */
void
secintrs(int on)
{
	USED(on);
	panic("secintrs");
#ifdef xxx
	int irq, cpu, wd;
	uint bit;
	Mpl s;
	Plic *plic = (Plic *)soc.plic;

	if (plic == nil)
		return;
	if (Intrdebug)
		iprint("secintrs on=%d\n", on);
	s = splhi();
	for (irq = Firstirq; irq < Ngintr; irq++) {
		if (plic->prio[irq] == Nopri)
			continue;
		wd = BITMAPWD(irq);
		bit = BITMAPBIT(irq);
		for (cpu = 1; cpu < sys->nonline; cpu++)
			(on? plicenactxtirq: plicdisctxtirq)(plic, cpu, wd, bit);
	}
	splx(s);
#endif
}

void
intrall(void)
{
	secintrs(1);
}

void
intrcpu0(void)
{
	secintrs(0);
}

void
plicdisable(uint irq)
{
	USED(irq);
	panic("plicdisable");
#ifdef XXX
	int cpu, wd, bit;
	Plic *plic = (Plic *)soc.plic;

	if (plic == nil || irq >= Ngintr)
		return;
	plic->prio[irq] = Nopri;
	wd = BITMAPWD(irq);
	bit = BITMAPBIT(irq);
	for (cpu = 0; cpu < sys->nonline; cpu++)
		plicdisctxtirq(plic, cpu, wd, bit);
#endif
}

void
plicnopend(uint irq)			/* unused */
{
	USED(irq);
	panic("picnopend");
#ifdef XXX
	Plic *plic = (Plic *)soc.plic;

	if (plic == nil || irq >= Ngintr)
		return;
	if (soc.nodevamo) {
		lock(&pliclock);
		plic->pendbits[BITMAPWD(irq)] &= ~BITMAPBIT(irq);
		unlock(&pliclock);
	} else
		amoandnw(&plic->pendbits[BITMAPWD(irq)], BITMAPBIT(irq));
#endif
}

void
plicoff(void)
{
	panic("plicoff");
#ifdef xxx
	int irq;
	Plic *plic = (Plic *)soc.plic;

	if (plic == nil)
		return;
	if (Intrdebug)
		iprint("plic off\n");
	for (irq = Ngintr-1; irq >= Firstirq; irq--)
		plic->prio[irq] = Nopri;	/* don't interrupt on irq */
#endif
}

/* returns interrupt id (irq); 0 is none */
uint
plicclaim(uint ctxt)
{
	Plic *plic = (Plic *)soc.plic;

	return (plic? plic->context[ctxt].claimcompl: 0);
}

void
pliccompl(uint irq, uint ctxt)
{
	Plic *plic = (Plic *)soc.plic;

	if (plic)
		plic->context[ctxt].claimcompl = irq;
}

void
intrenableall(void)
{
	int irq;
	static int first = 1;

	/* first time, turn them all on and see what interrupts */
	if (first) {
		first = 0;
		iprint("enabling all global interrupts...");
		printenables = 0;
		for (irq = Firstirq; irq < Ngintr; irq++)
			plicenable(irq);
		printenables = 1;
		iprint("\n");
	}
}

/* old 9k interface */
void
intrenable(int irq, void (*f)(Ureg*, void*), void* a, int tbdf, char *name)
{
	int vno;
	Vctl *v;

	if(f == nil){
		print("intrenable: nil handler for %d, tbdf %#ux for %s\n",
			irq, tbdf, name);
	}
	if (irq >= Ngintr) {
		print("intrenable: irq %d >= Ngintr (%d) for %s\n",
			irq, Ngintr, name);
	}

	vno = vctlidx(irq, Globalintr);
	if(vno == -1){
		print("intrenable: couldn't enable irq %d, tbdf %#ux for %s\n",
			irq, tbdf, name);
	}
	if((uint)vno >= nelem(vctl))
		panic("intrenable: vector %d out of range", vno);
	v = newvec(f, a, tbdf, name);	/* alloc before lock in case of sleep */
	ilock(&vctllock);
	if(vctl[vno])
		panic("intrenable: vector %d for %s already allocated by %s",
			vno, name, vctl[vno]->name);
	plicenable(irq);
	if (soc.allintrs)
		intrenableall();

	/* we assert that vectors are unshared, though irqs may be */
	v->isintr = 1;
	v->irq = irq;
	v->vno = vno;
	v->type = Globalintr;
	vctl[vno] = v;
	v->pollnxt = pollvecs;		/* add to poll chain */
	pollvecs = v;
	iunlock(&vctllock);
	if ((Trapdebug || Intrdebug) && printenables)
		iprint("intrenable %s irq %d vector %d for cpu%d\n",
			name, irq, vno, m->machno);

}

void
intrdisable(int, void (*f)(Ureg*, void*), void *a, int tbdf, char*)
{
	USED(f);
	USED(a);
	USED(tbdf);
	panic("intrdisable");
#ifdef xxx
	Vctl *v;

	ilock(&vctllock);
	v = vector;
	if(v == nil || vctl[v->vno] != v)
		panic("intrdisable: v %#p", v);
	plicdisable(v->vno);
	vctl[v->vno] = nil;
	iunlock(&vctllock);
//	free(v);		/* can't free while poll chain exists */
	return 0;
#endif
}
