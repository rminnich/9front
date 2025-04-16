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
		notify(ur);
	/* if we delayed sched because we held a lock, sched now */
	if(up->delaysched)
		sched();
	kexit(ur);
	/* restore EXL in status */
	setstatus(getstatus() | EXL);
}

void
fpunotify(void)
{
	up->fpstate |= FPnotify;
}

void
fpunoted(void)
{
	up->fpstate &= ~FPnotify;
}

FPsave*
notefpsave(Proc*)
{
	return nil;
}

int
notify(Ureg *ur)
{
	ulong sp;
	char *msg;

	if(up->procctl)
		procctl();
	if(up->nnote == 0)
		return 0;

	spllo();
	qlock(&up->debug);
	msg = popnote(ur);
	if(msg == nil){
		qunlock(&up->debug);
		splhi();
		return 0;
	}

	sp = ur->usp - sizeof(Ureg) - BY2WD; /* spim libc */

	if(!okaddr((ulong)up->notify, BY2WD, 0) ||
	   !okaddr(sp-ERRMAX-4*BY2WD, sizeof(Ureg)+ERRMAX+4*BY2WD, 1)) {
		iprint("suicide: bad address or sp in notify\n");
		qunlock(&up->debug);
		pexit("Suicide", 0);
	}

	memmove((Ureg*)sp, ur, sizeof(Ureg));	/* push user regs */
	*(Ureg**)(sp-BY2WD) = up->ureg;	/* word under Ureg is old up->ureg */
	up->ureg = (void*)sp;

	sp -= BY2WD+ERRMAX;
	memmove((char*)sp, msg, ERRMAX);	/* push err string */

	sp -= 3*BY2WD;
	*(ulong*)(sp+2*BY2WD) = sp+3*BY2WD;	/* arg 2 is string */
	ur->r1 = (long)up->ureg;		/* arg 1 is ureg* */
	((ulong*)sp)[1] = (ulong)up->ureg;	/* arg 1 0(FP) is ureg* */
	((ulong*)sp)[0] = 0;			/* arg 0 is pc */
	ur->usp = sp;
	/*
	 * arrange to resume at user's handler as if handler(ureg, errstr)
	 * were being called.
	 */
	ur->pc = (ulong)up->notify;

	fpunotify();
	qunlock(&up->debug);
	splhi();
	return 1;
}


/*
 * Return user to state before notify(); called from user's handler.
 */
void
noted(Ureg *kur, int arg0)
{
	Ureg *nur;
	ulong oureg, sp;

	qlock(&up->debug);
	if(arg0!=NRSTR && !up->notified) {
		qunlock(&up->debug);
		pprint("call to noted() when not notified\n");
		pexit("Suicide", 0);
	}
	up->notified = 0;

	fpunoted();

	nur = up->ureg;

	oureg = (ulong)nur;
	if((oureg & (BY2WD-1)) || !okaddr((ulong)oureg-BY2WD, BY2WD+sizeof(Ureg), 0)){
		qunlock(&up->debug);
		pprint("bad up->ureg in noted or call to noted() when not notified\n");
		pexit("Suicide", 0);
	}

	setregisters(kur, (char*)kur, (char*)nur, sizeof(Ureg));

	switch(arg0) {
	case NCONT:
	case NRSTR:				/* only used by APE */
		if(!okaddr(kur->pc, BY2WD, 0) || !okaddr(kur->usp, BY2WD, 0)){
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		up->ureg = (Ureg*)(*(ulong*)(oureg-BY2WD));
		qunlock(&up->debug);
		break;

	case NSAVE:				/* only used by APE */
		sp = oureg-4*BY2WD-ERRMAX;
		kur->r1 = oureg;		/* arg 1 is ureg* */
		kur->usp = sp;
		if(!okaddr(kur->pc, BY2WD, 0) || !okaddr(kur->usp, 4*BY2WD, 1)){
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		qunlock(&up->debug);
		((ulong*)sp)[1] = oureg;	/* arg 1 0(FP) is ureg* */
		((ulong*)sp)[0] = 0;		/* arg 0 is pc */
		break;

	default:
		up->lastnote->flag = NDebug;
		/* fall through */

	case NDFLT:
		qunlock(&up->debug);
		if(up->lastnote->flag == NDebug)
			pprint("suicide: %s\n", up->lastnote->msg);
		pexit(up->lastnote->msg, up->lastnote->flag!=NDebug);
	}
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
