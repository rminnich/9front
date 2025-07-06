#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include <tos.h>
#include "ureg.h"

#include "arm.h"

/*
 *   Return user to state before notify()
 */
int
noted(Ureg* ureg, Ureg *nureg, int arg0)
{
	ulong oureg, sp;
	
	oureg = (ulong) nureg;
	if((oureg & 3) != 0)
		return -1;

	setregisters(ureg, (char*)ureg, (char*)nureg, sizeof(Ureg));
	
	switch(arg0){
	case NCONT: case NRSTR:
		if(!okaddr(ureg->pc, BY2WD, 0)
		|| !okaddr(ureg->sp, BY2WD, 0)
		|| (ureg->pc & 3) != 0 || (ureg->sp & 3) != 0)
			return -1;
		break;
	
	case NSAVE:
		sp = oureg - 4 * BY2WD - ERRMAX;
		if(!okaddr(ureg->pc, BY2WD, 0)
		|| !okaddr(sp, 4 * BY2WD, 1)
		|| (ureg->pc & 3) != 0 || (sp & 3) != 0)
			return -1;
		ureg->sp = sp;
		ureg->r0 = (uintptr) oureg;
		((ulong *) sp)[1] = oureg;
		((ulong *) sp)[0] = 0;
		break;
	}
	return 0;
}

Ureg*
notify(Ureg *ureg, char *msg)
{
	Ureg *nureg;
	ulong sp;

	sp = ureg->sp;
	sp -= 256;	/* debugging: preserve context causing problem */
	sp -= sizeof(Ureg);

	if(!okaddr(sp-ERRMAX-4*BY2WD, sizeof(Ureg)+ERRMAX+4*BY2WD, 1)
	|| ((uintptr) up->notify & 3) != 0
	|| (sp & 3) != 0)
		return nil;

	nureg = (Ureg*)sp;
	memmove(nureg, ureg, sizeof(Ureg));
	sp -= BY2WD+ERRMAX;
	memmove((char*)sp, msg, ERRMAX);
	sp -= 3*BY2WD;
	*(ulong*)(sp+2*BY2WD) = sp+3*BY2WD;
	*(ulong*)(sp+1*BY2WD) = (ulong)nureg;
	ureg->r0 = (uintptr)nureg;
	ureg->sp = sp;
	ureg->pc = (uintptr)up->notify;
	ureg->r14 = 0;

	return nureg;
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
		donotify(ureg);
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
