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

enum {
	Trapdebug	= 1,
	Probedebug	= 0,
	Intrdebug	= 0,
	Tryallcpus	= 0,

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
	print("procsave %p\n", p);
	panic("procsave");
//	fpuprocsave(p);
//	if(p->kp == 0)
//		p->tpidr = sysrd(TPIDR_EL0);
//	putasid(p);	// release asid
}

void
procrestore(Proc *p)
{
	print("procsave %p\n", p);
	panic("procsave");
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
	print("userureg %p\n", ureg);
	return 1;
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
	print("evenaddr %p\n", addr);
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

	uintptr *sp;
	Ureg *ureg;

	sp = (uintptr*)(USTKTOP - 64); // Actually sizeof (TOS) I guess.
	*--sp = argc;

	ureg = up->dbgreg;
	ureg->sp = (uintptr)sp;
	ureg->pc = entry;
	ureg->link = 0;
	return USTKTOP-sizeof(Tos);
	return 0;
}

static void panictrap(Ureg *ureg)
{
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

//	m->turnedfpoff = 0;

	/* syscall may change ureg->pc, so save a copy. */
	pc = ureg->pc;
/*	if (getsp() % sizeof(vlong) != 0)
		print("trapsyscall: odd sp %#p at %#p\n", getsp(), pc);*/
	scallnr = ureg->arg;
	/* on riscv64, ureg->ret is ureg->arg, so can't zero ureg->ret here. */
	/* Last syscall argument is location of return value in frame. */
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
trapdbg(Ureg *ureg, Cause *cp, int entry)
{
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
	print("type %d\n", type);
	return type;
}

static Traphandler traphandlers[Nfaulttypes] = {
[Unknownflt]	nil,
//[Exception]	trapriscv64,			/* may enable fpu */
//[Localintr]	traplocalintr,
//[Globalintr]	intr,
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
		if (handler == nil)
			panic("trap: trap type %d has no handler", type);
		clockintr = (*handler)(ureg, &why);
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

	if (Trapdebug)
		trapdbg(ureg, &why, 0);
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
