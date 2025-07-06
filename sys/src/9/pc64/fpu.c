#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "ureg.h"
#include "io.h"

enum {
	CR4Osfxsr  = 1 << 9,
	CR4Oxmmex  = 1 << 10,
	CR4Oxsave  = 1 << 18,
};

/*
 * SIMD Floating Point.
 * Assembler support to get at the individual instructions
 * is in l.s.
 */
extern void _clts(void);
extern void _fldcw(u16int);
extern void _fnclex(void);
extern void _fninit(void);
extern void _fxrstor(void*);
extern void _fxsave(void*);
extern void _xrstor(void*);
extern void _xsave(void*);
extern void _xsaveopt(void*);
extern void _fwait(void);
extern void _ldmxcsr(u32int);
extern void _stts(void);

static void mathemu(Ureg *ureg, void*);

static void
fpssesave(FPsave *s)
{
	_fxsave(s);
	_stts();
}
static void
fpsserestore(FPsave *s)
{
	_clts();
	_fxrstor(s);
}

static void
fpxsave(FPsave *s)
{
	_xsave(s);
	_stts();
}
static void
fpxrestore(FPsave *s)
{
	_clts();
	_xrstor(s);
}

static void
fpxsaves(FPsave *s)
{
	_xsaveopt(s);
	_stts();
}
static void
fpxrestores(FPsave *s)
{
	_clts();
	_xrstor(s);
}

static void
fpxsaveopt(FPsave *s)
{
	_xsaveopt(s);
	_stts();
}

/*
 *  Turn the FPU on and initialise it for use.
 *  Set the precision and mask the exceptions
 *  we don't care about from the generic Mach value.
 */
void
fpinit(void)
{
	_clts();
	_fninit();
	_fwait();
	_fldcw(0x0232);
	_ldmxcsr(0x1900);
}

static char* mathmsg[] =
{
	nil,	/* handled below */
	"denormalized operand",
	"division by zero",
	"numeric overflow",
	"numeric underflow",
	"precision loss",
};

static void
mathnote(ulong status, uintptr pc, int kernel)
{
	char *msg, note[ERRMAX];
	int i;

	/*
	 * Some attention should probably be paid here to the
	 * exception masks and error summary.
	 */
	msg = "unknown exception";
	for(i = 1; i <= 5; i++){
		if(!((1<<i) & status))
			continue;
		msg = mathmsg[i];
		break;
	}
	if(status & 0x01){
		if(status & 0x40){
			if(status & 0x200)
				msg = "stack overflow";
			else
				msg = "stack underflow";
		}else
			msg = "invalid operation";
	}
	snprint(note, sizeof note, "sys: fp: %s fppc=%#p status=0x%lux", msg, pc, status);
	if(kernel)
		panic("%s", note);

	postnote(up, 1, note, NDebug);
}

/*
 *  math coprocessor error
 */
static void
matherror(Ureg *ureg, void*)
{
	if(!userureg(ureg)){
		if(up == nil)
			mathnote(m->fpsave->fsw, m->fpsave->rip, 1);
		else
			mathnote(up->kfpsave->fsw, up->kfpsave->rip, 1);
		return;
	}
	if(up->fpstate != FPinactive){
		_clts();
		fpsave(up->fpsave);
		up->fpstate = FPinactive;
	}
	mathnote(up->fpsave->fsw, up->fpsave->rip, 0);
}

/*
 *  SIMD error
 */
static void
simderror(Ureg *ureg, void*)
{
	if(!userureg(ureg)){
		if(up == nil)
			mathnote(m->fpsave->mxcsr & 0x3f, ureg->pc, 1);
		else
			mathnote(up->kfpsave->mxcsr & 0x3f, ureg->pc, 1);
		return;
	}
	if(up->fpstate != FPinactive){
		_clts();
		fpsave(up->fpsave);
		up->fpstate = FPinactive;
	}
	mathnote(up->fpsave->mxcsr & 0x3f, ureg->pc, 0);
}

/*
 *  math coprocessor segment overrun
 */
static void
mathover(Ureg *ureg, void*)
{
	if(!userureg(ureg))
		panic("math overrun");

	pexit("math overrun", 0);
}

void
mathinit(void)
{
	trapenable(VectorCERR, matherror, 0, "matherror");
	if(m->cpuidfamily == 3)
		intrenable(IrqIRQ13, matherror, 0, BUSUNKNOWN, "matherror");
	trapenable(VectorCNA, mathemu, 0, "mathemu");
	trapenable(VectorCSO, mathover, 0, "mathover");
	trapenable(VectorSIMD, simderror, 0, "simderror");
}

/*
 *  fpuinit(), called from cpuidentify() for each cpu.
 */
void
fpuinit(void)
{
	u64int cr4;
	ulong regs[4];

	m->xcr0 = 0;
	cr4 = getcr4() | CR4Osfxsr|CR4Oxmmex;
	if((m->cpuidcx & (Xsave|Avx)) == (Xsave|Avx) && getconf("*noavx") == nil){
		cr4 |= CR4Oxsave;
		putcr4(cr4);

		m->xcr0 = 7;	/* x87, sse, avx */
		putxcr0(m->xcr0);

		cpuid(0xd, 1, regs);
		if(regs[0] & Xsaves){
			fpsave = fpxsaves;
			fprestore = fpxrestores;
		} else {
			if(regs[0] & Xsaveopt)
				fpsave = fpxsaveopt;
			else
				fpsave = fpxsave;
			fprestore = fpxrestore;
		}
	} else {
		cr4 &= ~CR4Oxsave;
		putcr4(cr4);

		fpsave = fpssesave;
		fprestore = fpsserestore;
	}

	m->fpsave = nil;
	m->fpstate = FPinit;
	_stts();
}

static FPalloc*
fpalloc(FPalloc *link)
{
	FPalloc *a;

	while((a = mallocalign(sizeof(FPalloc), FPalign, 0, 0)) == nil){
		int x = spllo();
		if(up != nil && !waserror()){
			resrcwait("no memory for FPalloc");
			poperror();
		}
		splx(x);
	}
	a->link = link;
	return a;
}

static void
fpfree(FPalloc *a)
{
	free(a);
}

void
fpuprocsetup(Proc *p)
{
	FPalloc *a;

	p->fpstate = FPinit;
	while((a = p->fpsave) != nil){
		p->fpsave = a->link;
		fpfree(a);
	}
}

void
fpuprocfork(Proc *p)
{
	int s;

	s = splhi();
	switch(up->fpstate & ~FPnotify){
	case FPprotected:
		_clts();
		/* wet floor */
	case FPactive:
		fpsave(up->fpsave);
		up->fpstate = FPinactive;
		/* wet floor */
	case FPinactive:
		if(p->fpsave == nil)
			p->fpsave = fpalloc(nil);
		memmove(p->fpsave, up->fpsave, sizeof(FPsave));
		p->fpstate = FPinactive;
	}
	splx(s);
}

void
fpuprocsave(Proc *p)
{
	if(p->state == Moribund){
		FPalloc *a;

		if(p->fpstate == FPactive || p->kfpstate == FPactive){
			_fnclex();
			_stts();
		}
		p->fpstate = p->kfpstate = FPinit;
		while((a = p->fpsave) != nil){
			p->fpsave = a->link;
			fpfree(a);
		}
		while((a = p->kfpsave) != nil){
			p->kfpsave = a->link;
			fpfree(a);
		}
		return;
	}
	if(p->kfpstate == FPactive){
		fpsave(p->kfpsave);
		p->kfpstate = FPinactive;
		return;
	}
	if(p->fpstate == FPprotected)
		_clts();
	else if(p->fpstate != FPactive)
		return;
	fpsave(p->fpsave);
	p->fpstate = FPinactive;
}

void
fpuprocrestore(Proc*)
{
	/*
	 * when the scheduler switches,
	 * we can discard its fp state.
	 */
	switch(m->fpstate){
	case FPactive:
		_fnclex();
		_stts();
		/* wet floor */
	case FPinactive:
		fpfree(m->fpsave);
		m->fpsave = nil;
		m->fpstate = FPinit;
	}
}

/*
 *  Protect or save FPU state and setup new state
 *  (lazily in the case of user process) for the kernel.
 *  All syscalls, traps and interrupts (except mathemu()!)
 *  are handled between fpukenter() and fpukexit(),
 *  so they can use floating point and vector instructions.
 */
void
fpukenter(Ureg *)
{
	if(up == nil){
		switch(m->fpstate){
		case FPactive:
			fpsave(m->fpsave);
			/* wet floor */
		case FPinactive:
			m->fpstate = FPinit;
		}
		return;
	}

	switch(up->fpstate){
	case FPactive:
		up->fpstate = FPprotected;
		_stts();
		/* wet floor */
	case FPprotected:
		return;
	}

	switch(up->kfpstate){
	case FPactive:
		fpsave(up->kfpsave);
		/* wet floor */
	case FPinactive:
		up->kfpstate = FPinit;
	}
}

void
fpukexit(Ureg *ureg)
{
	FPalloc *a;

	if(up == nil){
		switch(m->fpstate){
		case FPactive:
			_fnclex();
			_stts();
			/* wet floor */
		case FPinactive:
			a = m->fpsave;
			m->fpsave = a->link;
			fpfree(a);
		}
		m->fpstate = m->fpsave != nil? FPinactive: FPinit;
		return;
	}

	if(up->fpstate == FPprotected){
		if(userureg(ureg)){
			up->fpstate = FPactive;
			_clts();
		}
		return;
	}

	switch(up->kfpstate){
	case FPactive:
		_fnclex();
		_stts();
		/* wet floor */
	case FPinactive:
		a = up->kfpsave;
		up->kfpsave = a->link;
		fpfree(a);
	}
	up->kfpstate = up->kfpsave != nil? FPinactive: FPinit;
}

void
fpunotify(Proc *p)
{
	fpuprocsave(p);
	p->fpstate |= FPnotify;
}

void
fpunoted(Proc *p)
{
	FPalloc *o;

	if(p->fpstate & FPnotify) {
		p->fpstate &= ~FPnotify;
	} else if((o = p->fpsave->link) != nil) {
		fpfree(p->fpsave);
		p->fpsave = o;
		p->fpstate = FPinactive;
	} else {
		p->fpstate = FPinit;
	}
}

FPsave*
notefpsave(Proc *p)
{
	if(p->fpsave == nil)
		return nil;
	if(p->fpstate == (FPinactive|FPnotify)){
		p->fpsave = fpalloc(p->fpsave);
		memmove(p->fpsave, p->fpsave->link, sizeof(FPsave));
		p->fpstate = FPinactive;
	}
	return p->fpsave->link;
}

/*
 *  Before restoring the state, check for any pending
 *  exceptions, there's no way to restore the state without
 *  generating an unmasked exception.
 *  More attention should probably be paid here to the
 *  exception masks and error summary.
 */
static int
fpcheck(FPsave *save, int kernel)
{
	ulong status, control;

	status = save->fsw;
	control = save->fcw;
	if((status & ~control) & 0x07F){
		mathnote(status, save->rip, kernel);
		return 1;
	}
	return 0;
}

/*
 *  math coprocessor emulation fault
 */
static void
mathemu(Ureg *ureg, void*)
{
	if(!userureg(ureg)){
		if(up == nil){
			switch(m->fpstate){
			case FPinit:
				m->fpsave = fpalloc(m->fpsave);
				m->fpstate = FPactive;
				fpinit();
				break;
			case FPinactive:
				fpcheck(m->fpsave, 1);
				fprestore(m->fpsave);
				m->fpstate = FPactive;
				break;
			default:
				panic("floating point error in irq");
			}
			return;
		}

		if(up->fpstate == FPprotected){
			_clts();
			fpsave(up->fpsave);
			up->fpstate = FPinactive;
		}

		switch(up->kfpstate){
		case FPinit:
			up->kfpsave = fpalloc(up->kfpsave);
			up->kfpstate = FPactive;
			fpinit();
			break;
		case FPinactive:
			fpcheck(up->kfpsave, 1);
			fprestore(up->kfpsave);
			up->kfpstate = FPactive;
			break;
		default:
			panic("floating point error in trap");
		}
		return;
	}

	switch(up->fpstate){
	case FPinit|FPnotify:
		/* wet floor */
	case FPinit:
		if(up->fpsave == nil)
			up->fpsave = fpalloc(nil);
		up->fpstate = FPactive;
		fpinit();
		break;
	case FPinactive|FPnotify:
		spllo();
		qlock(&up->debug);
		notefpsave(up);
		qunlock(&up->debug);
		splhi();
		/* wet floor */
	case FPinactive:
		if(fpcheck(up->fpsave, 0))
			break;
		fprestore(up->fpsave);
		up->fpstate = FPactive;
		break;
	case FPprotected:
		up->fpstate = FPactive;
		_clts();
		break;
	case FPactive:
		postnote(up, 1, "sys: floating point error", NDebug);
		break;
	}
}
