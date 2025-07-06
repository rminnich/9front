#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include "tos.h"
#include "ureg.h"

FPsave initfp;

/*
 * called directly from assembler, not via trap()
 */
void
syscall(Ureg *ur)
{
	ulong scallnr;

	if(!kenter(ur))
		panic("syscall from kernel");
	scallnr = ur->r1;
	if(dosyscall(scallnr, (Sargs*)ur->sp, &ur->r1) == 0)
		ur->pc += 4;
	if(up->procctl || up->nnote)
		donotify(ur);
	/* if we delayed sched because we held a lock, sched now */
	if(up->delaysched)
		sched();
	kexit(ur);
	/* restore EXL in status */
	setstatus(getstatus() | EXL);
}

void
fpunotify(Proc *p)
{
	p->fpstate |= FPnotify;
}

void
fpunoted(Proc *p)
{
	p->fpstate &= ~FPnotify;
}

FPsave*
notefpsave(Proc*)
{
	return nil;
}

Ureg*
notify(Ureg *ur, char *msg)
{
	Ureg *nur;
	ulong sp;

	sp = ur->usp - sizeof(Ureg) - BY2WD; /* spim libc */

	if(!okaddr(sp-ERRMAX-4*BY2WD, sizeof(Ureg)+ERRMAX+4*BY2WD, 1))
		return nil;

	nur = (Ureg*)sp;
	memmove(nur, ur, sizeof(Ureg));	/* push user regs */

	sp -= BY2WD+ERRMAX;
	memmove((char*)sp, msg, ERRMAX);	/* push err string */

	sp -= 3*BY2WD;
	*(ulong*)(sp+2*BY2WD) = sp+3*BY2WD;	/* arg 2 is string */
	ur->r1 = (long)nur;			/* arg 1 is ureg* */
	((ulong*)sp)[1] = (ulong)nur;		/* arg 1 0(FP) is ureg* */
	((ulong*)sp)[0] = 0;			/* arg 0 is pc */
	ur->usp = sp;
	/*
	 * arrange to resume at user's handler as if handler(ureg, errstr)
	 * were being called.
	 */
	ur->pc = (ulong)up->notify;
	return nur;
}


/*
 * Return user to state before notify(); called from user's handler.
 */
int
noted(Ureg *kur, Ureg *nur, int arg0)
{
	ulong oureg, sp;

	oureg = (ulong)nur;
	if(oureg & (BY2WD-1))
		return -1;

	setregisters(kur, (char*)kur, (char*)nur, sizeof(Ureg));

	switch(arg0) {
	case NCONT:
	case NRSTR:				/* only used by APE */
		if(!okaddr(kur->pc, BY2WD, 0) || !okaddr(kur->usp, BY2WD, 0))
			return -1;
		break;

	case NSAVE:				/* only used by APE */
		sp = oureg-4*BY2WD-ERRMAX;
		kur->r1 = oureg;		/* arg 1 is ureg* */
		kur->usp = sp;
		if(!okaddr(kur->pc, BY2WD, 0) || !okaddr(kur->usp, 4*BY2WD, 1))
			return -1;
		((ulong*)sp)[1] = oureg;	/* arg 1 0(FP) is ureg* */
		((ulong*)sp)[0] = 0;		/* arg 0 is pc */
		break;
	}
	return 0;
}


void
forkchild(Proc *p, Ureg *ur)
{
	Ureg *cur;

//	iprint("%lud setting up for forking child %lud\n", up->pid, p->pid);
	p->sched.sp = (ulong)p - UREGSIZE;
	p->sched.pc = (ulong)forkret;

	cur = (Ureg*)(p->sched.sp+2*BY2WD);
	memmove(cur, ur, sizeof(Ureg));

	cur->r1 = 0;
	cur->pc += 4;
}


/* set up user registers before return from exec() */
uintptr
execregs(ulong entry, ulong ssize, ulong nargs)
{
	Ureg *ur;
	ulong *sp;

	sp = (ulong*)(USTKTOP - ssize);
	*--sp = nargs;

	ur = (Ureg*)up->dbgreg;
	ur->usp = (ulong)sp;
	ur->pc = entry - 4;		/* syscall advances it */

//	iprint("%lud: %s EXECREGS pc %#luX sp %#luX nargs %ld", up->pid, up->text, ur->pc, ur->usp, nargs);
//	delay(20);

	return USTKTOP-sizeof(Tos);	/* address of kernel/user shared data */
}
