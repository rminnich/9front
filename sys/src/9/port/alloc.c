#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"error.h"
#include	<pool.h>

static void poolprint(Pool*, char*, ...);
static void ppanic(Pool*, char*, ...);
static void plock(Pool*);
static void punlock(Pool*);

typedef struct Private	Private;
struct Private {
	Lock		lk;
	char		msg[1024];	/* a rock for messages to be printed at unlock */
};

static Private pmainpriv;
static Pool pmainmem = {
	.name=	"Main",
	.maxsize=	4*1024*1024,
	.minarena=	128*1024,
	.quantum=	32,
	.alloc=	xalloc,
	.merge=	xmerge,
	.flags=	POOL_TOLERANCE | POOL_NOREUSE,

	.lock=	plock,
	.unlock=	punlock,
	.print=	poolprint,
	.panic=	ppanic,

	.private=	&pmainpriv,
};

static Private pimagpriv;
static Pool pimagmem = {
	.name=	"Image",
	.maxsize=	16*1024*1024,
	.minarena=	2*1024*1024,
	.quantum=	32,
	.alloc=	xalloc,
	.merge=	xmerge,
	.flags=	0,

	.lock=	plock,
	.unlock=	punlock,
	.print=	poolprint,
	.panic=	ppanic,

	.private=	&pimagpriv,
};

static Private psecrpriv;
static Pool psecrmem = {
	.name=	"Secrets",
	.maxsize=	16*1024*1024,
	.minarena=	64*1024,
	.quantum=	32,
	.alloc=	xalloc,
	.merge=	xmerge,
	.flags=	POOL_ANTAGONISM,

	.lock=	plock,
	.unlock=	punlock,
	.print=	poolprint,
	.panic=	ppanic,

	.private=	&psecrpriv,
};

Pool*	mainmem = &pmainmem;
Pool*	imagmem = &pimagmem;
Pool*	secrmem = &psecrmem;

/*
 * because we can't print while we're holding the locks, 
 * we have the save the message and print it once we let go.
 */
static void
poolprint(Pool *p, char *fmt, ...)
{
	va_list v;
	Private *pv;

	pv = p->private;
	va_start(v, fmt);
	vseprint(pv->msg+strlen(pv->msg), pv->msg+sizeof pv->msg, fmt, v);
	va_end(v);
}

static void
ppanic(Pool *p, char *fmt, ...)
{
	va_list v;
	Private *pv;
	char msg[sizeof pv->msg];
if (0)print("ENTER POOL PANIC\n");
	pv = p->private;
	va_start(v, fmt);
	vseprint(pv->msg+strlen(pv->msg), pv->msg+sizeof pv->msg, fmt, v);
	va_end(v);
	memmove(msg, pv->msg, sizeof msg);
	iunlock(&pv->lk);
	panic("%s", msg);
}

static void
plock(Pool *p)
{
	Private *pv;
if (0)print("PLOCK %p mainmem %p\n", p, mainmem);
	pv = p->private;
	ilock(&pv->lk);
	pv->lk.pc = getcallerpc(&p);
	pv->msg[0] = 0;
if (0)print("plocked\n");
}

static void
punlock(Pool *p)
{
	Private *pv;
if (0)print("punlock\n");
	//char msg[sizeof pv->msg];

	pv = p->private;
	if(pv->msg[0] == 0){
		iunlock(&pv->lk);
		return;
	}

	//memmove(msg, pv->msg, sizeof msg);
	iunlock(&pv->lk);
	int i;
	for(i = 0; i < sizeof pv->msg; i++) {
		if (pv->msg[i] == 0) break;
		sbiputc(pv->msg[i]);
	}
	//iprint("%.*s", sizeof pv->msg, msg);
}

void
poolsummary(Pool *p)
{
if (0)print("%s max %llud cur %llud free %llud alloc %llud\n", p->name,
		(uvlong)p->maxsize, (uvlong)p->cursize,
		(uvlong)p->curfree, (uvlong)p->curalloc);
}

void
mallocsummary(void)
{
	poolsummary(mainmem);
	poolsummary(imagmem);
	poolsummary(secrmem);
}

/* everything from here down should be the same in libc, libdebugmalloc, and the kernel */
/* - except the code for malloc(), which alternately doesn't clear or does. */
/* - except the code for smalloc(), which lives only in the kernel. */

/*
 * Npadlong is the number of ulong's to leave at the beginning of 
 * each allocated buffer for our own bookkeeping.  We return to the callers
 * a pointer that points immediately after our bookkeeping area.  Incoming pointers
 * must be decremented by that much, and outgoing pointers incremented.
 * The malloc tag is stored at MallocOffset from the beginning of the block,
 * and the realloc tag at ReallocOffset.  The offsets are from the true beginning
 * of the block, not the beginning the caller sees.
 *
 * The extra if(Npadlong != 0) in various places is a hint for the compiler to
 * compile out function calls that would otherwise be no-ops.
 */

/*	non tracing
 *
enum {
	Npadlong = 0,
	MallocOffset = 0,
	ReallocOffset = 0,
};
 *
 */

/* tracing */
enum {
	Npadlong = 2,
	MallocOffset = 0,
	ReallocOffset = 1
};

/*
static void mmemset(void *v, char val, int size) {
	u8int *uc = v;
	int i;
	for(i = 0; i < size; i++) uc[i] = val;
}
*/
void*
smalloc(ulong size)
{
	void *v;
if (0)print("SMALLOC %d\n", size);
	if (0)poolcheck(mainmem);
	while((v = poolalloc(mainmem, size+Npadlong*sizeof(ulong))) == nil){
		if(!waserror()){
			resrcwait("no memory for smalloc");
			poperror();
		}
	}
	if(Npadlong){
		v = (ulong*)v+Npadlong;
		setmalloctag(v, getcallerpc(&size));
	}
	memset(v, 0, size);
if (0)print("SMALLOC RETURNS %p\n", v);
	return v;
}

void*
malloc(ulong size)
{
	void *v;
	extern int debugmemset;
if (0)print("MALLOC %d\n", size);
	v = poolalloc(mainmem, size+Npadlong*sizeof(ulong));
if (0)poolcheck(mainmem);
	if(v == nil)
		return nil;
	if(Npadlong){
		v = (ulong*)v+Npadlong;
		setmalloctag(v, getcallerpc(&size));
		setrealloctag(v, 0);
	}
	if (0)print("memset @ %p for %d bytes\n", v, size);
	if (0) while(debugmemset);
	memset(v, 0, size);
	if (1) {
		int i;
		for(i = 0; i < size; i++) {
			if (((char *)v)[i] != 0) {
			if (0)print("FAIL at index %d\n", i);
			}
		}
	}
if (0)print("MALLOC RETURNS %p\n", v);
	return v;
}

void*
mallocz(ulong size, int clr)
{
	void *v;
if (0)poolcheck(mainmem);
	v = poolalloc(mainmem, size+Npadlong*sizeof(ulong));
	if(v == nil)
		return nil;
	if(Npadlong){
		v = (ulong*)v+Npadlong;
		setmalloctag(v, getcallerpc(&size));
		setrealloctag(v, 0);
	}
	if(clr)
		memset(v, 0, size);
	return v;
}

void*
mallocalign(ulong size, ulong align, long offset, ulong span)
{
	void *v;

	v = poolallocalign(mainmem, size+Npadlong*sizeof(ulong), align, offset-Npadlong*sizeof(ulong), span);
	if(v == nil)
		return nil;
	if(Npadlong){
		v = (ulong*)v+Npadlong;
		setmalloctag(v, getcallerpc(&size));
		setrealloctag(v, 0);
	}
	memset(v, 0, size);
	return v;
}

void
free(void *v)
{
if (0)print("Don't free %p\n", v);
	if(v != nil)
		poolfree(mainmem, (ulong*)v-Npadlong);
}

void*
realloc(void *v, ulong size)
{
	void *nv;
if (0)print("realloc %p %u\n", v, size);

	if(v != nil)
		v = (ulong*)v-Npadlong;
	if(Npadlong && size != 0)
		size += Npadlong*sizeof(ulong);
	nv = poolrealloc(mainmem, v, size);
	if(nv != nil){
		nv = (ulong*)nv+Npadlong;
		setrealloctag(nv, getcallerpc(&v));
		if(v == nil)
			setmalloctag(nv, getcallerpc(&v));
	}		
	return nv;
}

ulong
msize(void *v)
{
	return poolmsize(mainmem, (ulong*)v-Npadlong)-Npadlong*sizeof(ulong);
}

/* secret memory, used to back cryptographic keys and cipher states */
void*
secalloc(ulong size)
{
	void *v;

	while((v = poolalloc(secrmem, size+Npadlong*sizeof(ulong))) == nil){
		if(!waserror()){
			resrcwait("no memory for secalloc");
			poperror();
		}
	}
	if(Npadlong){
		v = (ulong*)v+Npadlong;
		setmalloctag(v, getcallerpc(&size));
		setrealloctag(v, 0);
	}
	memset(v, 0, size);
	return v;
}

void
secfree(void *v)
{
	if(v != nil)
		poolfree(secrmem, (ulong*)v-Npadlong);
}

void
setmalloctag(void *v, uintptr pc)
{
	USED(v, pc);
	if(Npadlong <= MallocOffset || v == nil)
		return;
	((ulong*)v)[-Npadlong+MallocOffset] = (ulong)pc;
}

void
setrealloctag(void *v, uintptr pc)
{
	USED(v, pc);
	if(Npadlong <= ReallocOffset || v == nil)
		return;
	((ulong*)v)[-Npadlong+ReallocOffset] = (ulong)pc;
}

uintptr
getmalloctag(void *v)
{
	USED(v);
	if(Npadlong <= MallocOffset)
		return ~0;
	return (int)((ulong*)v)[-Npadlong+MallocOffset];
}

uintptr
getrealloctag(void *v)
{
	USED(v);
	if(Npadlong <= ReallocOffset)
		return ~0;
	return (int)((ulong*)v)[-Npadlong+ReallocOffset];
}

void check(void) {
	pooldump(mainmem);
	poolcheck(mainmem);
}
