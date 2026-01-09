#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

/*
 * The initcode array contains the binary text of the first
 * user process. Its job is to invoke the exec system call
 * for /boot/boot.
 * Initcode does not link with standard plan9 libc _main()
 * trampoline due to size constrains. Instead it is linked
 * with a small machine specific trampoline init9.s that
 * only sets the base address register and passes arguments
 * to startboot() (see port/initcode.c).
 */
#include	"initcode.i"

/*
 * The first process kernel process starts here.
 */
static void
proc0(void*)
{
	KMap *k;
	Page *p;

	print("proc0\n");
	spllo();
	if(waserror()){
		print("it went badly: %s\n", up->errstr);
		panic("proc0: %s", up->errstr);
	}

	up->pgrp = newpgrp();
	up->egrp = smalloc(sizeof(Egrp));
	up->egrp->ref = 1;
	up->fgrp = dupfgrp(nil);
	up->rgrp = newrgrp();
	print("rgrp\n");
	/*
	 * These are o.k. because rootinit is null.
	 * Then early kproc's will have a root and dot.
	 */
	up->slash = namec("#/", Atodir, 0, 0);
	print("done slash\n");
	pathclose(up->slash->path);
	print("pathclose\n");
	up->slash->path = newpath("/");
	print("newpath\n");
	up->dot = cclone(up->slash);
	print("DONE dot\n");

	/*
	 * Setup Text and Stack segments for initcode.
	 */
	print("USTKTPO %p base of stack %p size 0x%lx\n", USTKTOP, USTKTOP-USTKSIZE, USTKSIZE / BY2PG);
	up->seg[SSEG] = newseg(SG_STACK | SG_NOEXEC, USTKTOP-USTKSIZE, USTKSIZE / BY2PG);
	up->seg[TSEG] = newseg(SG_TEXT | SG_RONLY, UTZERO, 1);
	up->seg[TSEG]->flushme = 1;
	p = newpage(UTZERO, nil);
	print("newpage %p @ %p\n", p, UTZERO);
	k = kmap(p);
	print("kmap of %p is %p\n", p, k);
	print("seg and kmap done\n");
	print("init code installed, memmov to %p VA %p from %p for %d bytes\\n", k, VA(k), initcode, sizeof(initcode));
	extern int block;
	if (0)while(! block);
	memmove((uchar*)VA(k), initcode, sizeof(initcode));
	print("init code installed, memset %p VA %p for %ld bytes\\n", k, VA(k), BY2PG-sizeof(initcode));
	memset((uchar*)VA(k)+sizeof(initcode), 0, BY2PG-sizeof(initcode));
	print("umem zerod\n");
	kunmap(k);
	print("kernel unmapped, page in %p\n", p);
	segpage(up->seg[TSEG], p);
	//putmmu(p->va, p->pa, p);
	//print("TSEG paged in, now page in  USTKTOP %p\n", USTKTOP);
	//segpage(up->seg[SSEG], (void *)(USTKTOP));
	print("post TSEG\n");
	// NO. 
	//p = newpage(USTKTOP-BY2PG, nil);
	//segpage(up->seg[SSEG], p);
	//putmmu(p->va, p->pa, p); (mmu is the wrong one ...)
	/*
	 * Become a user process.
	 */
	print("become a user process\n");
	up->kp = 0;
	up->noswap = 0;
	up->privatemem = 0;
	print("procprio...\n");
	procpriority(up, PriNormal, 0);
	print("procsetup...\n");
	procsetup(up);
	print("call flushmmu...\n");
	flushmmu();
	print("poperror...\n");
	poperror();

	/*
	 * init0():
	 *	call chandevinit()
	 *	setup environment variables
	 *	prepare the stack for initcode
	 *	switch to usermode to run initcode
	 */
	print("call init0\n");
	init0();

	/* init0 will never return */
	panic("init0");
}

void
userinit(void)
{
	up = nil;
	kstrdup(&eve, "");
	kproc("*init*", proc0, nil);
}
