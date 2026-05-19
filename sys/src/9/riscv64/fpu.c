#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "ureg.h"
#include "../riscv64/sysreg.h"
#include "riscv64.h"

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
	Hold		= 1<<2,			/* Handling an FPU note */
};

static char *fpstnames[] = {
	"Init", "Inactive", "Clean", "Dirty",
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

/* These two functions arrange for FPU to be in the proper state
 * when we exit the kernel. */
static void
procfpon(Proc *p)
{
	p->dbgreg->status |= Fsst;
}

static void
procfpoff(Proc *p)
{
	p->dbgreg->status &= ~Fsst;
}

static void
fpsave(FPsave *p)
{
	print("fpsave\n");
	p->control = frrm();
	p->status = frflags();
	fpsaveregs(p->regs);
	fpoff();
}

static void
fprestore(FPsave *p)
{
	print("fprestore\n");
	fpon();
	fsrm(p->control);
	fsflags(p->status);
	fploadregs(p->regs);
}

static void
fpinit(void)
{
	print("fpinit\n");
	fprestore(&fpsave0);
}

FPsave*
notefpsave(Proc *p)
{
	if(p->fpsave == nil)
		return nil;
	if(p->fpstate == (FPidle|FPnotify)){
		p->fpsave = fpalloc(p->fpsave);
		memmove(p->fpsave, p->fpsave->link, sizeof(FPsave));
		p->fpstate = FPidle;
	}
	return p->fpsave->link;
}

void
fpuprocsave(Proc *p)
{
	print("fpuprocsave\n");
	if(p->state == Moribund){
		FPalloc *a;

		if(p->fpstate > FPinit || p->kfpstate > FPinit)
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
	if(p->kfpstate > FPinit){
		fpsave(p->kfpsave);
		p->kfpstate = FPidle;
		return;
	}
	if(p->fpstate > FPinit)
		procfpon(p);
	else
		return;
	fpsave(p->fpsave);
	p->fpstate = FPidle;
}

void
fpuprocrestore(Proc*p)
{
	print("fpuprocrestore\n");
	/*
	 * when the scheduler switches,
	 * we can discard its fp state.
	 */
	switch(m->fpstate){
	case FPclean:
	case FPdirty:
	case FPidle:
		/* wet floor */
		fpoff();
		procfpoff(p);
		fpfree(m->fpsave);
		m->fpsave = nil;
		m->fpstate = FPinit;
	}
}

void
fpuprocfork(Proc *p)
{
	int s;

	s = splhi();
	switch(up->fpstate & ~FPnotify){
	case FPclean:
	case FPdirty:
		procfpon(p);
		fpsave(up->fpsave);
		up->fpstate = FPidle;
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
		p->fpstate = FPidle;
	} else {
		p->fpstate = FPinit;
	}
}

void
mathtrap(Ureg *ureg)
{
	print("mathtrap\n");
	if(!userureg(ureg)){
		print("kernel mode\n");
		if(up == nil){
			switch(m->fpstate){
			case FPinit:
				m->fpsave = fpalloc(m->fpsave);
				m->fpstate = FPidle;
				fpinit();
				break;
			case FPidle:
			/* do we need the other cases here? */
				fprestore(m->fpsave);
				m->fpstate = FPidle;
				break;
			default:
				panic("floating point error in irq for FP state %d", m->fpstate);
			}
			return;
		}
		print("fpstate %d\n", up->fpstate);
		if(up->fpstate > FPclean){
			print("fp dirty. save it and mark it on\n");
			procfpon(up);
			fpsave(up->fpsave);
			up->fpstate = FPidle;
		}

		switch(up->kfpstate){
		case FPinit:
			up->kfpsave = fpalloc(up->kfpsave);
			up->kfpstate = FPidle;
			fpinit();
			break;
		case FPidle:
			fprestore(up->kfpsave);
			up->kfpstate = FPidle;
			break;
		default:
			panic("floating point error in trap");
		}
		return;
	}

	print("user trap: up -> fpstate %d\n", up->fpstate);
	switch(up->fpstate){
	case FPinit|FPnotify:
		/* wet floor */
	case FPinit:
		print("init: turn it on\n");
		if(up->fpsave == nil)
			up->fpsave = fpalloc(nil);
		up->fpstate = FPidle;
		procfpon(up);
		fpinit();
		break;
	case FPidle|FPnotify:
		print("idle | notify\n");
		spllo();
		qlock(&up->debug);
		notefpsave(up);
		qunlock(&up->debug);
		splhi();
		/* wet floor */
	case FPidle:
		print("idle\n");
		fprestore(up->fpsave);
		up->fpstate = FPidle;
		procfpon(up);
		break;
	/* this seems the wrong thing, but I don't know what I'm doing. */
	case FPclean:
	case FPdirty:
		print("clean | dirty\n");
		postnote(up, 1, "sys: floating point error", NDebug);
		break;
	}
	print("isfpon: %s\n", up->dbgreg->status & Fsst ? "yes" : "no");
}

void fpukexit(Ureg*ureg, FPsave*)
{
	FPalloc *a;
	print("fpukexit\n");

	/* if there is no process, just turn the FPU off. */
	if(up == nil){
		/* if it was used by the kernel, save state */
		switch(m->fpstate){
		case FPclean:
		case FPdirty:
		case FPidle:
			/* wet floor */
			fpoff();
			a = m->fpsave;
			m->fpsave = a->link;
			fpfree(a);
		}
		m->fpstate = m->fpsave != nil? FPidle: FPinit;
		return;
	}

	/* this is a very conservative decision. At the same time, we're turning it on,
	 * so it is inactive (but will be restored). */
	if(up->fpstate > FPinit){
		if(userureg(ureg)){
			up->fpstate = FPidle;
			procfpon(up);
		}
		return;
	}

	switch(up->kfpstate){
		case FPclean:
		case FPdirty:
		fpoff();
		/* wet floor */
	case FPidle:
		a = up->kfpsave;
		up->kfpsave = a->link;
		fpfree(a);
	}
	up->kfpstate = up->kfpsave != nil? FPidle: FPinit;
}
