#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

static void
lrudel(Blk *b)
{
	if(b == fs->chead)
		fs->chead = b->cnext;
	if(b == fs->ctail)
		fs->ctail = b->cprev;
	if(b->cnext != nil)
		b->cnext->cprev = b->cprev;
	if(b->cprev != nil)
		b->cprev->cnext = b->cnext;
	b->cnext = nil;
	b->cprev = nil;		
}

void
lrutop(Blk *b)
{
	qlock(&fs->lrulk);
	/*
	 * Someone got in first and did a
	 * cache lookup; we no longer want
	 * to put this into the LRU, because
	 * its now in use.
	 */
	assert(b->magic == Magic);
	if(b->ref != 0){
		qunlock(&fs->lrulk);
		return;
	}
	lrudel(b);
	if(fs->chead != nil)
		fs->chead->cprev = b;
	if(fs->ctail == nil)
		fs->ctail = b;
	b->cnext = fs->chead;
	fs->chead = b;
	rwakeup(&fs->lrurz);
	qunlock(&fs->lrulk);
}

void
lrubot(Blk *b)
{
	qlock(&fs->lrulk);
	/*
	 * Someone got in first and did a
	 * cache lookup; we no longer want
	 * to put this into the LRU, because
	 * its now in use.
	 */
	assert(b->magic == Magic);
	if(b->ref != 0){
		qunlock(&fs->lrulk);
		return;
	}
	lrudel(b);
	if(fs->ctail != nil)
		fs->ctail->cnext = b;
	if(fs->chead == nil)
		fs->chead = b;
	b->cprev = fs->ctail;
	fs->ctail = b;
	rwakeup(&fs->lrurz);
	qunlock(&fs->lrulk);
}

void
cacheins(Blk *b)
{
	Bucket *bkt;
	u32int h;

	assert(b->magic == Magic);
	h = ihash(b->bp.addr);
	bkt = &fs->bcache[h % fs->cmax];
	qlock(&fs->lrulk);
	traceb("cache", b->bp);
	if(checkflag(b, Bcached)){
		qunlock(&fs->lrulk);
		return;
	}
	assert(b->hnext == nil);
	for(Blk *bb = bkt->b; bb != nil; bb = bb->hnext)
		assert(b != bb);
	setflag(b, Bcached);
	b->cached = getcallerpc(&b);
	b->hnext = bkt->b;
	bkt->b = b;
	qunlock(&fs->lrulk);
}

void
cachedel_lk(vlong addr)
{
	Bucket *bkt;
	Blk *b, **p;
	u32int h;

	if(addr == -1)
		return;

	tracex("uncache", Zb, addr, getcallerpc(&addr));
	h = ihash(addr);
	bkt = &fs->bcache[h % fs->cmax];
	p = &bkt->b;
	for(b = bkt->b; b != nil; b = b->hnext){
		if(b->bp.addr == addr){
			*p = b->hnext;
			clrflag(b, Bcached);
			b->uncached = getcallerpc(&addr);
			b->hnext = nil;
			break;
		}
		p = &b->hnext;
	}
}
void
cachedel(vlong addr)
{
	qlock(&fs->lrulk);
	tracex("uncachelk", Zb, addr, getcallerpc(&addr));
	cachedel_lk(addr);
	qunlock(&fs->lrulk);
}

void
cacheflag(vlong addr, int flg)
{
	Bucket *bkt;
	u32int h;
	Blk *b;

	h = ihash(addr);
	bkt = &fs->bcache[h % fs->cmax];
	qlock(&fs->lrulk);
	for(b = bkt->b; b != nil; b = b->hnext)
		if(b->bp.addr == addr)
			setflag(b, flg);
	qunlock(&fs->lrulk);

}

Blk*
cacheget(vlong addr)
{
	Bucket *bkt;
	u32int h;
	Blk *b;

	h = ihash(addr);
	bkt = &fs->bcache[h % fs->cmax];
	qlock(&fs->lrulk);
	for(b = bkt->b; b != nil; b = b->hnext){
		if(b->bp.addr == addr){
			holdblk(b);
			lrudel(b);
			b->lasthold = getcallerpc(&addr);
			break;
		}
	}
	qunlock(&fs->lrulk);

	return b;
}

/*
 * Pulls the block from the bottom of the LRU for reuse.
 */
Blk*
cachepluck(void)
{
	Blk *b;

	qlock(&fs->lrulk);
	while(fs->ctail == nil)
		rsleep(&fs->lrurz);

	b = fs->ctail;
	assert(b->magic == Magic);
	assert(b->ref == 0);
	if(checkflag(b, Bcached))
		cachedel_lk(b->bp.addr);
	if(checkflag(b, Bcached))
		fprint(2, "%B cached %#p freed %#p\n", b->bp, b->cached, b->freed);
	lrudel(b);
	assert(!checkflag(b, Bcached));
	b->flag = 0;
	b->lasthold = 0;
	b->lastdrop = 0;
	b->freed = 0;
	b->hnext = nil;
	qunlock(&fs->lrulk);

	return  holdblk(b);
}
