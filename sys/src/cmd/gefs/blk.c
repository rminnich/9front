#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"
#include "atomic.h"

static vlong	blkalloc_lk(Arena*, int);
static vlong	blkalloc(int, uint, int);
static void	blkdealloc_lk(Arena*, vlong);
static Blk*	initblk(Blk*, vlong, vlong, int);

int
checkflag(Blk *b, int f)
{
	long v;

	v = agetl(&b->flag);
	return (v & f) == f;
}

void
setflag(Blk *b, int f)
{
	long ov, nv;

	while(1){
		ov = agetl(&b->flag);
		nv = ov | f;
		if(acasl(&b->flag, ov, nv))
			break;
	}
}

void
clrflag(Blk *b, int f)
{
	long ov, nv;

	while(1){
		ov = agetl(&b->flag);
		nv = ov & ~f;
		if(acasl(&b->flag, ov, nv))
			break;
	}
}

void
syncblk(Blk *b)
{
	assert(checkflag(b, Bfinal));
	assert(b->bp.addr >= 0);
	clrflag(b, Bdirty);
	if(pwrite(fs->fd, b->buf, Blksz, b->bp.addr) == -1)
		broke("%B %s: %r", b->bp, Eio);
}

static Blk*
readblk(vlong bp, int flg)
{
	vlong off, rem, n;
	char *p;
	Blk *b;

	assert(bp != -1);
	b = cachepluck();
	b->alloced = getcallerpc(&bp);
	off = bp;
	rem = Blksz;
	while(rem != 0){
		n = pread(fs->fd, b->buf, rem, off);
		if(n <= 0)
			error("%s: %r", Eio);
		off += n;
		rem -= n;
	}
	b->cnext = nil;
	b->cprev = nil;
	b->hnext = nil;
	b->flag = 0;

	b->bp.addr = bp;
	b->bp.hash = -1;
	b->bp.gen = -1;

	b->nval = 0;
	b->valsz = 0;
	b->nbuf = 0;
	b->bufsz = 0;
	b->logsz = 0;

	p = b->buf + 2;
	b->type = (flg&GBraw) ? Tdat : UNPACK16(b->buf+0);
	switch(b->type){
	default:
		broke("invalid block type %d @%llx", b->type, bp);
		break;
	case Tdat:
	case Tsuper:
		b->data = b->buf;
		break;
	case Tarena:
		b->data = p;
		break;
	case Tdlist:
	case Tlog:
		b->logsz = UNPACK16(p);		p += 2;
		b->logh = UNPACK64(p);		p += 8;
		b->logp = unpackbp(p, Ptrsz);	p += Ptrsz;
		assert(p - b->buf == Loghdsz);
		b->data = p;
		break;
	case Tpivot:
		b->nval = UNPACK16(p);		p += 2;
		b->valsz = UNPACK16(p);		p += 2;
		b->nbuf = UNPACK16(p);		p += 2;
		b->bufsz = UNPACK16(p);		p += 2;
		assert(p - b->buf == Pivhdsz);
		b->data = p;
		break;
	case Tleaf:
		b->nval = UNPACK16(p);		p += 2;
		b->valsz = UNPACK16(p);		p += 2;
		assert(p - b->buf == Leafhdsz);
		b->data = p;
		break;
	}
	assert(b->magic == Magic);
	return b;
}

static Arena*
pickarena(uint ty, uint hint, int tries)
{
	uint n, r;

	r = ainc(&fs->roundrobin)/2048;
	if(ty == Tdat)
		n = hint % (fs->narena - 1) + r + 1;
	else
		n = r;
	return &fs->arenas[(n + tries) % fs->narena];
}

Arena*
getarena(vlong b)
{
	int hi, lo, mid;
	vlong alo, ahi;
	Arena *a;

	lo = 0;
	hi = fs->narena;
	if(b == fs->sb0->bp.addr)
		return &fs->arenas[0];
	if(b == fs->sb1->bp.addr)
		return &fs->arenas[hi-1];
	while(1){
		mid = (hi + lo)/2;
		a = &fs->arenas[mid];
		alo = a->h0->bp.addr;
		ahi = alo + a->size + 2*Blksz;
		if(b < alo)
			hi = mid-1;
		else if(b > ahi)
			lo = mid+1;
		else
			return a;
	}
}


static void
freerange(Avltree *t, vlong off, vlong len)
{
	Arange *r, *s;

	assert(len % Blksz == 0);
	if((r = calloc(1, sizeof(Arange))) == nil)
		error(Enomem);
	r->off = off;
	r->len = len;
	assert(avllookup(t, r, 0) == nil);
	avlinsert(t, r);

Again:
	s = (Arange*)avlprev(r);
	if(s != nil && s->off+s->len == r->off){
		avldelete(t, r);
		s->len = s->len + r->len;
		free(r);
		r = s;
		goto Again;
	}
	s = (Arange*)avlnext(r);
	if(s != nil && r->off+r->len == s->off){
		avldelete(t, r);
		s->off = r->off;
		s->len = s->len + r->len;
		free(r);
		r = s;
		goto Again;
	}
}

static void
grabrange(Avltree *t, vlong off, vlong len)
{
	Arange *r, *s, q;
	vlong l;

	assert(len % Blksz == 0);
	q.off = off;
	q.len = len;
	r = (Arange*)avllookup(t, &q.Avl, -1);
	if(r == nil || off + len > r->off + r->len)
		abort();

	if(off == r->off){
		r->off += len;
		r->len -= len;
	}else if(off + len == r->off + r->len){
		r->len -= len;
	}else if(off > r->off && off+len < r->off + r->len){
		s = emalloc(sizeof(Arange), 0);
		l = r->len;
		s->off = off + len;
		r->len = off - r->off;
		s->len = l - r->len - len;
		avlinsert(t, s);
	}else
		abort();

	if(r->len == 0){
		avldelete(t, r);
		free(r);
	}
}

static Blk*
mklogblk(Arena *a, vlong o)
{
	Blk *lb;

	lb = a->logbuf[a->lbidx++ % nelem(a->logbuf)];
	if(lb->bp.addr != -1)
		cachedel(lb->bp.addr);
	initblk(lb, o, -1, Tlog);
	finalize(lb);
	syncblk(lb);
	traceb("logblk" , lb->bp);
	return lb;
}

/*
 * Logs an allocation. Must be called
 * with arena lock held. Duplicates some
 * of the work in allocblk to prevent
 * recursion.
 */
static void
logappend(Arena *a, vlong off, vlong len, int op)
{
	vlong o, start, end;
	Blk *nl, *lb;
	char *p;

	lb = a->logtl;
	assert((off & 0xff) == 0);
	assert(op == LogAlloc || op == LogFree || op == LogSync);
	if(op != LogSync){
		start = a->h0->bp.addr;
		end = start + a->size + 2*Blksz;
		assert(lb == nil || lb->type == Tlog);
		assert(off >= start);
		assert(off <= end);
	}
	assert(lb == nil || lb->logsz >= 0);
	dprint("logop %d: %llx+%llx@%x\n", op, off, len, lb?lb->logsz:-1);
	/*
	 * move to the next block when we have
	 * too little room in the log:
	 * We're appending up to 16 bytes as
	 * part of the operation, followed by
	 * 16 bytes of new log entry allocation
	 * and chaining.
	 */
	if(lb == nil || lb->logsz >= Logspc - Logslop){
		o = blkalloc_lk(a, 0);
		if(o == -1)
			error(Efull);
		nl = mklogblk(a, o);
		p = lb->data + lb->logsz;
		PACK64(p, o|LogAlloc1);
		lb->logsz += 8;
		lb->logp = nl->bp;
		finalize(lb);
		syncblk(lb);
		a->logtl = nl;
		a->nlog++;
		lb = nl;
	}

	setflag(lb, Bdirty);
	if(len == Blksz){
		if(op == LogAlloc)
			op = LogAlloc1;
		else if(op == LogFree)
			op = LogFree1;
	}
	off |= op;
	p = lb->data + lb->logsz;
	PACK64(p, off);
	lb->logsz += 8;
	if(op >= Log2wide){
		PACK64(p+8, len);
		lb->logsz += 8;
	}
}

void
loadlog(Arena *a, Bptr bp)
{
	vlong ent, off, len, gen;
	int op, i, n;
	char *d;
	Blk *b;


	dprint("loadlog %B\n", bp);
	traceb("loadlog", bp);
	while(1){
		b = getblk(bp, 0);
		dprint("\tload %B chain %B\n", bp, b->logp);
		/* the hash covers the log and offset */
		for(i = 0; i < b->logsz; i += n){
			d = b->data + i;
			ent = UNPACK64(d);
			op = ent & 0xff;
			off = ent & ~0xff;
			n = (op >= Log2wide) ? 16 : 8;
			switch(op){
			case LogSync:
				gen = ent >> 8;
				dprint("\tlog@%x: sync %lld\n", i, gen);
				if(gen >= fs->qgen){
					if(a->logtl == nil){
						b->logsz = i;
						a->logtl = holdblk(b);
						return;
					}
					dropblk(b);
					return;
				}
				break;
	
			case LogAlloc:
			case LogAlloc1:
				len = (op >= Log2wide) ? UNPACK64(d+8) : Blksz;
				dprint("\tlog@%x alloc: %llx+%llx\n", i, off, len);
				grabrange(a->free, off & ~0xff, len);
				a->used += len;
				break;
			case LogFree:
			case LogFree1:
				len = (op >= Log2wide) ? UNPACK64(d+8) : Blksz;
				dprint("\tlog@%x free: %llx+%llx\n", i, off, len);
				freerange(a->free, off & ~0xff, len);
				a->used -= len;
				break;
			default:
				dprint("\tlog@%x: log op %d\n", i, op);
				abort();
				break;
			}
		}
		if(b->logp.addr == -1){
			a->logtl = b;
			return;
		}
		bp = b->logp;
		dropblk(b);
	}
}

void
compresslog(Arena *a)
{

	int i, nr, nblks;
	vlong sz, *blks;
	Blk *b, *nb;
	Arange *r;
	Bptr hd;
	char *p;

	tracem("compresslog");
	if(a->logtl != nil){
		finalize(a->logtl);
		syncblk(a->logtl);
	}
	/*
	 * Prepare what we're writing back.
	 * Arenas must be sized so that we can
	 * keep the merged log in memory for
	 * a rewrite.
	 */
	sz = 0;
	nr = 0;
	a->nlog = 0;
	for(r = (Arange*)avlmin(a->free); r != nil; r = (Arange*)avlnext(r)){
		sz += 16;
		nr++;
	}

	/*
	 * Make a pessimistic estimate of the number of blocks
	 * needed to store the ranges, as well as the blocks
	 * used to store the range allocations.
	 *
	 * This does modify the tree, but it's safe because
	 * we can only be removing entries from the tree, not
	 * splitting or inserting new ones.
	 */
	nblks = (sz+Logspc)/(Logspc - Logslop) + 16*nr/(Logspc-Logslop) + 1;
	if((blks = calloc(nblks, sizeof(vlong))) == nil)
		error(Enomem);
	if(waserror()){
		free(blks);
		nexterror();
	}
	for(i = 0; i < nblks; i++){
		blks[i] = blkalloc_lk(a, 1);
		if(blks[i] == -1)
			error(Efull);
	}
	/* fill up the log with the ranges from the tree */
	i = 0;
	hd = (Bptr){blks[0], -1, -1};
	b = a->logbuf[a->lbidx++ % nelem(a->logbuf)];
	a->logbuf[a->lbidx % nelem(a->logbuf)]->bp = Zb;
	if(b->bp.addr != -1)
		cachedel(b->bp.addr);
	initblk(b, blks[i++], -1, Tlog);
	finalize(b);
	for(r = (Arange*)avlmin(a->free); r != nil; r = (Arange*)avlnext(r)){
		if(b->logsz >= Logspc - Logslop){
			a->nlog++;
			nb = a->logbuf[a->lbidx++ % nelem(a->logbuf)];
			if(nb->bp.addr != -1)
				cachedel(nb->bp.addr);
			initblk(nb, blks[i++], -1, Tlog);
			b->logp = nb->bp;
			setflag(b, Bdirty);
			finalize(b);
			syncblk(b);
			b = nb;
		}
		p = b->data + b->logsz;
		PACK64(p+0, r->off|LogFree);
		PACK64(p+8, r->len);
		b->logsz += 16;
	}
	finalize(b);
	syncblk(b);

	/*
	 * now we have a valid freelist, and we can start
	 * appending stuff to it. Clean up the eagerly
	 * allocated extra blocks.
	 */
	a->loghd = hd;
	a->logtl = b;
	for(; i < nblks; i++){
		cachedel(b->bp.addr);
		blkdealloc_lk(a, blks[i]);
	}
	poperror();
	free(blks);
}

int
logbarrier(Arena *a, vlong gen)
{
	logappend(a, gen<<8, 0, LogSync);
	if(a->loghd.addr == -1)
		a->loghd = a->logtl->bp;
	return 0;
}

/*
 * Allocate from an arena, with lock
 * held. May be called multiple times
 * per operation, to alloc space for
 * the alloc log.
 */
static vlong
blkalloc_lk(Arena *a, int seq)
{
	Arange *r;
	vlong b;

	if(seq)
		r = (Arange*)avlmin(a->free);
	else
		r = (Arange*)avlmax(a->free);
	if(!usereserve && a->size - a->used <= a->reserve)
		return -1;
	if(r == nil)
		broke(Estuffed);

	/*
	 * A bit of sleight of hand here:
	 * while we're changing the sorting
	 * key, but we know it won't change
	 * the sort order because the tree
	 * covers disjoint ranges
	 */
	if(seq){
		b = r->off;
		r->len -= Blksz;
		r->off += Blksz;
	}else{
		r->len -= Blksz;
		b = r->off + r->len;
	}
	if(r->len == 0){
		avldelete(a->free, r);
		free(r);
	}
	a->used += Blksz;
	return b;
}

static void
blkdealloc_lk(Arena *a, vlong b)
{
	logappend(a, b, Blksz, LogFree);
	if(a->loghd.addr == -1)
		a->loghd = a->logtl->bp;
	freerange(a->free, b, Blksz);
	a->used -= Blksz;
}

void
blkdealloc(vlong b)
{
	Arena *a;

	a = getarena(b);
 	qlock(a);
	blkdealloc_lk(a, b);
	qunlock(a);
}

static vlong
blkalloc(int ty, uint hint, int seq)
{
	Arena *a;
	vlong b;
	int tries;

	tries = 0;
Again:
	a = pickarena(ty, hint, tries);
	/*
	 * Loop through the arena up to 2 times.
	 * The first pass tries to find an arena
	 * that has space and is not in use, the
	 * second waits until an arena is free.
	 */
	if(tries == 2*fs->narena)
		error(Efull);
	tries++;
	if(tries < fs->narena){
		if(canqlock(a) == 0)
			goto Again;
	}else
		qlock(a);
	if(waserror()){
		qunlock(a);
		nexterror();
	}
	b = blkalloc_lk(a, seq);
	if(b == -1){
		qunlock(a);
		poperror();
		goto Again;
	}
	logappend(a, b, Blksz, LogAlloc);
	if(a->loghd.addr == -1)
		a->loghd = a->logtl->bp;
	qunlock(a);
	poperror();
	return b;
}

static Blk*
initblk(Blk *b, vlong bp, vlong gen, int ty)
{
	Blk *ob;

	ob = cacheget(bp);
	if(ob != nil)
		fatal("double alloc: %#p %B %#p %B", b, b->bp, ob, ob->bp);
	b->type = ty;
	b->bp.addr = bp;
	b->bp.hash = -1;
	b->bp.gen = gen;
	switch(ty){
	case Tdat:
		b->data = b->buf;
		break;
	case Tarena:
		b->data = b->buf+2;
		break;
	case Tdlist:
	case Tlog:
		b->logsz = 0;
		b->logp = (Bptr){-1, -1, -1};
		b->data = b->buf + Loghdsz;
		break;
	case Tpivot:
		b->data = b->buf + Pivhdsz;
		break;
	case Tleaf:
		b->data = b->buf + Leafhdsz;
		break;
	}
	setflag(b, Bdirty);
	b->nval = 0;
	b->valsz = 0;
	b->nbuf = 0;
	b->bufsz = 0;
	b->logsz = 0;
	b->alloced = getcallerpc(&b);

	return b;
}

Blk*
newdblk(Tree *t, vlong hint, int seq)
{
	vlong bp;
	Blk *b;

	bp = blkalloc(Tdat, hint, seq);
	b = cachepluck();
	initblk(b, bp, t->memgen, Tdat);
	b->alloced = getcallerpc(&t);
	tracex("newblk" , b->bp, Tdat, -1);
	return b;

}

Blk*
newblk(Tree *t, int ty)
{
	vlong bp;
	Blk *b;

	bp = blkalloc(ty, 0, 0);
	b = cachepluck();
	initblk(b, bp, t->memgen, ty);
	b->alloced = getcallerpc(&t);
	tracex("newblk" , b->bp, ty, -1);
	return b;
}

Blk*
dupblk(Tree *t, Blk *b)
{
	Blk *r;

	if((r = newblk(t, b->type)) == nil)
		return nil;

	tracex("dup" , b->bp, b->type, t->gen);
	setflag(r, Bdirty);
	r->bp.hash = -1;
	r->nval = b->nval;
	r->valsz = b->valsz;
	r->nbuf = b->nbuf;
	r->bufsz = b->bufsz;
	r->logsz = b->logsz;
	r->alloced = getcallerpc(&t);
	memcpy(r->buf, b->buf, sizeof(r->buf));
	return r;
}

void
finalize(Blk *b)
{
	if(b->type != Tdat)
		PACK16(b->buf, b->type);

	switch(b->type){
	default:
		abort();
		break;
	case Tpivot:
		PACK16(b->buf+2, b->nval);
		PACK16(b->buf+4, b->valsz);
		PACK16(b->buf+6, b->nbuf);
		PACK16(b->buf+8, b->bufsz);
		break;
	case Tleaf:
		PACK16(b->buf+2, b->nval);
		PACK16(b->buf+4, b->valsz);
		break;
	case Tdlist:
	case Tlog:
		b->logh = bufhash(b->data, b->logsz);
		PACK16(b->buf+2, b->logsz);
		PACK64(b->buf+4, b->logh);
		packbp(b->buf+12, Ptrsz, &b->logp);
		break;
	case Tdat:
	case Tarena:
	case Tsuper:
		break;
	}

	b->bp.hash = blkhash(b);
	setflag(b, Bfinal);
	cacheins(b);
	b->cached = getcallerpc(&b);
}

Blk*
getblk(Bptr bp, int flg)
{
	uvlong xh, ck;
	Blk *b;
	int i;

	i = ihash(bp.addr) % nelem(fs->blklk);
	tracex("get" , bp, getcallerpc(&bp), -1);
	qlock(&fs->blklk[i]);
	if(waserror()){
		qunlock(&fs->blklk[i]);
		nexterror();
	}
	if((b = cacheget(bp.addr)) != nil){
		b->lasthold = getcallerpc(&bp);
		qunlock(&fs->blklk[i]);
		poperror();
		return b;
	}
	b = readblk(bp.addr, flg);
	b->alloced = getcallerpc(&bp);
	b->bp.hash = blkhash(b);
	if((flg&GBnochk) == 0){
		if(b->type == Tlog || b->type == Tdlist){
			xh = b->logh;
			ck = bufhash(b->data, b->logsz);
		}else{
			xh = bp.hash;
			ck = b->bp.hash;
		}
		if(ck != xh){
			if(flg & GBsoftchk){
				fprint(2, "%s: %ullx %llux != %llux", Ecorrupt, bp.addr, xh, ck);
				error(Ecorrupt);
			}else{
				broke("%s: %ullx %llux != %llux", Ecorrupt, bp.addr, xh, ck);
			}
		}
	}
	b->bp.gen = bp.gen;
	b->lasthold = getcallerpc(&bp);
	cacheins(b);
	qunlock(&fs->blklk[i]);
	poperror();

	return b;
}


Blk*
holdblk(Blk *b)
{
	ainc(&b->ref);
	b->lasthold = getcallerpc(&b);
	return b;
}

void
dropblk(Blk *b)
{
	assert(b == nil || b->ref > 0);
	if(b == nil || adec(&b->ref) != 0)
		return;
	b->lastdrop = getcallerpc(&b);
	/*
	 * freed blocks go to the LRU bottom
	 * for early reuse.
	 */
	if(checkflag(b, Bfreed))
		lrubot(b);
	else
		lrutop(b);
}

ushort
blkfill(Blk *b)
{
	switch(b->type){
	case Tpivot:
		return 2*b->nbuf + b->bufsz +  2*b->nval + b->valsz;
	case Tleaf:
		return 2*b->nval + b->valsz;
	default:
		fprint(2, "invalid block @%lld\n", b->bp.addr);
		abort();
	}
}

void
limbo(Bfree *f)
{
	Bfree *p;
	ulong ge;

	while(1){
		ge = agetl(&fs->epoch);
		p = agetp(&fs->limbo[ge]);
		f->next = p;
		if(acasp(&fs->limbo[ge], p, f)){
			aincl(&fs->nlimbo, 1);
			break;
		}
	}
}

void
freeblk(Tree *t, Blk *b, Bptr bp)
{
	Bfree *f;

	if(t == &fs->snap || (t != nil && bp.gen < t->memgen)){
		tracex("killb", bp, getcallerpc(&t), -1);
		killblk(t, bp);
		return;
	}

	tracex("freeb", bp, getcallerpc(&t), -1);
	f = emalloc(sizeof(Bfree), 0);
	f->op = DFblk;
	f->bp = bp;
	f->b = nil;
	if(b != nil){
		setflag(b, Blimbo);
		b->freed = getcallerpc(&t);
		f->b = holdblk(b);
	}
	limbo(f);
}

void
epochstart(int tid)
{
	ulong ge;

	ge = agetl(&fs->epoch);
	asetl(&fs->lepoch[tid], ge | Eactive);
}

void
epochend(int tid)
{
	ulong le;

	le = agetl(&fs->lepoch[tid]);
	asetl(&fs->lepoch[tid], le &~ Eactive);
}

void
epochwait(void)
{
	int i, delay;
	ulong e, ge;

	delay = 0;
Again:
	ge = agetl(&fs->epoch);
	for(i = 0; i < fs->nworker; i++){
		e = agetl(&fs->lepoch[i]);
		if((e & Eactive) && e != (ge | Eactive)){
			if(delay < 100)
				delay++;
			else
				fprint(2, "stalled epoch %lx [worker %d]\n", e, i);
			sleep(delay);
			goto Again;
		}
	}
}

void
epochclean(void)
{
	ulong c, e, ge;
	Bfree *p, *n;
	Arena *a;
	Qent qe;
	int i;

	c = agetl(&fs->nlimbo);
	ge = agetl(&fs->epoch);
	for(i = 0; i < fs->nworker; i++){
		e = agetl(&fs->lepoch[i]);
		if((e & Eactive) && e != (ge | Eactive)){
			if(c < fs->cmax/4)
				return;
			epochwait();
		}
	}
	epochwait();
	p = asetp(&fs->limbo[(ge+1)%3], nil);
	asetl(&fs->epoch, (ge+1)%3);

	for(; p != nil; p = n){
		n = p->next;
		switch(p->op){
		case DFtree:
			free(p->t);
			break;
		case DFmnt:
			free(p->m);
			break;
		case DFblk:
			a = getarena(p->bp.addr);
			qe.op = Qfree;
			qe.bp = p->bp;
			qe.b = nil;
			qput(a->sync, qe);
			cacheflag(p->bp.addr, Bfreed);
			if(p->b != nil){
				clrflag(p->b, Blimbo);
				setflag(p->b, Bfreed);
				dropblk(p->b);
			}
			break;
		default:
			abort();
		}
		aincl(&fs->nlimbo, -1);
		free(p);
	}
}

void
enqueue(Blk *b)
{
	Arena *a;
	Qent qe;

	assert(checkflag(b, Bdirty));
	assert(b->bp.addr >= 0);

	b->enqueued = getcallerpc(&b);
	a = getarena(b->bp.addr);
	holdblk(b);
	finalize(b);
	traceb("queueb", b->bp);
	setflag(b, Bqueued);
	b->queued = getcallerpc(&b);
	qe.op = Qwrite;
	qe.bp = b->bp;
	qe.b = b;
	qput(a->sync, qe);
}

void
qinit(Syncq *q)
{
	q->fullrz.l = &q->lk;
	q->emptyrz.l = &q->lk;
	q->nheap = 0;
	q->heapsz = fs->cmax;
	q->heap = emalloc(q->heapsz*sizeof(Qent), 1);

}

int
qcmp(Qent *a, Qent *b)
{
	if(a->qgen != b->qgen)
		return (a->qgen < b->qgen) ? -1 : 1;
	if(a->op != b->op)
		return (a->op < b->op) ? -1 : 1;
	if(a->bp.addr != b->bp.addr)
		return (a->bp.addr < b->bp.addr) ? -1 : 1;
	return 0;
}

void
qput(Syncq *q, Qent qe)
{
	int i;

	if(qe.op == Qfree || qe.op == Qwrite)
		assert((qe.bp.addr & (Blksz-1)) == 0);
	else if(qe.op == Qfence)
		assert(fs->syncing > 0);
	else
		abort();
	qlock(&q->lk);
	qe.qgen = agetv(&fs->qgen);
	while(q->nheap == q->heapsz)
		rsleep(&q->fullrz);
	for(i = q->nheap; i > 0; i = (i-1)/2){
		if(qcmp(&qe, &q->heap[(i-1)/2]) == 1)
			break;
		q->heap[i] = q->heap[(i-1)/2];
	}
	q->heap[i] = qe;
	q->nheap++;
	rwakeup(&q->emptyrz);
	qunlock(&q->lk);
}

static Qent
qpop(Syncq *q)
{
	int i, l, r, m;
	Qent e, t;

	qlock(&q->lk);
	while(q->nheap == 0)
		rsleep(&q->emptyrz);
	e = q->heap[0];
	if(--q->nheap == 0)
		goto Out;

	i = 0;
	q->heap[0] = q->heap[q->nheap];
	while(1){
		m = i;
		l = 2*i+1;
		r = 2*i+2;
		if(l < q->nheap && qcmp(&q->heap[m], &q->heap[l]) == 1)
			m = l;
		if(r < q->nheap && qcmp(&q->heap[m], &q->heap[r]) == 1)
			m = r;
		if(m == i)
			break;
		t = q->heap[m];
		q->heap[m] = q->heap[i];
		q->heap[i] = t;
		i = m;
	}
Out:
	rwakeup(&q->fullrz);
	qunlock(&q->lk);
	if(e.b != nil){
		clrflag(e.b, Bqueued);
		e.b->queued = 0;
	}
	return e;
}

void
runsync(int, void *p)
{
	Arena *a;
	Syncq *q;
	Qent qe;

	q = p;
	if(waserror()){
		aincl(&fs->rdonly, 1);
		fprint(2, "error syncing: %s\n", errmsg());
		return;
	}
	while(1){
		qe = qpop(q);
		switch(qe.op){
		case Qfree:
			tracex("qfreeb", qe.bp, qe.qgen, -1);
			a = getarena(qe.bp.addr);
			qlock(a);
			cachedel(qe.bp.addr);
			blkdealloc_lk(a, qe.bp.addr);
			if(qe.b != nil)
				dropblk(qe.b);
			qunlock(a);
			break;
		case Qfence:
			tracev("qfence", qe.qgen);
			qlock(&fs->synclk);
			if(--fs->syncing == 0)
				rwakeupall(&fs->syncrz);
			qunlock(&fs->synclk);
			break;
		case Qwrite:
			tracex("qsyncb", qe.bp, qe.qgen, -1);
			if(checkflag(qe.b, Bfreed) == 0)
				syncblk(qe.b);
			dropblk(qe.b);
			break;
		default:
			abort();
		}
		assert(estacksz() == 1);
	}
}
