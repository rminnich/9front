#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

enum
{
	Nhole		= 128,
	Magichole	= 0x484F4C45,			/* HOLE */
};

typedef struct Hole Hole;
typedef struct Xalloc Xalloc;
typedef struct Xhdr Xhdr;

struct Hole
{
	uintptr	addr;
	uintptr	size;
	uintptr	top;
	Hole*	link;
};

struct Xhdr
{
	ulong	size;
	ulong	magix;
	char	data[];
};

struct Xalloc
{
	Lock;
	Hole	hole[Nhole];
	Hole*	flist;
	Hole*	table;
};

static Xalloc	xlists;

int once = 0;
void
xinit(void)
{
	ulong maxpages, kpages, n;
	Hole *h, *eh;
	Confmem *cm;
	int i;
	print("XINIT XINIT XINIT XINIT from %p\n", getcallerpc(&n));
	if (once) print("XINIT OH NO CLALAED TWICE %d fro %p!!!\n", once, getcallerpc(&n));
	once++;

	eh = &xlists.hole[Nhole-1];
	for(h = xlists.hole; h < eh; h++)
		h->link = h+1;
	print("xinit: after setting up hole links\n");
	xlists.flist = xlists.hole;

	kpages = conf.npage - conf.upages;
	print("XINIT: kpages 0x%lx\n", kpages);

	for(i=0; i<nelem(conf.mem); i++){
		print("xinit for\n");
		cm = &conf.mem[i];
		print("xinit cm #%d: cm %p\n", i, cm);
		n = cm->npage;
		print("%d: npage 0x%lx\n", i, n);
		if(n > kpages)
			n = kpages;
		/* don't try to use non-KADDR-able memory for kernel */
		print("xinit:call cankaddr with cm->base %p", cm->base);
		maxpages = cankaddr(cm->base)/BY2PG;
		print("xinit:maxpages 0x%lx\n", maxpages);
		if(n > maxpages)
			n = maxpages;
	print("maxpages and n is 0x%lx\n", n);
		/* give to kernel */
		if(n > 0){
			print("xinit: give %ld to kernel n > 0\n", n);
			cm->kbase = (uintptr)KADDR(cm->base);
			cm->klimit = (uintptr)cm->kbase+(uintptr)n*BY2PG;
			if(cm->klimit == 0)
				cm->klimit = (uintptr)-BY2PG;
			print("xinit call xhole with %p, %llx\n", cm->base, cm->klimit - cm->kbase);
			xhole(cm->base, cm->klimit - cm->kbase);
			print("xinit: end of ok, kpages is now 0x%lx...ok\n", kpages - n);
			kpages -= n;
		}
		/*
		 * anything left over: cm->npage - nkpages(cm)
		 * will be given to user by pageinit()
		 */
	}
	print("xinit: print summary\n");
	xsummary();
	print("xinit: done\n");
}

void*
xspanalloc(ulong size, int align, ulong span)
{
	uintptr a, v, t;

	a = (uintptr)xalloc(size+align+span);
	if(a == 0)
		panic("xspanalloc: %lud %d %lux", size, align, span);

	if(span > 2) {
		v = (a + span) & ~((uintptr)span-1);
		t = v - a;
		if(t > 0)
			xhole(PADDR(a), t);
		t = a + span - v;
		if(t > 0)
			xhole(PADDR(v+size+align), t);
	}
	else
		v = a;

	if(align > 1)
		v = (v + align) & ~((uintptr)align-1);

	return (void*)v;
}

void*
xallocz(ulong size, int zero)
{
	Xhdr *p;
	Hole *h, **l;

	print("xallocz %lud %s\n", size, zero ? "zerod" : "");
	xsummary();
	/* add room for magix & size overhead, round up to nearest vlong */
	size += BY2V + offsetof(Xhdr, data[0]);
	size &= ~(BY2V-1);

	ilock(&xlists);
	l = &xlists.table;
	print("xallocz xsummary before for loop\n");
	xsummary();
	for(h = *l; h; h = h->link) {
		print("check h %p h->addr %p h->size 0x%llx size 0x%lx\n", h, h->addr, h->size, size);
		if(h->size >= size) {
			print("... it's good addr %p\n", h->addr);
			p = (Xhdr*)KADDR(h->addr);
			print("p is %p\n", p);
			h->addr += size;
			h->size -= size;
			print("xsummary after allocate using hole %p, addr %p, \n", h, p);
			xsummary();
			if(h->size == 0) {
				*l = h->link;
				h->link = xlists.flist;
				xlists.flist = h;
			}
			print("iunlock %p\n", &xlists);
			iunlock(&xlists);
			print("memset %p for %lud bytes\n", p, size);
			//while(zero);
			if(zero)
				memset(p, 0, size);
			p->magix = Magichole;
			p->size = size;
			print("XALLOC  %p 0x%lx bytes called by %p\n", p->data, size, getcallerpc(&size));
			return p->data;
		}
		l = &h->link;
	}
	iunlock(&xlists);
	return nil;
}

void*
xalloc(ulong size)
{
	void *v = xallocz(size, 1);
	print("xalloc %p callerpc %p\n", v, getcallerpc(&size));
	return v;
}

void
xfree(void *p)
{
	Xhdr *x;

	x = (Xhdr*)((uintptr)p - offsetof(Xhdr, data[0]));
	if(x->magix != Magichole) {
		xsummary();
		panic("xfree(%#p) %#ux != %#lux", p, Magichole, x->magix);
	}
	xhole(PADDR((uintptr)x), x->size);
}

int
xmerge(void *vp, void *vq)
{
	Xhdr *p, *q;

	print("XMERGE\n");
	p = (Xhdr*)(((uintptr)vp - offsetof(Xhdr, data[0])));
	q = (Xhdr*)(((uintptr)vq - offsetof(Xhdr, data[0])));
	if(p->magix != Magichole || q->magix != Magichole) {
		int i;
		ulong *wd;
		void *badp;

		xsummary();
		badp = (p->magix != Magichole? p: q);
		wd = (ulong *)badp - 12;
		for (i = 24; i-- > 0; ) {
			print("%#p: %lux", wd, *wd);
			if (wd == badp)
				print(" <-");
			print("\n");
			wd++;
		}
		panic("xmerge(%#p, %#p) bad magic %#lux, %#lux",
			vp, vq, p->magix, q->magix);
	}
	if((uchar*)p+p->size == (uchar*)q) {
		p->size += q->size;
		return 1;
	}
	return 0;
}

void
xhole(uintptr addr, uintptr size)
{
	Hole *h, *c, **l;
	uintptr top;

	print("XHOLE %p 0x%llx\n", addr, size);
	if(size == 0)
		return;

	top = addr + size;
	ilock(&xlists);
	l = &xlists.table;
	for(h = *l; h; h = h->link) {
		if(h->top == addr) {
			h->size += size;
			h->top = h->addr+h->size;
			c = h->link;
			if(c && h->top == c->addr) {
				h->top += c->size;
				h->size += c->size;
				h->link = c->link;
				c->link = xlists.flist;
				xlists.flist = c;
			}
			iunlock(&xlists);
			return;
		}
		if(h->addr > addr)
			break;
		l = &h->link;
	}
	if(h && top == h->addr) {
		h->addr -= size;
		h->size += size;
		iunlock(&xlists);
		return;
	}

	if(xlists.flist == nil) {
		iunlock(&xlists);
		print("xfree: no free holes, leaked %llud bytes\n", (uvlong)size);
		return;
	}

	h = xlists.flist;
	xlists.flist = h->link;
	h->addr = addr;
	h->top = top;
	h->size = size;
	h->link = *l;
	*l = h;
	iunlock(&xlists);
}

void
xsummary(void)
{
	int i;
	Hole *h;
	uintptr s;

	i = 0;
	for(h = xlists.flist; h; h = h->link)
		i++;
	print("%d holes free\n", i);

	s = 0;
	for(h = xlists.table; h; h = h->link) {
		print("%#8.8p %#8.8p %llud\n", h->addr, h->top, (uvlong)h->size);
		s += h->size;
	}
	print("%llud bytes free\n", (uvlong)s);
}
