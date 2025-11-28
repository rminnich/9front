#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "ureg.h"
#include "../riscv64/sysreg.h"

void fpoff(void)
{
	panic("fpoff");
}

FPsave*
notefpsave(Proc *p)
{
	panic("notefpsave");
#ifdef XXX
	if(p->fpsave == nil)
		return nil;
	if(p->fpstate == (FPinactive|FPnotify)){
		p->fpsave = fpalloc(p->fpsave);
		memmove(p->fpsave, p->fpsave->link, sizeof(FPsave));
		p->fpstate = FPinactive;
	}
	return p->fpsave->link;
#endif
	return nil;
}

void
fpuprocsave(Proc *p)
{
	panic("fpuprocsave");
#ifdef XXX
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
#endif
}

void fpfree(FPsave	*)
{
	panic("fpfree");
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
	panic("fpnoted");
/*
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
*/
}

void
mathtrap(Ureg *ureg)
{
	panic("mathtrap");
#ifdef XXX
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
#endif
}
