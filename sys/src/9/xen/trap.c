#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"ureg.h"
#include	"../port/error.h"
#include	<trace.h>

#define INTRLOG(a)  
#define SETUPLOG(a)
#define SYSCALLLOG(a)
#define FAULTLOG(a) 
#define FAULTLOGFAST(a)
#define POSTNOTELOG(a)
#define TRAPLOG(a)

int faultpanic = 0;

enum {
	/* trap_info_t flags */
	SPL0 = 0,
	SPL3 = 3,
	EvDisable = 4,
};

extern void irqinit(void);
extern int irqhandled(Ureg*, int);

static void debugbpt(Ureg*, void*);
static void fault386(Ureg*, void*);
static void safe_fault386(Ureg*, void*);
static void doublefault(Ureg*, void*);
static void unexpected(Ureg*, void*);
static void _dumpstack(Ureg*);

/* we started out doing the 'giant bulk init' for all traps. 
  * we're going to do them one-by-one since error analysis is 
  * so much easier that way.
  */
void
trapinit(void)
{
	trap_info_t t[2];
	ulong vaddr;
	int v, flag;

	HYPERVISOR_set_callbacks(
		KESEL, (ulong)hypervisor_callback,
		KESEL, (ulong)failsafe_callback);

	/* XXX rework as single hypercall once debugged */
	t[1].address = 0;
	vaddr = (ulong)vectortable;
	for(v = 0; v < 256; v++){
		switch(v){
		case VectorBPT:
		case VectorSYSCALL:
			flag = SPL3 | EvDisable;
			break;
		default:
			flag = SPL0 | EvDisable;
			break;
		}
		t[0] = (trap_info_t){ v, flag, KESEL, vaddr };
		if(HYPERVISOR_set_trap_table(t) < 0)
			panic("trapinit: FAIL: try to set: 0x%x, 0x%x, 0x%x, 0x%ulx\n", 
				t[0].vector, t[0].flags, t[0].cs, t[0].address);
		vaddr += 6;
	}

	irqinit();

	/*
	 * Special traps.
	 * Syscall() is called directly without going through trap().
	 */
	trapenable(VectorBPT, debugbpt, 0, "debugpt");
	trapenable(VectorPF, fault386, 0, "fault386");
	trapenable(Vector2F, doublefault, 0, "doublefault");
	trapenable(Vector15, unexpected, 0, "unexpected");
}

static char* excname[32] = {
	"divide error",
	"debug exception",
	"nonmaskable interrupt",
	"breakpoint",
	"overflow",
	"bounds check",
	"invalid opcode",
	"coprocessor not available",
	"double fault",
	"coprocessor segment overrun",
	"invalid TSS",
	"segment not present",
	"stack exception",
	"general protection violation",
	"page fault",
	"15 (reserved)",
	"coprocessor error",
	"alignment check",
	"machine check",
	"19 (reserved)",
	"20 (reserved)",
	"21 (reserved)",
	"22 (reserved)",
	"23 (reserved)",
	"24 (reserved)",
	"25 (reserved)",
	"26 (reserved)",
	"27 (reserved)",
	"28 (reserved)",
	"29 (reserved)",
	"30 (reserved)",
	"31 (reserved)",
};

static int
usertrap(int vno)
{
	char buf[ERRMAX];

	if(vno < nelem(excname)){
		spllo();
		sprint(buf, "sys: trap: %s", excname[vno]);
		postnote(up, 1, buf, NDebug);
		return 1;
	}
	return 0;
}

/*
 *  All traps come here.  It is slower to have all traps call trap()
 *  rather than directly vectoring the handler.  However, this avoids a
 *  lot of code duplication and possible bugs.  The only exception is
 *  VectorSYSCALL.
 *  Trap is called with interrupts disabled via interrupt-gates.
 */
void
trap(Ureg* ureg)
{
	int vno, user;

	user = kenter(ureg);
	vno = ureg->trap;
	if(!irqhandled(ureg, vno) && (!user || !usertrap(vno))){
		if(!user){
			/* early fault before trapinit() */
			if(vno == VectorPF)
				fault386(ureg, 0);
		}

		dumpregs(ureg);
		if(!user){
			ureg->sp = (ulong)&ureg->sp;
			_dumpstack(ureg);
		}
		if(vno < nelem(excname))
			panic("%s", excname[vno]);
		panic("unknown trap/intr: %d", vno);
	}
	splhi();

	if(user){
		if(up->procctl || up->nnote)
			donotify(ureg);
		kexit(ureg);
	}

	if (ureg->trap == 0xe) {
		/*
		  * on page fault, we need to restore the old spl
		  * Xen won't do it for us.
		  * XXX verify this.
		  */
		if (ureg->flags & 0x200)
			spllo();
	}
}

void
dumpregs2(Ureg* ureg)
{
	if(up)
		print("cpu%d: registers for %s %lud\n",
			m->machno, up->text, up->pid);
	else
		print("cpu%d: registers for kernel\n", m->machno);
	print("FLAGS=%luX TRAP=%luX ECODE=%luX PC=%luX",
		ureg->flags, ureg->trap, ureg->ecode, ureg->pc);
	print(" SS=%4.4luX USP=%luX\n", ureg->ss & 0xFFFF, ureg->usp);
	print("  AX %8.8luX  BX %8.8luX  CX %8.8luX  DX %8.8luX\n",
		ureg->ax, ureg->bx, ureg->cx, ureg->dx);
	print("  SI %8.8luX  DI %8.8luX  BP %8.8luX\n",
		ureg->si, ureg->di, ureg->bp);
	print("  CS %4.4luX  DS %4.4luX  ES %4.4luX  FS %4.4luX  GS %4.4luX\n",
		ureg->cs & 0xFFFF, ureg->ds & 0xFFFF, ureg->es & 0xFFFF,
		ureg->fs & 0xFFFF, ureg->gs & 0xFFFF);
}

void
dumpregs(Ureg* ureg)
{
	extern ulong etext;

	dumpregs2(ureg);

	/*
	 * Processor control registers.
	 * If machine check exception, time stamp counter, page size extensions
	 * or enhanced virtual 8086 mode extensions are supported, there is a
	 * CR4. If there is a CR4 and machine check extensions, read the machine
	 * check address and machine check type registers if RDMSR supported.
	 */
	print("SKIPPING get of crx and other such stuff.\n");/* */
#ifdef NOT
	print("  CR0 %8.8lux CR2 %8.8lux CR3 %8.8lux",
		getcr0(), getcr2(), getcr3());
	if(m->cpuiddx & 0x9A){
		print(" CR4 %8.8lux", getcr4());
		if((m->cpuiddx & 0xA0) == 0xA0){
			rdmsr(0x00, &mca);
			rdmsr(0x01, &mct);
			print("\n  MCA %8.8llux MCT %8.8llux", mca, mct);
		}
	}
#endif
	print("\n  ur %lux up %lux\n", (ulong)ureg, (ulong)up);
}


/*
 * Fill in enough of Ureg to get a stack trace, and call a function.
 * Used by debugging interface rdb.
 */
void
callwithureg(void (*fn)(Ureg*))
{
	Ureg ureg;
	ureg.pc = getcallerpc(&fn);
	ureg.sp = (ulong)&fn;
	fn(&ureg);
}

static void
_dumpstack(Ureg *ureg)
{
	ulong l, v, i, estack;
	extern ulong etext;
	int x;

	if(getconf("*nodumpstack")){
		iprint("dumpstack disabled\n");
		return;
	}
	iprint("dumpstack\n");
	x = 0;
	x += print("ktrace /kernel/path %.8lux %.8lux <<EOF\n", ureg->pc, ureg->sp);
	i = 0;
	if(up
	&& (ulong)&l >= (ulong)up - KSTACK
	&& (ulong)&l <= (ulong)up)
		estack = (ulong)up;
	else if((ulong)&l >= (ulong)m->stack
	&& (ulong)&l <= (ulong)m+BY2PG)
		estack = (ulong)m+MACHSIZE;
	else
		return;
	x += print("estackx %.8lux\n", estack);

	for(l=(ulong)&l; l<estack; l+=4){
		v = *(ulong*)l;
		if((KTZERO < v && v < (ulong)&etext) || estack-l<32){
			/*
			 * we could Pick off general CALL (((uchar*)v)[-5] == 0xE8)
			 * and CALL indirect through AX (((uchar*)v)[-2] == 0xFF && ((uchar*)v)[-2] == 0xD0),
			 * but this is too clever and misses faulting address.
			 */
			x += print("%.8lux=%.8lux ", l, v);
			i++;
		}
		if(i == 4){
			i = 0;
			x += print("\n");
		}
	}
	if(i)
		print("\n");
	print("EOF\n");
}

void
dumpstack(void)
{
	callwithureg(_dumpstack);
}

static void
debugbpt(Ureg* ureg, void*)
{
	if(up == 0)
		panic("kernel bpt");
	/* restore pc to instruction that caused the trap */
	ureg->pc--;
	postnote(up, 1, "sys: breakpoint", NDebug);
}

static void
doublefault(Ureg*, void*)
{
	panic("double fault");
}

static void
unexpected(Ureg* ureg, void*)
{
	print("unexpected trap %lud; ignoring\n", ureg->trap);
}

static void
fault386(Ureg* ureg, void*)
{
	ulong addr;
	int read;

	addr = HYPERVISOR_shared_info->vcpu_info[m->machno].arch.cr2;
	if (faultpanic) {
		dprint("cr2 is 0x%lx\n", addr);
		//dumpregs(ureg);
		dumpstack();
		panic("fault386");
	}
	if(!userureg(ureg) && mmukmapsync(addr))
		return;
	read = !(ureg->ecode & 2);
	if(fault(addr, ureg->pc, read) < 0){
		if(!userureg(ureg)){
			dumpregs(ureg);
			panic("kernel fault: %s addr=%#p", read? "read": "write", addr);
		}
		faultnote("fault", read? "read": "write", addr);
	}
	FAULTLOG(dprint("fault386: all done\n");)
}

/*
 *  Syscall is called directly from assembler without going through trap().
 */
void
syscall(Ureg* ureg)
{
	ulong scallnr;

	if(!kenter(ureg))
		panic("syscall: cs 0x%4.4luX", ureg->cs);
	scallnr = ureg->ax;
	dosyscall(scallnr, (Sargs*)(ureg->sp + BY2WD), &ureg->ax);
	if(up->procctl || up->nnote)
		donotify(ureg);
	/* if we delayed sched because we held a lock, sched now */
	if(up->delaysched)
		sched();
	kexit(ureg);
}

/*
 *  Call user, if necessary, with note.
 *  Pass user the Ureg struct and the note on his stack.
 */
Ureg*
notify(Ureg* ureg, char *msg)
{
	Ureg *nureg;
	ulong sp;

	sp = ureg->usp;
	sp -= sizeof(Ureg);

	if(!okaddr(sp-ERRMAX-4*BY2WD, sizeof(Ureg)+ERRMAX+4*BY2WD, 1))
		return nil;

	nureg = (Ureg*)sp;
	memmove(nureg, ureg, sizeof(Ureg));
	sp -= BY2WD+ERRMAX;
	memmove((char*)sp, msg, ERRMAX);
	sp -= 3*BY2WD;
	*(ulong*)(sp+2*BY2WD) = sp+3*BY2WD;		/* arg 2 is string */
	*(ulong*)(sp+1*BY2WD) = (ulong)nureg;		/* arg 1 is ureg* */
	*(ulong*)(sp+0*BY2WD) = 0;			/* arg 0 is pc */
	ureg->usp = sp;
	ureg->pc = (ulong)up->notify;
	return nureg;
}

/*
 *   Return user to state before notify()
 */
int
noted(Ureg *ureg, Ureg *nureg, int arg0)
{
	ulong oureg, sp;

	oureg = (ulong)nureg;

	/*
	 * Check the segment selectors are all valid, otherwise
	 * a fault will be taken on attempting to return to the
	 * user process.
	 * Take care with the comparisons as different processor
	 * generations push segment descriptors in different ways.
	 */
	if((nureg->cs & 0xFFFF) != UESEL || (nureg->ss & 0xFFFF) != UDSEL
	  || (nureg->ds & 0xFFFF) != UDSEL || (nureg->es & 0xFFFF) != UDSEL
	  || (nureg->fs & 0xFFFF) != UDSEL || (nureg->gs & 0xFFFF) != UDSEL){

		pprint("bad segment selector in noted\n");
		pprint("cs is %#lux, wanted %#ux\n", nureg->cs, UESEL);
		pprint("ds is %#lux, wanted %#ux\n", nureg->ds, UDSEL);
		pprint("es is %#lux, fs is %#lux, gs %#lux, wanted %#ux\n", 
			ureg->es, ureg->fs, ureg->gs, UDSEL);
		return -1;
	}

	setregisters(ureg, (char*)ureg, (char*)nureg, sizeof(Ureg));

	switch(arg0){
	case NCONT:
	case NRSTR:
		if(!okaddr(ureg->pc, 1, 0) || !okaddr(ureg->usp, BY2WD, 0))
			return -1;
		break;
	case NSAVE:
		sp = oureg-4*BY2WD-ERRMAX;
		if(!okaddr(ureg->pc, 1, 0) || !okaddr(sp, 4*BY2WD, 1))
			return -1;
		ureg->usp = sp;
		((ulong*)sp)[1] = oureg;	/* arg 1 0(FP) is ureg* */
		((ulong*)sp)[0] = 0;		/* arg 0 is pc */
		break;
	}
	return 0;
}

uintptr
execregs(uintptr entry, ulong ssize, ulong nargs)
{
	ulong *sp;
	Ureg *ureg;

	up->fpstate = FPinit;
	fpoff();

	sp = (ulong*)(USTKTOP - ssize);
	*--sp = nargs;

	ureg = up->dbgreg;
	ureg->usp = (ulong)sp;
	ureg->pc = entry;
//	print("execregs returns 0x%x\n", USTKTOP-sizeof(Tos));
	return USTKTOP-sizeof(Tos);		/* address of kernel/user shared data */
}

/*
 *  return the userpc the last exception happened at
 */
ulong
userpc(void)
{
	Ureg *ureg;

	ureg = (Ureg*)up->dbgreg;
	return ureg->pc;
}

/* This routine must save the values of registers the user is not permitted
 * to write from devproc and then restore the saved values before returning.
 */
void
setregisters(Ureg* ureg, char* pureg, char* uva, int n)
{
	ulong flags;
	ulong cs;
	ulong ss;

	flags = ureg->flags;
	cs = ureg->cs;
	ss = ureg->ss;
	memmove(pureg, uva, n);
	ureg->flags = (ureg->flags & 0x00FF) | (flags & 0xFF00);
	ureg->cs = cs;
	ureg->ss = ss;
}

void
kprocchild(Proc *p, void (*entry)(void))
{
	/*
	 * gotolabel() needs a word on the stack in
	 * which to place the return PC used to jump
	 * to linkproc().
	 */
	p->sched.pc = (ulong)entry;
	p->sched.sp = (ulong)p - BY2WD;
}

void
forkchild(Proc *p, Ureg *ureg)
{
	Ureg *cureg;

	/*
	 * Add 2*BY2WD to the stack to account for
	 *  - the return PC
	 *  - trap's argument (ur)
	 */
	p->sched.sp = (ulong)p - (sizeof(Ureg)+2*BY2WD);
	p->sched.pc = (ulong)forkret;

	cureg = (Ureg*)(p->sched.sp+2*BY2WD);
	memmove(cureg, ureg, sizeof(Ureg));
	/* return value of syscall in child */
	cureg->ax = 0;
}

/* Give enough context in the ureg to produce a kernel stack for
 * a sleeping process
 */
void
setkernur(Ureg* ureg, Proc* p)
{
	ureg->pc = p->sched.pc;
	ureg->sp = p->sched.sp+4;
}

ulong
dbgpc(Proc *p)
{
	Ureg *ureg;

	ureg = p->dbgreg;
	if(ureg == 0)
		return 0;

	return ureg->pc;
}

/*
 * install_safe_pf_handler / install_normal_pf_handler:
 * 
 * These are used within the failsafe_callback handler in entry.S to avoid
 * taking a full page fault when reloading FS and GS. This is because FS and 
 * GS could be invalid at pretty much any point while Xenolinux executes (we 
 * don't set them to safe values on entry to the kernel). At *any* point Xen 
 * may be entered due to a hardware interrupt --- on exit from Xen an invalid 
 * FS/GS will cause our failsafe_callback to be executed. This could occur, 
 * for example, while the mmu_update_queue is in an inconsistent state. This
 * is disastrous because the normal page-fault handler touches the update
 * queue!
 * 
 * Fortunately, within the failsafe handler it is safe to force DS/ES/FS/GS
 * to zero if they cannot be reloaded -- at this point executing a normal
 * page fault would not change this effect. The safe page-fault handler
 * ensures this end result (blow away the selector value) without the dangers
 * of the normal page-fault handler.
 * 
 * NB. Perhaps this can all go away after we have implemented writeable
 * page tables. :-)
 */
static void
safe_fault386(Ureg* , void* ) {
	panic("DO SAFE PAGE FAULT!\n");


   
}

unsigned long install_safe_pf_handler(void)
{
	dprint("called from failsafe callback\n");
	trapenable(VectorPF, safe_fault386, 0, "safe_fault386");
	return 0;
}

void install_normal_pf_handler(unsigned long)
{
	trapenable(VectorPF, fault386, 0, "fault386");
}
