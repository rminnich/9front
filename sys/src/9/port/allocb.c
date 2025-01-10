#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"error.h"

enum
{
	Hdrspc		= 64,		/* leave room for high-level headers */
	Tlrspc		= 16,		/* extra room at the end for pad/crc/mac */
	Bdead		= 0x51494F42,	/* "QIOB" */
};

static Block*
_allocb(ulong size, ulong align)
{
	Block *b;

	size = ROUND(size+Tlrspc, align);
	if((b = mallocz(sizeof(Block)+Hdrspc+size+align-1, 0)) == nil)
		return nil;

	b->next = nil;
	b->list = nil;
	b->pool = nil;
	b->flag = 0;

	/* align start of data portion by rounding up */
	b->base = (uchar*)ROUND((uintptr)&b[1], (uintptr)align);

	/* align end of data portion by rounding down */
	b->lim = (uchar*)(((uintptr)b + msize(b)) & ~((uintptr)align-1));

	/* leave room at beginning for added headers */
	b->wp = b->rp = b->lim - size;

	return b;
}

Block*
allocb(int size)
{
	Block *b;

	/*
	 * Check in a process and wait until successful.
	 */
	if(up == nil)
		panic("allocb without up: %#p", getcallerpc(&size));
	while((b = _allocb(size, BLOCKALIGN)) == nil){
		if(up->nlocks || m->ilockdepth || !islo()){
			xsummary();
			mallocsummary();
			panic("allocb: no memory for %d bytes", size);
		}
		if(!waserror()){
			resrcwait("no memory for allocb");
			poperror();
		}
	}
	setmalloctag(b, getcallerpc(&size));

	return b;
}

Block*
iallocb(int size)
{
	Block *b;

	if((b = _allocb(size, BLOCKALIGN)) == nil){
		static ulong nerr;
		if((nerr++%10000)==0){
			if(nerr > 10000000){
				xsummary();
				mallocsummary();
				panic("iallocb: out of memory");
			}
			iprint("iallocb: no memory for %d bytes\n", size);
		}
		return nil;
	}
	setmalloctag(b, getcallerpc(&size));
	b->flag = BINTR;

	return b;
}

void
freeb(Block *b)
{
	Bpool *p;
	void *dead = (void*)Bdead;

	if(b == nil)
		return;

	if((p = b->pool) != nil) {
		b->next = nil;
		b->rp = b->wp = b->lim - ROUND(p->size+Tlrspc, p->align);
		b->flag = BINTR;
		ilock(p);
		b->list = p->head;
		p->head = b;
		iunlock(p);
		return;
	}

	/* poison the block in case someone is still holding onto it */
	b->next = dead;
	b->list = dead;
	b->rp = dead;
	b->wp = dead;
	b->lim = dead;
	b->base = dead;
	b->pool = dead;

	free(b);
}

static ulong
_alignment(ulong align)
{
	if(align <= BLOCKALIGN)
		return BLOCKALIGN;

	/* make it a power of two */
	align--;
	align |= align>>1;
	align |= align>>2;
	align |= align>>4;
	align |= align>>8;
	align |= align>>16;
	align++;

	return align;
}

Block*
iallocbp(Bpool *p)
{
	Block *b;

	ilock(p);
	if((b = p->head) != nil){
		p->head = b->list;
		b->list = nil;
		iunlock(p);
	} else {
		iunlock(p);
		p->align = _alignment(p->align);
		b = _allocb(p->size, p->align);
		if(b == nil)
			return nil;
		setmalloctag(b, getcallerpc(&p));
		b->pool = p;
		b->flag = BINTR;
	}

	return b;
}

void
growbp(Bpool *p, int n)
{
	ulong size;
	Block *b;
	uchar *a;

	if(n < 1)
		return;
	if((b = malloc(sizeof(Block)*n)) == nil)
		return;
	p->align = _alignment(p->align);
	size = ROUND(p->size+Hdrspc+Tlrspc, p->align);
	if((a = mallocalign(size*n, p->align, 0, 0)) == nil){
		free(b);
		return;
	}
	setmalloctag(b, getcallerpc(&p));
	while(n > 0){
		b->base = a;
		a += size;
		b->lim = a;
		b->pool = p;
		freeb(b);
		b++;
		n--;
	}
}

void
checkb(Block *b, char *msg)
{
	void *dead = (void*)Bdead;

	if(b == dead)
		panic("checkb b %s %#p", msg, b);
	if(b->base == dead || b->lim == dead
	|| b->next == dead || b->list == dead || b->pool == dead
	|| b->rp == dead || b->wp == dead){
		print("checkb: base %#p lim %#p next %#p list %#p pool %#p\n",
			b->base, b->lim, b->next, b->list, b->pool);
		print("checkb: rp %#p wp %#p\n", b->rp, b->wp);
		panic("checkb dead: %s", msg);
	}
	if(b->base > b->lim)
		panic("checkb 0 %s %#p %#p", msg, b->base, b->lim);
	if(b->rp < b->base)
		panic("checkb 1 %s %#p %#p", msg, b->base, b->rp);
	if(b->wp < b->base)
		panic("checkb 2 %s %#p %#p", msg, b->base, b->wp);
	if(b->rp > b->lim)
		panic("checkb 3 %s %#p %#p", msg, b->rp, b->lim);
	if(b->wp > b->lim)
		panic("checkb 4 %s %#p %#p", msg, b->wp, b->lim);
}
