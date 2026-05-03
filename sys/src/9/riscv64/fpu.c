#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "ureg.h"
#include "../riscv64/sysreg.h"

/* l.s, for now */
extern ulong frrm(void);
extern void fsrm(ulong fcr);
extern ulong frflags(void);
extern void fsflags(ulong fsr);

/* from 9k */
enum {						/* FCSR */
	I		= FPAINEX,
	D		= FPAINVAL,		/* Denormalized-Operand */
	Z		= FPAZDIV,
	O		= FPAOVFL,		/* Overflow */
	U		= FPAUNFL,		/* Underflow */
};

enum {						/* PFPU.state */
	Init		= 0,			/* The FPU has not been used */
	Busy		= 1,			/* The FPU is being used */
	Idle		= 2,			/* The FPU has been used */

	Hold		= 1<<2,			/* Handling an FPU note */
};

static char *fpstnames[] = {
	"Init", "Busy", "Idle",
};

/* initial contents of high fp regs.  compiler assumes such initialization. */
double fpzero = 0, fphalf = 0.5, fpone = 1, fptwo = 2;
/* thank you 9k! */

static FPalloc*
fpalloc(FPalloc *link)
{
	FPalloc *a;

	while((a = mallocalign(sizeof(FPalloc), 16, 0, 0)) == nil){
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
fpfree(FPalloc*a)
{
	free(a);
}

static FPsave fpsave0;

static void
fpsave(FPsave *p)
{
	p->control = frrm();
	p->status = frflags();
	fpsaveregs(p->regs);
	fpoff();
}

static void
fprestore(FPsave *p)
{
	fpon();
	fsrm(p->control);
	fsflags(p->status);
	fploadregs(p->regs);
}

static void
fpinit(void)
{
	fprestore(&fpsave0);
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
	return nil;
}

void
fpuprocsave(Proc *p)
{
	if(p->state == Moribund){
		FPalloc *a;

		if(p->fpstate == FPactive || p->kfpstate == FPactive)
			fpoff();
		p->fpstate = p->kfpstate = FPinit;
		while((a = p->fpsave) != nil) {
			p->fpsave = a->link;
			fpfree(a);
		}
		while((a = p->kfpsave) != nil) {
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
		fpon();
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
		fpoff();
		/* wet floor */
	case FPinactive:
		fpfree(m->fpsave);
		m->fpsave = nil;
		m->fpstate = FPinit;
	}
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

void
mathtrap(Ureg *ureg)
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
				fprestore(m->fpsave);
				m->fpstate = FPactive;
				break;
			default:
				panic("floating point error in irq");
			}
			return;
		}

		if(up->fpstate == FPprotected){
			fpon();
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
		fprestore(up->fpsave);
		up->fpstate = FPactive;
		break;
	case FPprotected:
		up->fpstate = FPactive;
		fpon();
		break;
	case FPactive:
		postnote(up, 1, "sys: floating point error", NDebug);
		break;
	}
}

void fpukexit(Ureg*ureg, FPsave*)
{
	FPalloc *a;

	if(up == nil){
		switch(m->fpstate){
		case FPactive:
			fpoff();
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
			fpon();
		}
		return;
	}

	switch(up->kfpstate){
	case FPactive:
		fpoff();
		/* wet floor */
	case FPinactive:
		a = up->kfpsave;
		up->kfpsave = a->link;
		fpfree(a);
	}
	up->kfpstate = up->kfpsave != nil? FPinactive: FPinit;
}
