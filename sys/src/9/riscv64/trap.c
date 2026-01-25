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
#include "/sys/src/libc/9syscall/sys.h"

/* instruction decoding */
#define UNCOMPINST(pc)	(*(ushort *)(pc) | *(ushort *)((pc) + 2) << 16)
#define BASEOP(inst)	((inst) & MASK(7))

enum {
	Trapdebug	= 1,
	Probedebug	= 0,
	Intrdebug	= 0,
	Tryallcpus	= 0,
	TrapSpew	= 0,

	Ntimevec = 20,		/* number of time buckets for each intr */
	Ncauses = Ngintr + Nlintr + Nexc,	/* # of Vctls */
};
enum Faulttypes {
	Unknownflt, Exception, Localintr, Globalintr, Nfaulttypes,
};

void
setupwatchpts(Proc*, Watchpt*, int)
{
}

void
dumpstack(void) 
{
	// TODO
}

void
setregisters(Ureg* ureg, char* pureg, char* uva, int n)
{
	ulong v;

	v = ureg->ie;
	memmove(pureg, uva, n);
	ureg->ie = v;
}

uintptr
userpc(void)
{
	Ureg *ur = up->dbgreg;
	return ur->pc;
}

uintptr
dbgpc(Proc *)
{
	Ureg *ur = up->dbgreg;
	if(ur == nil)
		return 0;
	return ur->pc;
}

void
procfork(Proc *p)
{
	print("procrfork %p\n", p);
	panic("procfork");
//	fpuprocfork(p);
//	p->tpidr = up->tpidr;
}

void
procsetup(Proc *p)
{
	print("procsetup %p\n", p);
//	fpuprocsetup(p);
}

void
procsave(Proc *p)
{
	print("procsave what is it for? %p\n", p);
//	fpuprocsave(p);
//	if(p->kp == 0)
//		p->tpidr = sysrd(TPIDR_EL0);
//	putasid(p);	// release asid
}

void
procrestore(Proc *p)
{
	print("procresotre what is it for %p\n", p);
//	fpuprocrestore(p);
//	if(p->kp == 0)
//		syswr(TPIDR_EL0, p->tpidr);
}

void
setkernur(Ureg *ureg, Proc *p)
{
	ureg->pc = p->sched.pc;
	ureg->sp = p->sched.sp;
	ureg->link = (uintptr)sched;
}

int
userureg(Ureg* ureg)
{
	switch (ureg->curmode) {
	default:
		return (ureg->status & Spp) == 0;
	case Mppmach:
		return (ureg->status & Mpp) == Mppuser;
	}
}

void
callwithureg(void (*f) (Ureg *))
{
	print("callwithureg %p\n", f);
	Ureg u;
	
	u.pc = getcallerpc(&f);
	u.sp = (uintptr) &f;
	f(&u);
}

void
kprocchild(Proc *p, void (*entry)(void))
{
	print("kprocchild %p %p\n", p, entry);
//	panic("kprocchild");
	p->sched.pc = (uintptr) entry;
	p->sched.sp = (uintptr) p - 16;
	*(void**)p->sched.sp = kprocchild;	/* fake */
}

void
evenaddr(uintptr addr)
{
	if(addr & 2){
		postnote(up, 1, "sys: odd word address", NDebug);
		error(Ebadarg);
	}
	if(addr & 1){
		postnote(up, 1, "sys: odd byte address", NDebug);
		error(Ebadarg);
	}
}

void
forkchild(Proc *p, Ureg *ureg)
{
	print("forkchil %p %p\n", p, ureg);
	Ureg *cureg;

	p->sched.pc = (uintptr) forkret;
	p->sched.sp = (uintptr) p - TRAPFRAMESIZE;

	cureg = (Ureg*) (p->sched.sp + 16);
	memmove(cureg, ureg, sizeof(Ureg));
	cureg->arg = 0;
}

uintptr
execregs(uintptr entry, int argc, char *argv[], Tos *tos)
{
	print("execregs %p %d %p %p\n", entry, argc, argv, tos);
panic("execregs");
#ifdef xxx
	uintptr *sp;
	Ureg *ureg;

	sp = (uintptr*)(USTKTOP - 64); // Actually sizeof (TOS) I guess.
	*--sp = argc;

	ureg = up->dbgreg;
	ureg->sp = (uintptr)sp;
	ureg->pc = entry;
	ureg->link = 0;
	return USTKTOP-sizeof(Tos);
#endif
	return 0;
}

static void panictrap(Ureg *ureg)
{
	USED(ureg);
	sbiputc('&');
	print("trap");
	panic("trap");
}

// Geoff
typedef struct {
	/* exception or interrupt code from [ms]cause, without Rv64intr bit */
	uchar	cause;
	uchar	user;	/* flag: fault occurred while in user mode */
	uchar	type;	/* Faulttypes code */
	short	vno;	/* vector number; see vctlidx */
} Cause;
typedef int (*Traphandler)(Ureg *, Cause *);
typedef void (*Exchandler)(Ureg *, Cause *);

static char* excname[] = {
	"instruction address alignment",
	"instruction access",
	"illegal instruction",
	"breakpoint",
	"load address alignment",
	"load access",
	"store address alignment",
	"store access",
	"system call",
	"environment call from super mode",
	"#10 (reserved)",
	"environment call from machine mode",
	"instruction page fault",
	"load page fault",
	"#14 (reserved)",
	"store page fault",
	/* hypervisor stuff from here down */
/*
[Doubletrap]	"double trap",
[Swchk]		"software check",
[Hwerr]		"hardware error",
[Instgpage]	"instruction guest-page fault",
[Loadgpage]	"load guest-page fault",
[Virtinstr]	"virtual instruction",
[Storegpage]	"store guest-page fault",
*/
};

static void
trapdbg(Ureg *ureg, Cause *cp, int entry)
{
	extern int block;
	int type;

	type = cp->type;
	/* if we print uart interrupts, we'll recurse forever. */
//	if (type == Globalintr)
//		return;
/*
	iprint("|%c%c%c ", ureg->curmode == Mppsuper? 'S': 'M', entry? '>': '<',
		type == Exception? 'E': 'I');
*/
	if (cp->cause >= nelem(excname) || excname[cp->cause] == nil)
		iprint("cause %d", cp->cause);
	else {
		iprint("%s", excname[cp->cause]);
		if (cp->cause == Envcalluser) {
			iprint(" pid %d", up->pid);
			if (entry)
				iprint(" %d", ureg->arg);
			else
				iprint(" return %#llux", ureg->arg);
		}
	}
	iprint(" from %s pc %#p tval %#p up %#p\n",
		cp->user? "user": "kernel", ureg->pc, ureg->tval, up);
}

static void
posttrapnote(Ureg *ureg, uint cause, char *name)
{
	char buf[ERRMAX];

	spllo();
	snprint(buf, sizeof buf, "sys: trap: %s for address %#p",
		cause < nelem(excname) && excname[cause]? excname[cause]: name,
		ureg->tval);
	postnote(up, 1, buf, NDebug);
}


/*
 * on riscv, we have to manually advance the PC past an ECALL instruction,
 * for example, so that we don't re-execute it.
 */
static void
advancepc(Ureg *ureg)
{
	/* examine short at PC, which is sufficient to decide if compressed */
	if ((ureg->pc & 1) == 0)
		ureg->pc += ISCOMPRESSED(*(ushort *)ureg->pc)? 2: 4;
}

static void
trapsyscall(Ureg *ureg, Cause *)
{
	uint scallnr;
	uintptr pc;

	int i;
	for(i = 0; i < 32; i++) print("%d:0x%llx\n", i, ureg->regs[i]);
//	m->turnedfpoff = 0;

	/* syscall may change ureg->pc, so save a copy. */
	pc = ureg->pc;
/*	if (getsp() % sizeof(vlong) != 0)
		print("trapsyscall: odd sp %#p at %#p\n", getsp(), pc);*/
	scallnr = ureg->arg;
	/* on riscv64, ureg->ret is ureg->arg, so can't zero ureg->ret here. */
	/* Last syscall argument is location of return value in frame. */
	if (TrapSpew) print("dosyscall(%d, %p, %p val %p\n", scallnr, (Sargs*)(ureg->sp+BY2WD), &ureg->arg, ureg->arg);
	dosyscall(scallnr, (Sargs*)(ureg->sp+BY2WD), &ureg->arg);

	/*
	 * a changed pc could be the result of receiving a note, but also
	 * successful exec does not return here, but at the entry
	 * point of the new program.
	 */
	if (pc == ureg->pc)
		advancepc(ureg);
	else if (Trapdebug && scallnr != EXEC)
		iprint("syscall %d changed return ureg->pc %#p (old pc %#p)\n",
			scallnr, ureg->pc, pc);

}

static void
trapcallmch(Ureg *, Cause *)
{
	panic("unexpected environment call from machine mode");
}

static void
trapmisaligned(Ureg *ureg, Cause *cp)
{
	if (cp->user)
		posttrapnote(ureg, cp->cause, "misaligned access");
	else
		panic("misaligned access to %#p at %#p", ureg->tval, ureg->pc);
}

static void
debugbpt(Ureg* , Cause*)
{
	char buf[ERRMAX];

	if(up == 0)
		panic("kernel bpt");
	/*
	 * on riscv, pc points at the instruction that caused the trap,
	 * so there's no need to back up the pc.
	 */
	snprint(buf, sizeof buf, "sys: breakpoint");
	postnote(up, 1, buf, NDebug);
}


/* decode trap cause from *ureg into *cp & return type */
static int
whatcause(Cause *cp, Ureg *ureg)
{
	int type, cause;

	cp->user = userureg(ureg);
	if(cp->user){
		up->dbgreg = ureg;
		cycles(&up->kentry);
	}

	/* ureg->cause has MCAUSE or SCAUSE CSR, as appropriate */
	cause = ureg->cause & ~Rv64intr;
	if (!(ureg->cause & Rv64intr))
		type = Exception;
	else if ((cause & ~Msdiff) == Supextintr) {
		type = Globalintr;
		cause = 0;	/* actual causes will come from the plic */
	} else
		type = Localintr;	/* these are very frequent */
	cp->type = type;
	cp->cause = cause;

#ifdef xxx
	cp->vno = vctlidx(cause, type);
	if (cp->vno < 0)
		panic("trap: cause %d or type %d out of range", cause, type);

	/* these counts are reset every second */
	if (Trapdebug && cp->vno &&
	    ++trapcnt[cp->vno] % (1024*1024) == 0)	/* tweak to taste */
		iprint("trap: vector %d trapping lots\n", cp->vno);
#endif
	if (TrapSpew) print("type %d\n", type);
	return type;
}

static void
trapaccess(Ureg *ureg, Cause *cp)
{
	if (cp->user)
		posttrapnote(ureg, cp->cause, "illegal access");
	else
		panic("trap: illegal access to %#p at %#p", ureg->tval,
			ureg->pc);
}

static void
badinst(Ureg *ureg, Cause *cp)
{
	if (cp->user)
		posttrapnote(ureg, cp->cause, "illegal instruction");
	else
		panic("illegal instruction at %#p: %#p", ureg->pc, ureg->tval);
}

/*
 * Illinst could be from legitimate user fp use while fpu is off.
 * Bad CSR accesses will be noted and ignored.
 * It could also be due to the C910 erroneously trapping an unknown fence,
 * such as fence.tso.
 */
static void
trapillinst(Ureg *ureg, Cause *cp)
{
	int rd, skip, funct3;
	ulong inst;
	uintptr pc;

	/* if non-zero, ureg->tval will be the trapping instruction */
	pc = ureg->pc;
	if (pc & 1) {
		if (!cp->user)
			panic("trapillinst: odd pc %#p", pc);
		ureg->tval = pc;
		posttrapnote(ureg, cp->cause, "odd pc");
		return;
	}

	print("IMPLEMENT ISFPINST\n");
#ifdef XXX
	if (isfpinst(pc, ureg)) {
		if (!cp->user)
			panic("kernel fpu use at %#p: %#p", pc, ureg->tval);
	//	fptrap(ureg, 0);
		vecacct(vctl[cp->vno]);
		return;			/* re-execute FP but with FPU on */
	}
#endif
	/*
	 * if we support vector extension, we'll need to cope with the
	 * vector instructions and vector processor here, analogously to fp.
	 * [ms]status.Vsst is vector context status.  ja and jl will have to
	 * implement the vector instructions first.
	 */

	if (ISCOMPRESSED(*(ushort *)pc)) {	/* instrs are little-endian */
		badinst(ureg, cp);
		return;
	}
	skip = 0;
	inst = UNCOMPINST(pc);
	if (inst != ureg->tval)
		iprint("illinst: inst %#lux tval %#llux\n", inst, ureg->tval);
	funct3 = (inst>>12) & MASK(3);
	switch (BASEOP(inst)) {
	case SYSTEM:
		if ((funct3 & MASK(2)) != 0) {
			/*
			 * bad CSR: zero dest and otherwise ignore.
			 * some CSRs are optional or obsolete.
			 * it could be a CSR that we want to emulate, in theory.
			 */
			iprint("CSR instruction for bad CSR %lx at %#p\n",
				inst>>20, pc);
			rd = (inst>>7) & MASK(5);
			if (rd)
				ureg->regs[rd] = 0;
		//	m->probebad = 1;
			skip = 1;
		}
		break;
	case 0xf:
		if (funct3 == 0)
			/*
			 * some fence; shouldn't happen, but c910 is buggy.
			 * coherence has been called.
			 */
			skip = 1;
		break;
	}
	if (skip)
		ureg->pc += 4;
	else
		badinst(ureg, cp);
}

/*
 * the page fault couldn't be resolved, probably because the address is
 * unmapped.  Report the error in the appropriate way.
 */
static void
badpagefault(Ureg *ureg, uintptr addr, int read, int insyscall)
{
	char buf[ERRMAX];

	/*
	 * It is possible to get here with !user if, for example,
	 * a process was in a system call accessing a shared
	 * segment but was preempted by another process which shrunk
	 * or deallocated the shared segment; when the original
	 * process resumes it may fault while in kernel mode.
	 * No need to panic this case, post a note to the process
	 * and unwind the error stack. There must be an error stack
	 * (up->nerrlab != 0) if this is a system call, if not then
	 * the game's a bogey.
	 */
	if(!userureg(ureg) && (!insyscall || up->nerrlab == 0)){
		dumpregs(ureg);
		panic("fault: addr %#p pc %#p", addr, ureg->pc);
	}
	snprint(buf, sizeof buf, "sys: trap: fault %s addr=%#p pc=%#p",
		read? "read": "write", addr, ureg->pc);
	postnote(up, 1, buf, NDebug);
	if(insyscall)
		error(buf);
}

/*
 *  find out fault address and type of access.
 *  Call common fault handler.
 */
static void
faultriscv64(Ureg* ureg, Cause *cp)
{
	uintptr addr;
	int read, insyscall;
	extern int block;

	/*
	 * There must be a user context.
	 * If not, the usual problem is causing a fault during
	 * initialisation before the system is fully up.
	 */
	addr = ureg->tval;
	if(up == nil)
		panic("fault %#lld with up == nil; pc %#p addr %#p",
			ureg->cause, ureg->pc, addr);
	if (addr == 0 && up->nlocks)		/* debugging for gs */
		panic("fault %#lld pc %#p addr %#p nlocks %d",
			ureg->cause, ureg->pc, addr, up->nlocks);

	//vecacct(vctl[cp->vno]);
	insyscall = up->insyscall;
	up->insyscall = 1;
	read = ureg->cause != Storepage;  /* exception, so Rv64intr must be 0 */
	/* page fault on a kernel address is never okay. */
	if (0)while (block != 1024);
	if((intptr)addr < 0 || fault(addr, ureg->pc, read) < 0)
		badpagefault(ureg, addr, read, insyscall);
	up->insyscall = insyscall;
}

typedef struct Lastexcept Lastexcept;
struct Lastexcept {		/* should be per cpu */
	uintptr	pc;
	uintptr	addr;		/* failed address of load or store */
	short	consec;
};

static void
faultstuck(Ureg *ureg)
{
	static Lastexcept exc;

	if (ureg->pc == exc.pc && ureg->tval == exc.addr) {
		if (++exc.consec >= 10)
			panic("%d consecutive exceptions at pc %#p for addr %#p",
				exc.consec, exc.pc, exc.addr);
	} else {
		exc.pc = ureg->pc;
		exc.addr = ureg->tval;
		exc.consec = 0;
	}
}

/* see trap() for Envcalluser short cut, mtrap.s for Envcallsup */
static Exchandler exchandlers[] = {
[Breakpt]	debugbpt,
[Instpage]	faultriscv64,		/* page fault */
[Loadpage]	faultriscv64,
[Storepage]	faultriscv64,
[Instaccess]	trapaccess,		/* failed pmp or pma check */
[Loadaccess]	trapaccess,
[Storeaccess]	trapaccess,
[Illinst]	trapillinst,
[Instaddralign]	trapmisaligned,
[Loadaddralign]	trapmisaligned,
[Storeaddralign] trapmisaligned,
[Envcalluser]	trapsyscall,		/* backstop; short-cut in trap */
[Envcallmch]	trapcallmch,
/*
 * missing: Envcallvsup, Debugexc, Instgpage, Loadgpage, Virtinstr, Storegpage,
 * Doubletrap, Swchk, Hwerr
 */
};

/* handle exceptions */
static int
trapriscv64(Ureg *ureg, Cause *cp)
{
	uint cause;
	Exchandler handler;
	if (TrapSpew) print("trapriscv64 ur %p cp %p\n", ureg, cp);
#ifdef xxx
	if (cp->user)
		m->turnedfpoff = 0;
	else if (m->probing) {
		m->probebad = 1;
		m->probing = 0;
		coherence();
		if (0)
			iprint("probe trapped\n");
		/* have to advance PC on risc-v to skip faulting instruction. */
		advancepc(ureg);
		return 0;			/* not a clock interrupt */
	}
#endif

	cause = cp->cause;
	if (cause >= nelem(exchandlers))
		panic("trapriscv64: cause %d out of range", cause);
	handler = exchandlers[cause];
	if (TrapSpew) print("trapriscv64: handler %p\n", handler);
	if (handler) {
		(*handler)(ureg, cp);
		if (Trapdebug)
			faultstuck(ureg);
		if (TrapSpew) print("trapriscv64: done handler\n");
		return 0;			/* not a clock interrupt */
	}
	if (TrapSpew) print("trapriscv64: no handler\n");
	/* could be a local intr */
	panic("unknown exception, cause %d from_user %d", cause, cp->user);
}

static int
traplocalintr(Ureg *ureg, Cause *cp)
{
	int clockintr;
	uint cause;

	if (TrapSpew) print("traplocalintr ureg %p cause %p\n", ureg, cause);
	m->intr++;			/* okay here; only tmr and sw intrs */
	m->perf.intrts = perfticks();
	cause = cp->cause;
	if (cause < Local0intr)
		cause &= ~Msdiff;	/* map mach to super codes */
	if (TrapSpew) print("case %x\n", cause);
	switch (cause) {
	case Suptmrintr:
		if (TrapSpew) print("clockintr\n");
		clockoff();
		if (TrapSpew) print("clockoff\n");
		if (++m->clockintrdepth > 1 && m->clockintrsok) {
			/* nested clock interrupt; probably shutting down */
			m->clockintrsok = 0;
			// iprint("cpu%d: nested clock interrupt\n", m->machno);
		}
		timerintr(ureg, 0);
		if (TrapSpew) print("timerintr DONE\n");
		--m->clockintrdepth;
		clockenable();
		clockintr = 1;
		break;
	case Supswintr:
		/*
		 * an ipi should normally pop out of wfi at splhi in idlehands,
		 * and not end up here.  getting here means that we sent an ipi
		 * to a cpu that stopped waiting before the ipi arrived, which
		 * is harmless.  sending the ipi zeroed ipiwait.  if we could
		 * interrupt idlehands, we would want to set mp->ipiwait=1 here
		 * so that it wouldn't be counted both by idlehands and below.
		 */
		gotipi = 1;
		clearipi();
		clockintr = 0;
		break;
	case Supextintr:
		/*
		 * NB: intr is not reached here, we short-circuited the cause
		 * to Globalintr in whatcause.
		 */
		// clockintr = intr(ureg, cp);
		panic("traplocalintr: handed an external interrupt");
	case 0:					/* probably NMI */
		if(m->machno == 0)
			panic("NMI @ %#p", ureg->pc);
		else {
			iprint("nmi: cpu%d: PC %#p\n", m->machno, ureg->pc);
			for(;;)
				idlehands();
		}
	default:
		panic("trap: unknown local interrupt %d", cp->cause);
	}
	// NOTE: this does not work for interrupt 0x20 (STIP); that only gets reset
	// by advancing the timer. Somehow that's not getting done correctly elsewhere.
	if ((clrsipbit(1<<cp->cause) & (1<<cause)) != 0) {
		uvlong next = rdtsc();
		print("clrsipbit(%llx): did not clear bit\n", 1<<cp->cause);
		sbisettimer(next + 0x100000);
		print("clrsipbit is now %llx\n", clrsipbit(1<<cp->cause));
	}
	if (TrapSpew) print("IMPLEMENT vecacct(vctl[cp->vno]);\n");
	if (TrapSpew) print("IMPLEMENENT intrtime(m, cp->vno);\n");
	if (!soc.plic)
		if (TrapSpew) print("implement poll(ureg, nil);		/* frequent polling */\n");
	return clockintr;
}
/*
 * call for global interrupts (those coming from the plic).
 * these are never clock interrupts, so returns 0 (not a clockintr).
 */
int
intr(Ureg* ureg, Cause *cp)
{
	USED(ureg); USED(cp); panic("implement intr");
#ifdef x
	m->intr++;
	int id, ctxt, vno, trips;
	Vctl *vec;

	if (++m->intrdepth > 1)
		iprint("cpu%d: nested intrs at depth %d\n", m->machno,
			m->intrdepth);
	ctxt = m->plicctxt + Super;
	if (Intrdebug && !soc.poll)
		iprint("intr: checking plic for ctxt %d\n", ctxt);
	trips = 100;
	while (soc.plic && (id = plicclaim(ctxt)) != 0) {
		m->perf.intrts = perfticks();
		/* id is actual cause, global irq.  map to a vector. */
		/* cp->vno = */ vno = vctlidx(id, Globalintr);
		if (vno < 0)
			panic("intr: intr id %d out of range", id);

		if (Intrdebug)
			iprint("intr: plic id %d vector %d\n", id, vno);
		vec = vctl[vno];
		if (vec == nil) {		/* maybe it's spurious? */
			plicdisable(vno);
			iprint("intr: no vector set up for intr id %d\n", id);
		} else
			callintrsvc(ureg, vec);
		pliccompl(id, ctxt);
		intrtime(m, vno);

		if (--trips <= 0) {
			plicoff();
			soc.plic = 0;		/* poll in future */
			iprint("intr: stuck in plicclaim loop, id %d, polling\n",
				id);
		}
	}
	if (!soc.plic)
		poll(ureg, cp);			/* mainly for tinyemu */
	/* having no work is not unusual */
	if (Intrdebug && !soc.poll)
		iprint("intr: done\n");
	m->intrdepth--;

	/* in case the cpus all raced into wfi, always wake */
	if(up)
		preempted();
	/*
	 * procs waiting for this interrupt could be on
	 * any cpu, so wake any idling cpus.
	 */
	idlewake();
#endif
	return 0;
}

static Traphandler traphandlers[Nfaulttypes] = {
[Unknownflt]	nil,
[Exception]	trapriscv64,			/* may enable fpu */
[Localintr]	traplocalintr,
[Globalintr]	intr,
};

/*
 *  All traps come here.  It is slower to have all traps call trap()
 *  rather than directly vectoring the handler.  However, this avoids a
 *  lot of code duplication and possible bugs.
 *  Trap is called with interrupts disabled.
 */
void
trap(Ureg* ureg)
{
	int clockintr;
	uint type;
	Cause why;
	Traphandler handler;

	if (Trapdebug) {
		if (ureg == nil)
			panic("trap with nil ureg");
	}

	/*
	 * if we trapped from supervisor mode into machine mode, revert to
	 * phys device addresses, assuming that we are about to reboot.
	 * this is only possible if we link with mtrap.$O (e.g., tinyemu).
	if (ureg->curmode == Mppmach && (ureg->status & Mpp) == Mppsuper)
		usephysdevaddrs();
*/

	type = whatcause(&why, ureg);
	if (TrapSpew) print("trapentry: type %d\n", type);
	if (Trapdebug)
		trapdbg(ureg, &why, 1);

	/* short-cut for syscalls */
	if (ureg->cause == Envcalluser)	/* Rv64intr is off for exceptions */
		trapsyscall(ureg, nil);
		/* syscall() did the whole job; we're done */
	else {
		if (type >= nelem(traphandlers))
			panic("trap: trap type %d too large", type);
		handler = traphandlers[type];
		if (TrapSpew) print("handler is %p\n", handler);
		if (handler == nil)
			panic("trap: trap type %d has no handler", type);
		clockintr = (*handler)(ureg, &why);
		if (TrapSpew) print("back from handler\n");
		splhi();		/* minimise harm if handler went low */
	//	fpsts2ureg(ureg); /* propagate Fsst changes back to user mode */

		/*
		 * delaysched set (because we held a lock or because our
		 * quantum ended)?
		 */
	/*	if(up && up->delaysched && clockintr && m->clockintrsok) {
			sched();
			splhi();
		}*/
		if(why.user) {
			if(up->procctl || up->nnote)
				notify(ureg, "h");
			/*
			 * kexit() bills time to whatever process was running,
			 * so don't call it here.
			 */
		}
	}

	if (0)if (Trapdebug)
		trapdbg(ureg, &why, 0);
	if (TrapSpew) print("all done trap()\n");
}

/*
 * Dump general registers.
 * try to fit it on a cga screen (80x25).
 */
static void
dumpgpr(Ureg* ureg)
{
	int i;

	if(up != nil)
		iprint("cpu%d: registers for %s %d\n",
			m->machno, up->text, up->pid);
	else
		iprint("cpu%d: registers for kernel\n", m->machno);

	for (i = 1; i <= 31; i++)
		iprint("r%d\t%#16.16p%c", i, ureg->regs[i],
			i%2 == 0 || i == 31? '\n': '\t');
	iprint("pc\t%#p\t", ureg->pc);
	iprint("type\t%#p\t", ureg->type);
	iprint("m\t%#16.16p\nup\t%#16.16p\n", m, up);
}

void
dumpregs(Ureg* ureg)
{
	if(getconf("*nodumpregs")){
		iprint("dumpregs disabled\n");
		return;
	}
	dumpgpr(ureg);

	/*
	 * Processor CSRs.
	 */
	iprint("satp\t%#16.16llux\n", (uvlong)rsatp());

//	archdumpregs();
}


void clearipi(void)
{
	panic("clearipi");
}

/* base plic context for mach (M mode). dependent upon system configuration */
/* TODO: do we want this? */
uint
mach2context(Mach *)
{
	int ctxtoff;		/* context relative to M of first real hart */

//	if (mach->machno < soc.hobbled)		/* hobbled mgmt hart? */
//		return mach->machno;
//	ctxtoff = sys->nprivmodes * (mach->machno - soc.hobbled);
//	return (soc.context0? soc.context0: soc.hobbled) + ctxtoff;
	ctxtoff = 0;
	return ctxtoff;
}
