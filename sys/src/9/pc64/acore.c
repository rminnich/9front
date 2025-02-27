#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/pci.h"
#include	"ureg.h"

#define DBG iprint
/*
 * NIX code run at the AC.
 * This is the "AC kernel".
 */

/*
 * FPU:
 *
 * The TC handles the FPU by keeping track of the state for the
 * current process. If it has been used and must be saved, it is saved, etc.
 * When a process gets to the AC, we handle the FPU directly, and save its
 * state before going back to the TC (or the TC state would be stale).
 *
 * Because of this, each time the process comes back to the AC and
 * uses the FPU it will get a device not available trap and
 * the state will be restored. This could be optimized because the AC
 * is single-process, and we do not have to disable the FPU while
 * saving, so it does not have to be restored.
 */

extern char* acfpunm(Ureg* ureg, void*);
extern char* acfpumf(Ureg* ureg, void*);
extern char* acfpuxf(Ureg* ureg, void*);
extern void acfpusysprocsetup(Proc*);

extern void _acsysret(void);
extern void _actrapret(void);

ACVctl *acvctl[256];

/* 
 * Test inter core calls by calling a cores to print something, and then
 * waiting for it to complete.
 */
static void
testiccfn(void)
{
	iprint("called: %s\n", (char*)m->icc->data);
}

void
testicc(int i)
{
	Mach *mp;

	if((mp = machp[i]) != nil && mp->online != 0){
		if(mp->nixtype != NIXAC){
			iprint("testicc: core %d is not an AC\n", i);
			return;
		}
		iprint("calling core %d... ", i);
		mp->icc->flushtlb = 0;
		snprint((char*)mp->icc->data, ICCLNSZ, "<%d>", i);
		mfence();
		iprint("Set testicc to %p\n", testiccfn);
		mp->icc->fn = testiccfn;
		iprint("wait ...\n");
		// mwait is allowed to return with nothing changed.
		while(mp->icc->fn == testiccfn)
			mwait(&mp->icc->fn);
		iprint("done\n");
	}
}

/*
 * Check if the AC kernel (mach) stack has more than 4*KiB free.
 * Do not call panic, the stack is gigantic.
 */
static void
acstackok(void)
{
	char dummy;
	char *sstart;

	sstart = (char *)m - BY2PG - 4*PTSZ - MACHSIZE; // is that the same? MACHSTKSZ;
	if(&dummy < sstart + 4*KiB){
		iprint("ac kernel stack overflow, cpu%d stopped\n", m->machno);
		DONE();
	}
}

void
acmmuswitch(void)
{
	extern Page mach0pml4;

	DBG("acmmuswitch mpl4 %#p mach0pml4 %#p m0pml4 %#p\n", PADDR(m->pml4), m->pml4, PADDR(machp[0]->pml4));


	putcr3(PADDR(m->pml4));
}

/*
 * Main scheduling loop done by the application core.
 * Some of functions run will not return.
 * The system call handler will reset the stack and
 * call acsched again.
 * We loop because some functions may return and we should
 * wait for another call.
 */
void
acsched(void)
{
	acmmuswitch();
	for(;;){
		if (0)acstackok();
		iprint("acstackok is ok\n");
		iprint("&m->icc->fn %p fn %p\n", &m->icc->fn, m->icc->fn);
		while(m->icc->fn == nil)
			mwait(&m->icc->fn);
		iprint("m %p m->icc->flushtlb %d m->icc->fn %p\n", m, m->icc->flushtlb, m->icc->fn);
		if(m->icc->flushtlb)
			acmmuswitch();
		iprint("acsched: cpu%d: fn %#p\n", m->machno, m->icc->fn);
		m->icc->fn();
		iprint("acsched: cpu%d: idle\n", m->machno);
		mfence();
		m->icc->fn = nil;
	}
}

/*
 * Beware: up is not set when this function is called.
 */
void
actouser(void)
{
	void xactouser(u64int);
	Ureg *u;

	acfpusysprocsetup(m->proc);

	u = m->proc->dbgreg;
	iprint("  AX %.16lluX  BX %.16lluX  CX %.16lluX\n",
		u->ax, u->bx, u->cx);
	iprint("  DX %.16lluX  SI %.16lluX  DI %.16lluX\n",
		u->dx, u->si, u->di);
	iprint("  BP %.16lluX  R8 %.16lluX  R9 %.16lluX\n",
		u->bp, u->r8, u->r9);
	iprint(" R10 %.16lluX R11 %.16lluX R12 %.16lluX\n",
		u->r10, u->r11, u->r12);
	iprint(" R13 %.16lluX R14 %.16lluX R15 %.16lluX\n",
		u->r13, u->r14, u->r15);
	iprint("  CS %.4lluX   SS %.4lluX    PC %.16lluX  SP %.16lluX\n",
		u->cs & 0xffff, u->ss & 0xffff, u->pc, u->sp);
	iprint("TYPE %.2lluX  ERROR %.4lluX FLAGS %.8lluX\n",
		u->type & 0xff, u->error & 0xffff, u->flags & 0xffffffff);

	/*
	 * Processor control registers.
	 * If machine check exception, time stamp counter, page size extensions
	 * or enhanced virtual 8086 mode extensions are supported, there is a
	 * CR4. If there is a CR4 and machine check extensions, read the machine
	 * check address and machine check type registers if RDMSR supported.
	 */
	iprint(" CR0 %8.8llux CR2 %16.16llux CR3 %16.16llux",
		getcr0(), getcr2(), getcr3());
	if(m->cpuiddx & (Mce|Tsc|Pse|Vmex)){
		iprint(" CR4 %16.16llux\n", getcr4());
		if(u->type == 18)
			dumpmcregs();
	}
	iprint("  ur %#p up %#p\n", u, up);
	iprint("cpu%d: touser m %p m->proc %p m->stack %p\n", m->machno, m, m->proc, m->stack);
	iprint("cpu%d: touser usp = %#p entry %#p\n", m->machno, u->sp, u->pc);
	//while(1);
	xactouser(u->sp);
	panic("actouser RETURNED, can't happen");
}

void
actrapret(void)
{
	/* done by actrap() */
}

/*
 * Entered in AP core context, upon traps (system calls go through acsyscall)
 * using up->dbgreg means cores MUST be homogeneous.
 *
 * BUG: We should setup some trapenable() mechanism for the AC,
 * so that code like fpu.c could arrange for handlers specific for
 * the AC, instead of doint that by hand here.
 * 
 * All interrupts are masked while in the "kernel"
 */
void
actrap(Ureg *u)
{
	char *n;
	ACVctl *v;

	n = nil;

	////_pmcupdate(m);
	if(m->proc != nil){
		m->proc->nactrap++;
		m->proc->actime1 = fastticks(nil);
	}
	/* there are a few traps we handle quickly, in particular
	 * API timer interrupts and such. */
	print("ACTRAP: %ld\n", u->type);
	if(u->type < nelem(acvctl)){
		v = acvctl[u->type];
		if(v != nil){
			DBG("actrap: cpu%d: %ulld\n", m->machno, u->type);
			n = v->f(u, v->a);
			if(n != nil)
				goto Post;
			return;
		}
	}
	switch(u->type){
	case Vector2F:
		iprint("AC: double fault\n");
		dumpregs(u);
		ndnr();
	case VectorIPI:
		m->intr++;
		DBG("actrap: cpu%d: IPI\n", m->machno);
		panic("apiceoi(VectorIPI);");
		break;
	case IrqTIMER:
		panic("timer interrupt in an AC");
		break;
	case VectorPF:
		/* this case is here for debug only */
		m->pfault++;
		DBG("actrap: cpu%d: PF cr2 %#ullx\n", m->machno, getcr2());
		break;
	default:
		iprint("actrap: cpu%d: %ulld\n", m->machno, u->type);
	}
Post:
	m->icc->rc = ICCTRAP;
	m->cr2 = getcr2();
	memmove(m->proc->dbgreg, u, sizeof *u);
	m->icc->note = n;
	fpuprocsave(m->proc);
	/*//_pmcupdate(m);*/
	mfence();
	m->icc->fn = nil;
	ready(m->proc);

	while (m->icc->fn == nil)
		mwait(&m->icc->fn);

	if(m->icc->flushtlb)
		acmmuswitch();
	if(m->icc->fn != actrapret)
		acsched();
	DBG("actrap: ret\n");
	memmove(u, m->proc->dbgreg, sizeof *u);
	if(m->proc)
		m->proc->actime += fastticks2us(fastticks(nil) - m->proc->actime1);
}

static uintptr sp;
void
acsyscall(Ureg *ureg)
{
	Proc *p;
	/*
	 * If we saved the Ureg into m->proc->dbgregs,
	 * There's nothing else we have to do.
	 * Otherwise, we should m->proc->dbgregs = u;
	 */

	//_pmcupdate(m);
	p = m->proc;
	sp = ureg->sp;
	DBG("acsyscall: cpu%d, pc %p, sp %p savesp %p\n", m->machno, ureg->pc, ureg->sp, sp);
	p->actime1 = fastticks(nil);
	m->syscall++;	/* would also count it in the TS core */
	m->icc->rc = ICCSYSCALL;
	m->cr2 = getcr2();
	fpuprocsave(p);
	//_pmcupdate(m);
	mfence();
	m->icc->fn = nil;
	DBG("acsyscall: m is %p, m->proc is %p\n", m, m->proc);
	ready(p);
	DBG("back from ready, now call sched\n");
	/*
	 * The next call is probably going to make us jmp
	 * into user code, forgetting all our state in this
	 * stack, upon the next syscall.
	 * We don't nest calls in the current stack for too long.
	 */
	acsched();
}

/*
 * Called in AP core context, to return from system call.
 */
void
acsysret(void)
{
	Ureg *u = m->proc->dbgreg;
	fpukexit(u, m->proc->fpsave);
	iprint("  AX %.16lluX  BX %.16lluX  CX %.16lluX\n",
		u->ax, u->bx, u->cx);
	iprint("  DX %.16lluX  SI %.16lluX  DI %.16lluX\n",
		u->dx, u->si, u->di);
	iprint("  BP %.16lluX  R8 %.16lluX  R9 %.16lluX\n",
		u->bp, u->r8, u->r9);
	iprint(" R10 %.16lluX R11 %.16lluX R12 %.16lluX\n",
		u->r10, u->r11, u->r12);
	iprint(" R13 %.16lluX R14 %.16lluX R15 %.16lluX\n",
		u->r13, u->r14, u->r15);
	iprint("  CS %.4lluX   SS %.4lluX    PC %.16lluX  SP %.16lluX\n",
		u->cs & 0xffff, u->ss & 0xffff, u->pc, u->sp);
	iprint("TYPE %.2lluX  ERROR %.4lluX FLAGS %.8lluX\n",
		u->type & 0xff, u->error & 0xffff, u->flags & 0xffffffff);

	/*
	 * Processor control registers.
	 * If machine check exception, time stamp counter, page size extensions
	 * or enhanced virtual 8086 mode extensions are supported, there is a
	 * CR4. If there is a CR4 and machine check extensions, read the machine
	 * check address and machine check type registers if RDMSR supported.
	 */
	iprint(" CR0 %8.8llux CR2 %16.16llux CR3 %16.16llux",
		getcr0(), getcr2(), getcr3());
	if(m->cpuiddx & (Mce|Tsc|Pse|Vmex)){
		iprint(" CR4 %16.16llux\n", getcr4());
		if(u->type == 18)
			dumpmcregs();
	}
	iprint("  ur %#p up %#p\n", u, up);
	DBG("acsysret m %p m->machno %d m->proc %p u->sp %p sp %p\n", m, m->machno, m->proc, u->sp, sp);
	if (sp != u->sp)
		DBG("THIS CAN NOT HAPPEN: SP %p != u->sp %p", sp, u->sp);
	if(m->proc != nil)
		m->proc->actime += fastticks2us(fastticks(nil) - m->proc->actime1);
	DBG("cpu%d:acsysret: pc %p, sp %p savesp %p\n", m->machno, u->pc, u->sp, sp);
	DBG("Call _acsysret\n");
	_acsysret();
}

void
dumpreg(void *u)
{
	iprint("reg is %p\n", u);
	ndnr();
}

char *rolename[] = 
{
	[NIXAC]	"AC",
	[NIXTC]	"TC",
	[NIXKC]	"KC",
	[NIXXC]	"XC",
};

void
acmodeset(int mode)
{
	switch(mode){
	case NIXAC:
	case NIXKC:
	case NIXTC:
	case NIXXC:
		break;
	default:
		panic("acmodeset: bad mode %d", mode);
	}
	m->nixtype = mode;
}

void
acinit(void)
{
#ifdef x
	Mach *mp;
	Proc *pp;

	Fix these for 9front, maybe.
	/*
	 * Be sure a few  assembler assumptions still hold.
	 * Someone moved m->stack and I had fun debugging...
	 */
	mp = 0;
	pp = 0;
	assert((uintptr)&mp->proc == 16);
	assert((uintptr)&pp->dbgreg == 24);
	assert((uintptr)&mp->stack == 24);
#endif
	void lapicintron(void);
	/*
	 * Lower the priority of the apic to 0,
	 * to accept interrupts.
	 * Raise it later if needed to disable them.
	 */
	lapicintron();
}

/* The old classic setup in plan 9 differs from 9front. Still trying to work it out. */
void
acfpusysprocsetup(Proc *p)
{
	if (p == nil)
		panic("p is nil");
	if (p->dbgreg == nil)
		panic("m %p p %p dbgreg is nil", m, p);
	fpukexit(p->dbgreg, p->fpsave);
	return;
	extern void _clts(void);
	if(p->fpstate == FPinit){
		/* The FPU is initialized in the TC but we must initialize
		 * it in the AC.
		 */
		/* follow, roughly, what we do in a fork and exec. That's what NIX did */
		p->fpstate = FPinactive;
		//p->fpstate = FPactive;
		//fpuprocrestore(p);
	}
}
/* debug -- put them here, not in main.c */
void
DONE(void)
{
	iprint("DONE\n");
	prflush();
	delay(10000);
	ndnr();
}

void
HERE(void)
{
	iprint("here\n");
	prflush();
	delay(5000);
}

void ndnr(void)
{
	while (1)
		;
}

/* do we need this? */
void
fpusysrfork(Ureg*)
{
	void fpuprocfork(Proc *p);
	fpuprocsave(up);
	up->fpstate = FPinit;
}

