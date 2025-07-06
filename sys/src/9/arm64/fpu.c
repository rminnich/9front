#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "ureg.h"
#include "../arm64/sysreg.h"

/* libc */
extern ulong getfcr(void);
extern void setfcr(ulong fcr);
extern ulong getfsr(void);
extern void setfsr(ulong fsr);

static FPsave fpsave0;

static void
fpsave(FPsave *p)
{
	p->control = getfcr();
	p->status = getfsr();
	fpsaveregs(p->regs);
	fpoff();
}

static void
fprestore(FPsave *p)
{
	fpon();
	setfcr(p->control);
	setfsr(p->status);
	fploadregs(p->regs);
}

static void
fpinit(void)
{
	fprestore(&fpsave0);
}

void
fpuinit(void)
{
	m->fpstate = FPinit;
	m->fpsave = nil;
	fpoff();
}

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
fpfree(FPalloc *a)
{
	free(a);
}


/*
 *  Protect or save FPU state and setup new state
 *  (lazily in the case of user process) for the kernel.
 *  All syscalls, traps and interrupts (except mathtrap()!)
 *  are handled between fpukenter() and fpukexit(),
 *  so they can use floating point and vector instructions.
 */
void
fpukenter(Ureg*)
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
		fpoff();
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

void
fpuprocsetup(Proc *p)
{
	FPalloc *a;

	p->fpstate = FPinit;
	while((a = p->fpsave) != nil) {
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
		fpon();
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
