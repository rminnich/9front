#include "u.h"
#include <ureg.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "tos.h"

static void
_dumpstack(Ureg *ureg)
{
	uintptr l, v, i, estack;
	extern ulong etext;
	int x;
	char *s;

	if((s = getconf("*nodumpstack")) != nil && strcmp(s, "0") != 0){
		iprint("dumpstack disabled\n");
		return;
	}
	iprint("cpu%d: dumpstack\n", m->machno);

	x = 0;
	x += iprint("ktrace /arm/9cycv %.8lux %.8lux %.8lux <<EOF\n", ureg->pc, ureg->sp, ureg->r14);
	i = 0;
	if(up
	&& (uintptr)&l >= (uintptr)up - KSTACK
	&& (uintptr)&l <= (uintptr)up)
		estack = (uintptr)up;
	else if((uintptr)&l >= (uintptr)m->stack
	&& (uintptr)&l <= (uintptr)m+MACHSIZE)
		estack = (uintptr)m+MACHSIZE;
	else
		return;
	x += iprint("estackx %p\n", estack);

	for(l = (uintptr)&l; l < estack; l += sizeof(uintptr)){
		v = *(uintptr*)l;
		if((KTZERO < v && v < (uintptr)&etext) || estack-l < 32){
			x += iprint("%.8p=%.8p ", l, v);
			i++;
		}
		if(i == 4){
			i = 0;
			x += iprint("\n");
		}
	}
	if(i)
		iprint("\n");
	iprint("EOF\n");
}

static char*
faulterr[0x20] = {
[0x01]	"alignement fault",
[0x02]	"debug event",
[0x04]	"fault on instruction cache maintenance",
[0x08]	"synchronous external abort",
[0x0C]	"synchronous external abort on translation table walk L1",
[0x0E]	"synchronous external abort on translation table walk L2",
[0x10]	"tlb conflict abort",
[0x16]	"asynchronous external abort",
[0x19]	"synchronous parity error on memory access",
[0x1C]	"synchronous parity error on translation table walk L1",
[0x1E]	"synchronous parity error on translation table walk L2",
};

static void
faultarm(Ureg *ureg, ulong fsr, uintptr addr)
{
	int read;
	char *type;

	if(!userureg(ureg)){
		if(addr >= USTKTOP){
			dumpregs(ureg);
			panic("kernel fault: bad address pc=%#.8lux addr=%#.8lux fsr=%#.8lux", ureg->pc, addr, fsr);
		}
	}

	read = (fsr & (1<<11)) == 0;
	switch(fsr & 0x1F){
	case 0x05:	/* translation fault L1 */
	case 0x07:	/* translation fault L2 */
	case 0x03:	/* access flag fault L1 */
	case 0x06:	/* access flag fault L2 */
	case 0x09:	/* domain fault L1 */
	case 0x0B:	/* domain fault L2 */
	case 0x0D:	/* permission fault L1 */
	case 0x0F:	/* permission fault L2 */
		if(fault(addr, ureg->pc, read) == 0)
			break;
		/* wet floor */
	default:
		type = faulterr[fsr & 0x1F];
		if(type == nil)
			type = "fault";
		if(!userureg(ureg)){
			dumpregs(ureg);
			panic("kernel %s: pc=%#.8lux addr=%#.8lux fsr=%#.8lux", type, ureg->pc, addr, fsr);
		}
		faultnote(type, read? "read": "write", addr);
	}
}

static void
mathtrap(Ureg *, ulong)
{
	int s;

	if(up->fpstate & FPnotify){
		postnote(up, 1, "sys: floating point in note handler", NDebug);
		return;
	}
	switch(up->fpstate){
	case FPinit:
		s = splhi();
		fpinit();
		up->fpstate = FPactive;
		splx(s);
		break;
	case FPinactive:
		s = splhi();
		fprestore(up->fpsave);
		up->fpstate = FPactive;
		splx(s);
		break;
	case FPactive:
		postnote(up, 1, "sys: floating point error", NDebug);
		break;
	}
}

void
trap(Ureg *ureg)
{
	int user;
	ulong opc, cp;

	user = kenter(ureg);
	switch(ureg->type){
	case PsrMund:
		ureg->pc -= 4;
		if(user){
			spllo();
			if(okaddr(ureg->pc, 4, 0)){
				opc = *(ulong*)ureg->pc;
				if((opc & 0x0f000000) == 0x0e000000 || (opc & 0x0e000000) == 0x0c000000){
					cp = opc >> 8 & 15;
					if(cp == 10 || cp == 11){
						mathtrap(ureg, opc);
						break;
					}
				}
			}
			postnote(up, 1, "sys: trap: invalid opcode", NDebug);
			break;
		}
		dumpregs(ureg);
		panic("invalid opcode at pc=%#.8lux lr=%#.8lux", ureg->pc, ureg->r14);
		break;
	case PsrMiabt:
		ureg->pc -= 4;
		faultarm(ureg, getifsr(), getifar());
		break;
	case PsrMabt:
		ureg->pc -= 8;
		faultarm(ureg, getdfsr(), getdfar());
		break;
	case PsrMirq:
		ureg->pc -= 4;
		intr(ureg);
		break;
	default:
		iprint("cpu%d: unknown trap type %ulx\n", m->machno, ureg->type);
	}
	splhi();
	if(user){
		if(up->procctl || up->nnote)
			donotify(ureg);
		kexit(ureg);
	}
}

void
syscall(Ureg *ureg)
{
	ulong scallnr;
	
	if(!kenter(ureg))
		panic("syscall: pc=%#.8lux", ureg->pc);
	scallnr = ureg->r0;
	dosyscall(scallnr, (Sargs*)(ureg->sp + BY2WD), &ureg->r0);
	if(up->procctl || up->nnote)
		donotify(ureg);
	if(up->delaysched)
		sched();
	kexit(ureg);
}

void
fpunotify(Proc *p)
{
	if(p->fpstate == FPactive){
		fpsave(p->fpsave);
		p->fpstate = FPinactive;
	}
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

int
noted(Ureg *ureg, Ureg *nureg, int arg0)
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
		ureg->sp = sp;
		ureg->r0 = (uintptr) oureg;
		if(!okaddr(ureg->pc, BY2WD, 0)
		|| !okaddr(ureg->sp, 4*BY2WD, 1)
		|| (ureg->pc & 3) != 0 || (ureg->sp & 3) != 0)
			return -1;
		((ulong *) sp)[1] = oureg;
		((ulong *) sp)[0] = 0;
		break;
	}
	return 0;
}


void
dumpstack(void)
{
	callwithureg(_dumpstack);
}

void
dumpregs(Ureg *ureg)
{
	iprint("trap: %lux psr %8.8lux type %2.2lux pc %8.8lux link %8.8lux\n",
		ureg->type, ureg->psr, ureg->type, ureg->pc, ureg->link);
	iprint("R14 %8.8lux R13 %8.8lux R12 %8.8lux R11 %8.8lux R10 %8.8lux\n",
		ureg->r14, ureg->r13, ureg->r12, ureg->r11, ureg->r10);
	iprint("R9  %8.8lux R8  %8.8lux R7  %8.8lux R6  %8.8lux R5  %8.8lux\n",
		ureg->r9, ureg->r8, ureg->r7, ureg->r6, ureg->r5);
	iprint("R4  %8.8lux R3  %8.8lux R2  %8.8lux R1  %8.8lux R0  %8.8lux\n",
		ureg->r4, ureg->r3, ureg->r2, ureg->r1, ureg->r0);
	iprint("pc %#lux link %#lux\n", ureg->pc, ureg->link);
}

void
setkernur(Ureg *ureg, Proc *p)
{
	ureg->pc = p->sched.pc;
	ureg->sp = p->sched.sp + 4;
	ureg->r14 = (uintptr) sched;
}

void
setregisters(Ureg* ureg, char* pureg, char* uva, int n)
{
	ulong v;

	v = ureg->psr;
	memmove(pureg, uva, n);
	ureg->psr = ureg->psr & 0xf80f0000 | v & 0x07f0ffff;
}

void
callwithureg(void (*f) (Ureg *))
{
	Ureg u;
	
	u.pc = getcallerpc(&f);
	u.sp = (uintptr) &f - 4;
	f(&u);
}

uintptr
userpc(void)
{
	Ureg *ur;
	
	ur = up->dbgreg;
	return ur->pc;
}

uintptr
dbgpc(Proc *)
{
	Ureg *ur;
	
	ur = up->dbgreg;
	if(ur == nil)
		return 0;
	return ur->pc;
}

void
procsave(Proc *p)
{
	if(p->fpstate == FPactive){
		if(p->state == Moribund)
			fpclear();
		else
			fpsave(p->fpsave);
		p->fpstate = FPinactive;
	}

	l1switch(&m->l1, 0);
}

void
procrestore(Proc*)
{
}

void
kprocchild(Proc *p, void (*entry)(void))
{
	p->sched.pc = (uintptr) entry;
	p->sched.sp = (uintptr) p;
}

void
forkchild(Proc *p, Ureg *ureg)
{
	Ureg *cureg;

	p->sched.pc = (uintptr) forkret;
	p->sched.sp = (uintptr) p - sizeof(Ureg);

	cureg = (Ureg*) p->sched.sp;
	memmove(cureg, ureg, sizeof(Ureg));
	cureg->r0 = 0;
}

uintptr
execregs(uintptr entry, ulong ssize, ulong nargs)
{
	ulong *sp;
	Ureg *ureg;

	sp = (ulong*)(USTKTOP - ssize);
	*--sp = nargs;

	ureg = up->dbgreg;
	ureg->sp = (uintptr) sp;
	ureg->pc = entry;
	ureg->r14 = 0;
	return USTKTOP-sizeof(Tos);
}
