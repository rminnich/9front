#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/pci.h"
#include	"ureg.h"

#define DBG if(0)print

Lock nixaclock;	/* NIX AC lock; held while assigning procs to cores */

/*
 * NIX support for the time sharing core.
 */

extern void actrapret(void);
extern void acsysret(void);

Mach*
getac(Proc *p, int core)
{
	int i;
	Mach *mp;

	if(core == 0)
		panic("can't getac for a %s", rolename[NIXTC]);
	lock(&nixaclock);
	if(waserror()){
		unlock(&nixaclock);
		nexterror();
	}
	if(core > 0){
		if(core >= MAXMACH)
			error("no such core");
		mp = machp[core];
		if(mp == nil || mp->online == 0 || mp->proc != nil)
			error("core not online or busy");
		if(mp->nixtype != NIXAC)
			error("core is not an AC");
	Found:
		mp->proc = p;
	}else{
		for(i = 0; i < MAXMACH; i++)
			if((mp = machp[i]) != nil && mp->online && mp->nixtype == NIXAC)
				if(mp->proc == nil)
					goto Found;
		error("not enough cores");
	}
	unlock(&nixaclock);
	poperror();
	return mp;
}

/*
 * BUG:
 * The AC must not accept interrupts while in the kernel,
 * or we must be prepared for nesting them, which we are not.
 * This is important for note handling, because postnote()
 * assumes that it's ok to send an IPI to an AC, no matter its
 * state. The /proc interface also assumes that.
 *
 */
void
intrac(Proc *p)
{
	Mach *ac;

	ac = p->ac;
	if(ac == nil){
		DBG("intrac: Proc.ac is nil. no ipi sent.\n");
		return;
	}
	/*
	 * It's ok if the AC gets idle in the mean time.
	 */
	DBG("intrac: ipi to cpu%d\n", ac->machno);
	apicipi(ac->apicno);
}

void
putac(Mach *m)
{
	mfence();
	m->proc = nil;
}

void
stopac(void)
{
	Mach *mp;

	mp = up->ac;
	if(mp == nil)
		return;
	if(mp->proc != up)
		panic("stopac: mp->proc %p != up %p", mp->proc, up);

	lock(&nixaclock);
	up->ac = nil;
	mp->proc = nil;
	unlock(&nixaclock);

	/* TODO:
	 * send sipi to up->ac, it would rerun squidboy(), and
	 * wait for us to give it a function to run.
	 */
}

/*
 * Functions starting with ac... are run in the application core.
 * All other functions are run by the time-sharing cores.
 */

typedef void (*APfunc)(void);
extern int notify(Ureg*);

/*
 * run an arbitrary function with arbitrary args on an ap core
 * first argument is always pml4 for process
 * make a field and a struct for the args cache line.
 *
 * Returns the return-code for the ICC or -1 if the process was
 * interrupted while issuing the ICC.
 */
int
runac(Mach *mp, APfunc func, int flushtlb, void *a, long n)
{
	u64int *dpg, *spg;

	if (n > sizeof(mp->icc->data))
		panic("runac: args %ld: > sizeof mp->icc->data %p", n, mp->icc->data);

	if(mp->online == 0)
		panic("mp %d not online; Bad core", m->machno);
	if(mp->proc != nil && mp->proc != up)
		panic("runapfunc: mach %d is busy with up %p, not proc %p", mp->machno, mp->proc, up);

	memmove(mp->icc->data, a, n);
	// This is called "flushtlb"
	// but it really means: flush the tlb on the ac.
	// Going beyond that, it means we are changing pml4 on the AC.
	// mp is the mach for the process on the mach.
	// m is our mach pml4, which is by definition for this process, since
	// this process is the one running this code.
	if(flushtlb){
		DBG("runac flushtlb: cp pml4 PA %#p %#p\n", PADDR(mp->pml4), PADDR(m->pml4));
		dpg = mp->pml4;
		spg = m->pml4;
		/* We should copy less:
		 *	memmove(dgp, spg, m->pml4->daddr * sizeof(PTE));
		 */
		memmove(dpg, spg, PTSZ);
		if(0){
			print("runac: upac pml4 %#p\n", up->ac->pml4);
			dumpptepg(4, PADDR(up->ac->pml4));
		}
	}

	mp->icc->flushtlb = flushtlb;
	mp->icc->rc = ICCOK;

	DBG("runac: exotic proc on cpu%d\n", mp->machno);
	if(waserror()){
		qunlock(&up->debug);
		nexterror();
	}
	qlock(&up->debug);
	up->nicc++;
	/* How does sched know to run this process again?
	 * We set the state to Exotic here. As long as the state
	 * is Exotic, sched will not run the process.
	 * When the process is ready to run,
	 * code in acore.c calls ready(p),
	 * which sets the state to Ready, and we will return
	 * from the call to sched(). */
	up->state = Exotic;
	up->psstate = 0;
	qunlock(&up->debug);
	poperror();
	mfence();
	DBG("cpu%d waits in sched mach is %p\n", m->machno, m);
	mp->icc->fn = func;
	sched();
	DBG("cpu%d returns from waiting for AC, m is %p\n", m->machno, m);
	return mp->icc->rc;
}

/*
 * Cleanup done by runacore to pretend we are going back to user space.
 * We won't return and won't do what syscall() would normally do.
 * Do it here instead.
 */
static void
fakeretfromsyscall(Ureg *ureg)
{
	int s;

	poperror();	/* as syscall() would do if we would return */
	if(up->procctl == Proc_tracesyscall){	/* Would this work? */
		up->procctl = Proc_stopme;
		s = splhi();
		_procctl(up);
		splx(s);
	}

	up->insyscall = 0;
	/* if we delayed sched because we held a lock, sched now */
	if(up->delaysched){
		sched();
		splhi();
	}
	kexit(ureg);
}

static void
tabs(int i)
{
	while(i--)
		print("\t");
}

void
dumpptepg(int lvl, u64int pa)
{
/* surely there is one of these in 9front?*/
	PTE *pte = KADDR(pa);
	int tab, i;

	tab = 4 - lvl;
	for(i = 0; i < PTSZ/sizeof(PTE); i++)
		if(pte[i] & PTEVALID){
			tabs(tab);
			print("l%d %#p[%#05x]: %#ullx\n", lvl, pa, i, pte[i]);

			/* skip kernel mappings */
			if((pte[i]&PTEUSER) == 0){
				tabs(tab+1);
				print("...kern...\n");
				continue;
			}
			if (! cankaddr(pte[i])){
				// WTF.
				print("Not addressable? WTF\n");
				continue;
			}
			if(lvl > 2)
				dumpptepg(lvl-1, PPN(pte[i]));
		}
}

static void
tcfaultnote(Mach *m, Ureg *ureg, char *access, uintptr addr)
{
	extern void checkpages(void);
	char buf[ERRMAX];

	if(!userureg(ureg)){
		dumpregs(ureg);
		panic("cpu%d:fault: %s addr=%#p", m->machno, access, addr);
	}
	checkpages();
	snprint(buf, sizeof(buf), "sys: trap: fault %s addr=%#p", access, addr);
	postnote(up, 1, buf, NDebug);
}

static void
tcfaultamd64(Mach *m, Ureg* ureg, void*)
{
	uintptr addr;
	int read, user;

	addr = getcr2(); //m->cr2;
	read = !(ureg->error & 2);
	user = userureg(ureg);
	if(user)
		up->insyscall = 1;
	else {
		extern void _peekinst(void);

		if((void(*)(void))ureg->pc == _peekinst){
			ureg->pc += 2;
			return;
		}
		if(addr >= USTKTOP)
			panic("kernel fault: bad address pc=%#p addr=%#p", ureg->pc, addr);
		if(up == nil)
			panic("kernel fault: no user process pc=%#p addr=%#p", ureg->pc, addr);
		if(waserror()){
			if(up->nerrlab == 0){
				pprint("suicide: sys: %s\n", up->errstr);
				pexit(up->errstr, 1);
			}
			nexterror();
		}
	}

	if(fault(addr, ureg->pc, read))
		tcfaultnote(m, ureg, read? "read": "write", addr);

	if(user)
		up->insyscall = 0;
	else
		poperror();
}


/*
 * Move the current process to an application core.
 * This is performed at the end of execac(), and
 * we pretend to be returning to user-space, but instead we
 * dispatch the process to another core.
 * 1. We do the final bookkeeping that syscall() would do after
 *    a return from sysexec(), because we are not returning.
 * 2. We dispatch the process to an AC using an ICC.
 *
 * This function won't return unless the process is reclaimed back
 * to the time-sharing core, and is the handler for the process
 * to deal with traps and system calls until the process dies.
 *
 * Remember that this function is the "line" between user and kernel
 * space, it's not expected to raise|handle any error.
 *
 * We install a safety error label, just in case we raise errors,
 * which we shouldn't. (noerrorsleft knows that for exotic processes
 * there is an error label pushed by us).
 */
void
runacore(void)
{
	Ureg *ureg;
	void (*fn)(void);
	int rc, flush, s;
	char *n;
	uvlong t1;
	void syscall(Ureg*);
	void trap(Ureg *ureg);

	if(waserror())
		panic("runacore: error: %s\n", up->errstr);
	ureg = up->dbgreg;
	fakeretfromsyscall(ureg);
	fpusysrfork(ureg);

	procpriority(up, PriKproc, 1);
	rc = runac(up->ac, actouser, 1, nil, 0);
	procpriority(up, PriNormal, 0);
	for(;;){
		t1 = fastticks(nil);
		flush = 0;
		fn = nil;
		switch(rc){
		case ICCTRAP:
			s = splhi();
			m->cr2 = up->ac->cr2;
			DBG("runacore: trap %ulld cr2 %#ullx ureg %#p\n",
				ureg->type, m->cr2, ureg);
			switch(ureg->type){
			case VectorIPI:
				if(up->procctl || up->nnote)
					notify(up->dbgreg);
				if(up->ac == nil)
					goto ToTC;
				kexit(up->dbgreg);
				break;
			case VectorNMI:
			case VectorCERR:
			case VectorSIMD:
				/* these are handled in the AC;
				 * If we get here, they left in m->icc->data
				 * a note to be posted to the process.
				 * Post it, and make the vector a NOP.
				 */
				n = up->ac->icc->note;
				if(n != nil)
					postnote(up, 1, n, NDebug);
				ureg->type = VectorIPI;		/* NOP */
				break;
			default:
				if(!userureg(ureg))
					DBG("runacore: Not a user space process?\n\n\n\n");
				putcr3(PADDR(m->pml4));
				putcr2(up->ac->cr2);
				if(ureg->type == VectorPF){
					if (0){
						dumpptepg(4, PADDR(up->ac->pml4));
						print("\n%s:\n", rolename[NIXTC]);
						dumpptepg(4, PADDR(m->pml4));
						dumpregs(ureg);
					}
					tcfaultamd64(m, ureg, nil);
				} else {
					trap(ureg);
				}
			}
			splx(s);
			flush = 1;
			fn = actrapret;
			break;
		case ICCSYSCALL:
			DBG("cpu%d:runacore: syscall bp %#ullx ureg %#p\n", m->machno, ureg->bp, ureg);
			putcr3(PADDR(m->pml4));
			if(0){
				up->s = *((Sargs*)((uintptr)ureg->sp+BY2WD));
				syscallfmt(ureg->bp, ureg->pc, (va_list)up->s.args);
				print("syscall: %s\n", up->syscalltrace);
			}
			// up->printsyscall = 1;
			syscall(ureg);
			flush = 1;
			fn = acsysret;
			if(0)
			if(up->nqtrap > 2 || up->nsyscall > 1)
				goto ToTC;
			if(up->ac == nil){
				print("up->ac is now nil; going ToTC\n");
				goto ToTC;
			}
			break;
		default:
			panic("runacore: unexpected rc = %d", rc);
		}
		up->tctime += fastticks2us(fastticks(nil) - t1);
		procpriority(up, PriExtra, 1);
		rc = runac(up->ac, fn, flush, nil, 0);
		procpriority(up, PriNormal, 0);
	}
ToTC:
	/*
	 *  to procctl, then syscall,  to
	 *  be back in the TC
	 */
	DBG("runacore: up %#p: return\n", up);
}

extern ACVctl *acvctl[];

void
actrapenable(int vno, void (*f)(Ureg*, void*), void* a, char *name)
{
	ACVctl *v;

	DBG("Enabled trap %s on AC\n", name);
	if(vno < 0 || vno >= 256)
		panic("actrapenable: vno %d\n", vno);
	v = malloc(sizeof(Vctl));
	v->f = f;
	v->a = a;
	v->vno = vno;
	strncpy(v->name, name, KNAMELEN);
	v->name[KNAMELEN-1] = 0;

	if(acvctl[vno])
		panic("vector %d: AC traps can't be shared", vno);
	acvctl[vno] = v;
}

