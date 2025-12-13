#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include <tos.h>
#include "ureg.h"
#include "../riscv64/sysreg.h"

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
	panic("procsetup");
//	fpuprocsetup(p);
//	p->tpidr = 0;
//	syswr(TPIDR_EL0, p->tpidr);
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
	panic("userureg");
//	return 0;
}

void
callwithureg(void (*f) (Ureg *))
{
	print("callwithureg %p\n", f);
	panic("callwithureg");
#ifdef XXX
	Ureg u;
	
	u.pc = getcallerpc(&f);
	u.sp = (uintptr) &f;
	f(&u);
#endif
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
	panic("evenaddr");
#ifdef XXX
	if(addr & 3){
		postnote(up, 1, "sys: odd address", NDebug);
		error(Ebadarg);
	}
#endif
}

void
forkchild(Proc *p, Ureg *ureg)
{
	print("forkchil %p %p\n", p, ureg);
	panic("forkchild");
#ifdef XXX
	Ureg *cureg;

	p->sched.pc = (uintptr) forkret;
	p->sched.sp = (uintptr) p - TRAPFRAMESIZE;

	cureg = (Ureg*) (p->sched.sp + 16);
	memmove(cureg, ureg, sizeof(Ureg));
	cureg->r0 = 0;
#endif
}

uintptr
execregs(uintptr entry, int argc, char *argv[], Tos *tos)
{
	print("execregs %p %d %p %p\n", entry, argc, argv, tos);
	panic("execregs");
#ifdef XXX
	uintptr *sp;
	Ureg *ureg;

	sp = (uintptr*)(USTKTOP - ssize);
	*--sp = nargs;

	ureg = up->dbgreg;
	ureg->sp = (uintptr)sp;
	ureg->pc = entry;
	ureg->link = 0;
	return USTKTOP-sizeof(Tos);
	return 0;
#endif
}
