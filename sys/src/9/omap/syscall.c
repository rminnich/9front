#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include <tos.h>
#include "ureg.h"

#include "arm.h"

typedef struct {
	uintptr	ip;
	Ureg*	arg0;
	char*	arg1;
	char	msg[ERRMAX];
	Ureg*	old;
	Ureg	ureg;
} NFrame;

/*
 *   Return user to state before notify()
 */
void
noted(Ureg* cur, int arg0)
{
	NFrame *nf;

	qlock(&up->debug);
	if(up->notified){
		up->notified = 0;
		splhi();
		fpunoted();
		spllo();
	} else if(arg0!=NRSTR){
		qunlock(&up->debug);
		pprint("call to noted() when not notified\n");
		pexit("Suicide", 0);
	}

	nf = up->ureg;

	/* sanity clause */
	if(!okaddr((uintptr)nf, sizeof(NFrame), 0)){
		qunlock(&up->debug);
		pprint("bad ureg in noted %#p\n", nf);
		pexit("Suicide", 0);
	}

	setregisters(cur, (char*)cur, (char*)&nf->ureg, sizeof(Ureg));

	switch((int)arg0){
	case NCONT:
	case NRSTR:
		if(!okaddr(cur->pc, BY2WD, 0) || !okaddr(cur->sp, BY2WD, 0)){
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		up->ureg = nf->old;
		qunlock(&up->debug);
		break;
	case NSAVE:
		cur->sp = (uintptr)nf;
		cur->r0 = (uintptr)&nf->ureg;
		if(!okaddr(cur->pc, BY2WD, 0) || !okaddr(cur->sp, sizeof(NFrame), 1)){
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		qunlock(&up->debug);
		nf->arg1 = nf->msg;
		nf->arg0 = &nf->ureg;
		nf->ip = 0;
		break;
	default:
		up->lastnote->flag = NDebug;
		/*FALLTHROUGH*/
	case NDFLT:
		qunlock(&up->debug);
		if(up->lastnote->flag == NDebug)
			pprint("suicide: %s\n", up->lastnote->msg);
		pexit(up->lastnote->msg, up->lastnote->flag != NDebug);
	}
}

/*
 *  Call user, if necessary, with note.
 *  Pass user the Ureg struct and the note on his stack.
 */
int
notify(Ureg* ureg)
{
	uintptr sp;
	NFrame *nf;
	char *msg;

	if(up->procctl)
		procctl();
	if(up->nnote == 0)
		return 0;

	spllo();
	qlock(&up->debug);
	msg = popnote(ureg);
	if(msg == nil){
		qunlock(&up->debug);
		splhi();
		return 0;
	}

	if(!okaddr((uintptr)up->notify, 1, 0)){
		qunlock(&up->debug);
		pprint("suicide: notify function address %#p\n", up->notify);
		pexit("Suicide", 0);
	}

	sp = ureg->sp - sizeof(NFrame);
	if(!okaddr(sp, sizeof(NFrame), 1)){
		qunlock(&up->debug);
		pprint("suicide: notify stack address %#p\n", sp);
		pexit("Suicide", 0);
	}

	nf = (void*)sp;
	memmove(&nf->ureg, ureg, sizeof(Ureg));
	nf->old = up->ureg;
	up->ureg = nf;
	memmove(nf->msg, msg, ERRMAX);
	nf->arg1 = nf->msg;
	nf->arg0 = &nf->ureg;
	nf->ip = 0;

	ureg->sp = sp;
	ureg->pc = (uintptr)up->notify;
	ureg->r0 = (uintptr)nf->arg0;

	splhi();
	fpunotify();
	qunlock(&up->debug);

	return 1;
}

void
syscall(Ureg* ureg)
{
	ulong scallnr;

	if(!kenter(ureg))
		panic("syscall: from kernel: pc %#lux r14 %#lux psr %#lux",
			ureg->pc, ureg->r14, ureg->psr);
	scallnr = ureg->r0;
	dosyscall(scallnr, (Sargs*)(ureg->sp + BY2WD), &ureg->r0);
	if(up->procctl || up->nnote)
		notify(ureg);
	/* if we delayed sched because we held a lock, sched now */
	if(up->delaysched)
		sched();
	kexit(ureg);
}

uintptr
execregs(uintptr entry, ulong ssize, ulong nargs)
{
	ulong *sp;
	Ureg *ureg;

	sp = (ulong*)(USTKTOP - ssize);
	*--sp = nargs;

	ureg = up->dbgreg;
//	memset(ureg, 0, 15*sizeof(ulong));
	ureg->r13 = (ulong)sp;
	ureg->pc = entry;
//print("%lud: EXECREGS pc %#ux sp %#ux nargs %ld\n", up->pid, ureg->pc, ureg->r13, nargs);

	/*
	 * return the address of kernel/user shared data
	 * (e.g. clock stuff)
	 */
	return USTKTOP-sizeof(Tos);
}

void
sysprocsetup(Proc* p)
{
	fpusysprocsetup(p);
}

/* 
 *  Craft a return frame which will cause the child to pop out of
 *  the scheduler in user mode with the return register zero.  Set
 *  pc to point to a l.s return function.
 */
void
forkchild(Proc *p, Ureg *ureg)
{
	Ureg *cureg;

//print("%lud setting up for forking child %lud\n", up->pid, p->pid);
	p->sched.sp = (ulong)p-sizeof(Ureg);
	p->sched.pc = (ulong)forkret;

	cureg = (Ureg*)(p->sched.sp);
	memmove(cureg, ureg, sizeof(Ureg));

	/* syscall returns 0 for child */
	cureg->r0 = 0;
}
