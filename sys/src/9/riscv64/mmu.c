#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#define INITMAP	(ROUND((uintptr)end + BY2PG, PGLSZ(1))-KZERO)

// kmax will be computed dynamically in future; for now it is
// UZERO.
uintptr klimit = UZERO;

void
mmu1init(void)
{
	extern u64int *sv57, *sv48, *sv39, *pGiB;
	m->mmutop = mallocalign(L1TOPSIZE, BY2PG, 0, 0);
	if(m->mmutop == nil)
		panic("mmu1init: no memory for mmutop");
	memset(m->mmutop, 0, L1TOPSIZE);
	memmove(m->mmutop, sv39, L1TOPSIZE);
	mmuswitch(nil);
}

uintptr
paddr(void *va)
{
	if((uintptr)va < KLIMIT)
		return (uintptr)va;
	panic("paddr: va=%#p pc=%#p", va, getcallerpc(&va));
}

uintptr
cankaddr(uintptr pa)
{
//	print("cankaddr %p, -KZ %p\n", pa,(uintptr)-KZERO );
	if(pa < (uintptr)KLIMIT)
		return pa;
	return 0;
}

void*
kaddr(uintptr pa)
{
	if(pa < (uintptr)KLIMIT)
		return (void*)(pa);
	panic("kaddr: pa=%#p pc=%#p", pa, getcallerpc(&pa));
}

static void*
kmapaddr(uintptr pa)
{
	return kaddr(pa);
}

KMap*
kmap(Page *p)
{
	return kmapaddr(p->pa);
}

void
kunmap(KMap*)
{
}

void
kmapinval(void)
{
}

static void*
rampage(void)
{
	uintptr pa;

	if(conf.npage)
		return mallocalign(BY2PG, BY2PG, 0, 0);

	pa = conf.mem[0].base;
	assert((pa % BY2PG) == 0);
	conf.mem[0].base += BY2PG;
	return KADDR(pa);
}

// nothing to do.
static void
l1map(uintptr va, uintptr pa, uintptr pe, uintptr attr)
{
	USED(va);
	USED(pa);
	USED(pe);
	USED(attr);
	if (0)print("l1map");
#ifdef xxx
	uintptr *l1, *l0;

	assert(pa < pe);

	va &= -BY2PG;
	pa &= -BY2PG;
	pe = PGROUND(pe);

	l1 = (uintptr*)L1;

	while(pa < pe){
		if(l1[PTL1X(va, 1)] == 0 && (pe-pa) >= PGLSZ(1) && ((va|pa) & PGLSZ(1)-1) == 0){
			l1[PTL1X(va, 1)] = PTEVALID | PA2PTE(pa) | attr;
			va += PGLSZ(1);
			pa += PGLSZ(1);
			continue;
		}
		if(l1[PTL1X(va, 1)] & PTEVALID) {
//			assert((l1[PTL1X(va, 1)] & PTETABLE) == PTETABLE);
			l0 = KADDR(l1[PTL1X(va, 1)] & -PGLSZ(0));
		} else {
			l0 = rampage();
			memset(l0, 0, BY2PG);
			l1[PTL1X(va, 1)] = PTEVALID | PA2PTE(PADDR(l0));
		}
		assert(l0[PTLX(va, 0)] == 0);
		l0[PTLX(va, 0)] = PTEVALID | PA2PTE(pa) | attr;
		va += BY2PG;
		pa += BY2PG;
	}
#endif
}

u64int *sv57, *sv48, *sv39, *pGiB;
u64int mmumode;

int block = 1;

void
kmapram(uintptr base, uintptr limit)
{
	u64int i;
	USED(base);
	USED(limit);
//	while(! block)
//	if (0) if ((base > 0) || (limit > 4 * GiB))
//		return;
	sv57[0] = ((((u64int)sv48)>>2)) | 1;
	print("%p is %p\n", sv57, sv57[0]);
	sv48[0] = ((((u64int)sv39)>>2)) | 1;
/*
print("%p is %p\n", sv48, sv48[0]);
	sv39[0] = ((((u64int)pGiB)>>2)) | 0x1;
print("%p is %p\n", sv39, sv39[0]);

	for(i = 0; i < 4; i++){
		pGiB[i] = (i<<10) | 0xf;
		print("%p is %p\n", &pGiB[i], pGiB[i]);
	}
*/
	for(i = 0; i < 4; i++){
		sv39[i] = ((0x40000000*i)>>2) | 0xcf;
		print("sv39:%p is %p\n", &sv39[i], sv39[i]);
	}
//while(! block);
wsatp(((uintptr)sv39>>12)|(8ULL<<60));
	if (0){
	// Probe.
	mmumode = 8ULL<<60;
	if (! mmumode) {
	wsatp((10ULL)<<60 | 0xf);
	if (rsatp() == (10ULL)<<60)mmumode = 10ULL<<60;
	}
	if (! mmumode) {
	wsatp((9ULL)<<60 | 0xf);
	if (rsatp() == (9ULL)<<60)mmumode = 9ULL<<60;
	}
	if (! mmumode) {
	wsatp((8ULL)<<60 | 0xf);
	if (rsatp() == (8ULL)<<60)mmumode = 8ULL<<60;
	}
	//print("rsatp %llx\n", rsatp());
	if (0)while (! block);
	switch(mmumode>>60) {
		case 10: 
				wsatp(((uintptr)sv57>>12)|mmumode);
				break;
		case 9:
				wsatp(((uintptr)sv48>>12)|mmumode);
				break;
		case 8:
				wsatp(((uintptr)sv39>>12)|mmumode);
				break;
		default:
			sbiputc('!');
			panic("rsatp is fucked");
			break;
	}
	return;
#ifdef xxx
	if(base < (uintptr)-KZERO && limit > (uintptr)-KZERO){
		kmapram(base, (uintptr)-KZERO);
		kmapram((uintptr)-KZERO, limit);
		return;
	}
	if(base < INITMAP)
		base = INITMAP;
	if(base >= limit || limit <= INITMAP)
		return;

	l1map((uintptr)kmapaddr(base), base, limit,
		PTEWRITE | PTEREAD);
#endif
	}
}

uintptr
mmukmap(uintptr va, uintptr pa, usize size)
{
	uintptr attr = 0, off;

	if(va == 0)
		return 0;

	off = pa & BY2PG-1;

//	TODO: When we support memory access cache flags, need to do a copy here
//	attr = va & PTEMA(7);
	attr |= PTEWRITE | PTEREAD;

	va &= -BY2PG;
	pa &= -BY2PG;

	l1map(va, pa, pa + off + size, attr);
	flushtlb();

	return va + off;
}

void*
vmap(uvlong pa, vlong size)
{
	USED(pa);
	USED(size);
#ifdef xxx
	static uintptr base = VMAP;
	uvlong pe = pa + size;
	uintptr va;

	va = base;
	base += PGROUND(pe) - (pa & -BY2PG);
	
	return (void*)mmukmap(va | PTEDEVICE, pa, size);
#endif
	panic("vmap");
	return nil;
}

void
vunmap(void *, vlong)
{
	panic("vunmap");
}

// That macro hackery is just too much for me to look at, and kenc should
// inline this function anyway.
static u64int vpn(uintptr va, int level)
{
	int shift = 12 + 9*level;
	uintptr val = (va>>shift)&0x1ff;
	if (0)print("<<<vpn(%p,%d)[shift %d]=%p>>>", va, level, shift, val);
	return val;
}

static uintptr 
ptephys(uintptr pte)
{
	return (pte & ~(0xFFFFULL<<48 | BY2PG-1))<<2;
}

/* there are still a few nasty assumptions in here about
 * pte not needing left shifts etc. riscv really fucked up
 * the pte format. Intel did better.
 */
static uintptr*
mmuwalk(uintptr va, int level)
{
	uintptr *table, pte;
	Page *pg;
	int i, x;
// In future, PTLEVELS will be dynamic.
	if (0)print("mmuwalk: va %p, walk to level %d starting from %d,", va, level, PTLEVELS);
	x = vpn(va, PTLEVELS-1);
	// N.B.: the assumption here is that mmutop was already set from 
	// p->mmutop. mmutop[x] will never be non-zero. If it is, it's a bug.
	table = m->mmutop;
	if (0)print("MMUWALK m is %p MMUTOP is %p\n", m,  m->mmutop);
	for(i = PTLEVELS-2; i >= level; i--){
		if (0)print("%d: table %p, index %d, pte %p: ", i, table, x, table[x]);
		pte = table[x];
		if (0)print(" %p %s, points to %p,", pte, pte & PTEVALID ? "valid" : "invalid", (pte>>10)<<12);
		if(pte & PTEVALID) {
			if (0){
				if(pte & (0xFFFFULL<<48))
					iprint("strange pte %#p va %#p, ", pte, va);
				pte &= ~(0xFFFFULL<<48 | BY2PG-1);
				pte <<= 2;
			}
			pte = (pte >>10)<<12;
		} else {
			pg = up->mmufree;
			if(pg == nil){
				if (0)print("up->mmufree is empty, return nil\n");
				return nil;
			}
			up->mmufree = pg->next;
			pg->va = va & -PGLSZ(i+1);
			if (0)print("page for pte is %p, ", pg->va);
			if((pg->next = up->mmuhead[i+1]) == nil)
				up->mmutail[i+1] = pg;
			up->mmuhead[i+1] = pg;
			if (0)print("SET up->mmuhead[%d] = %p, pte@ %p\n", i+1, pg, pg->pa);
			pte = pg->pa;
			memset(kmapaddr(pg->pa), 0, BY2PG);
			coherence();
			if (0)print(": SET table %p[%x]@%p = addr %p val%llx\n", table, x, &table[x], pte, ((pte>>12)<<10) | PTEVALID);
			table[x] = ((pte>>12)<<10) | PTEVALID;	// XXX: Does this need PA2PTE
		}
		table = kmapaddr(pte);
		if (0)print("kmapaddr of %p is %p, ", pte, table);
		x = vpn(va, (uintptr)i);
		if (0)print("\n");
	}
if (0)print("RETURN &%p[0x%x] = ", table, x);
if (0)print("%p\n", &table[x]);
	return &table[x];
}

u64int *
userpte(void *v)
{
	uintptr *pte = mmuwalk((uvlong)v, 0);
	if (v == nil){
		print("%p in up %p: not mapped\n", v, up);
		error("not mapped");
	}
	print("userpte %p -> pte %p contains %p phys %p v %p\n", v, pte, *pte, ptephys(*pte), kmapaddr(ptephys(*pte)));
	return pte;
}

void *
usertokernel(void *v)
{
	uintptr *pte = mmuwalk((uvlong)v, 0);
	if (v == nil){
		print("%p in up %p: not mapped\n", v, up);
		error("not mapped");
	}
	print("usertokernel %p -> pte %p contains %p phys %p v %p\n", v, pte, *pte, ptephys(*pte), kmapaddr(ptephys(*pte)));
	return kmapaddr((uintptr)pte);
}

static Proc *asidlist[256];

static int
allocasid(Proc *p)
{
	static Lock lk;
	Proc *x;
	int a;

	lock(&lk);
	a = p->asid;
	if(a < 0)
		a = -a;
	if(a == 0)
		a = p->pid;
	for(;; a++){
		a %= nelem(asidlist);
		if(a == 0)
			continue;	// reserved
		x = asidlist[a];
		if(x == p || x == nil || (x->asid < 0 && x->mach == nil))
			break;
	}
	p->asid = a;
	asidlist[a] = p;
	unlock(&lk);

	return x != p;
}

static void
freeasid(Proc *p)
{
	int a;

	a = p->asid;
	if(a < 0)
		a = -a;
	if(a > 0 && asidlist[a] == p)
		asidlist[a] = nil;
	p->asid = 0;
}

void
putasid(Proc *p)
{
	/*
	 * Prevent the following scenario:
	 *	pX sleeps on cpuA, leaving its page tables in mmutop
	 *	pX wakes up on cpuB, and exits, freeing its page tables
	 *  pY on cpuB allocates a freed page table page and overwrites with data
	 *  cpuA takes an interrupt, and is now running with bad page tables
	 * In theory this shouldn't hurt because only user address space tables
	 * are affected, and mmuswitch will clear mmutop before a user process is
	 * dispatched.  But empirically it correlates with weird problems, eg
	 * resetting of the core clock at 0x4000001C which confuses local timers.
	 */
	if(conf.nmach > 1)
		mmuswitch(nil);

	if(p->asid > 0)
		p->asid = -p->asid;
}

void
putmmu(uintptr va, uintptr pa, Page *pg)
{
	uintptr *pte, old;
	int s;
	uintptr pteattr = PTE2ATTR(pa) | PTEUSER | PTEACCESSED; // hack.

	if (pteattr & PTEWRITE)
		pteattr |= PTEDIRTY | PTEREAD;

if (1)print("pid %d putmmu(%p, %p, %p)\n", up ? up->pid : 0, va, pa, pg);
	s = splhi();
	while((pte = mmuwalk(va, 0)) == nil){
		spllo();
		up->mmufree = newpage(0, nil);
		if (0)print("putmmu: get a page %p, try again\n", up->mmufree);
		splhi();
	}
	if (up->pid == 5) soft();
	old = *pte;
	*pte = 0;
	if((old & PTEVALID) != 0)
		flushasidvall((uvlong)up->asid<<48 | va>>12);
	else
		flushasidva((uvlong)up->asid<<48 | va>>12);
	*pte = PA2PTE(pa) | pteattr;
	if (1)print("va %p pa %p pte %p *pte %p\n", va, pa, pte, *pte);
	if(needtxtflush(pg)){
		cachedwbinvse(kmap(pg), BY2PG);
		cacheiinvse((void*)va, BY2PG);
		donetxtflush(pg);
	}
	flushalltlb();
	flushvatlb(va);
	wsatp(rsatp());
	if (up->pid == 5) soft();
	splx(s);
	if (0)print("putmmu done\n");
}

static void
mmufree(Proc *p)
{
	int i;

	freeasid(p);

	for(i=1; i<PTLEVELS; i++){
		if(p->mmuhead[i] == nil)
			break;
		p->mmutail[i]->next = p->mmufree;
		p->mmufree = p->mmuhead[i];
		p->mmuhead[i] = p->mmutail[i] = nil;
	}
}

void
mmuswitch(Proc *p)
{
	uintptr va;
	Page *t;
	extern int block;
if (0)	print("SWITCH MMUTOP IS %p, @ 100 is %p\n", m->mmutop, m->mmutop[0x100]);
	for(va = UZERO; va < USTKTOP; va += PGLSZ(PTLEVELS-1))
		m->mmutop[PTLX(va, PTLEVELS-1)] = 0;

if (0)	print("p %p for setting tbr?\n", p);
	if(p == nil){
		// maybe flush the user mode entries? probably
		print("mmuswitch p is nil what to do?\n");
		wsatp(((uintptr)m->mmutop>>12)|(8ULL<<60));
		return;
	}

	if(p->newtlb){
		mmufree(p);
		p->newtlb = 0;
	}
if (0)	print("allocasid(p) %d\n", allocasid(p));
	if(allocasid(p)){
		print("NOT messing with ASID\n");
		flushasid((uvlong)p->asid<<48);
	}

	//print("set tbr to %llx\n", (uvlong)p->asid<<48 | PADDR(m->mmutop));
	//setttbr((uvlong)p->asid<<48 | PADDR(m->mmutop));

	for(t = p->mmuhead[PTLEVELS-1]; t != nil; t = t->next){
		va = t->va;
		u64int pte = ((t->pa)>>12) << 10 | PTEVALID; //| PTEUSER;
if (0)		print("Set mmutop[%llxx] to %llx\n", PTLX(va, PTLEVELS-1), pte );
		m->mmutop[PTLX(va, PTLEVELS-1)] = pte;
	}
	wsatp(((uintptr)m->mmutop>>12)|(8ULL<<60));
	if (0)print("MMUSWTICH: wrote satp: block\n");
	if (0)while (! block);
}

void
mmurelease(Proc *p)
{
	mmuswitch(nil);
	mmufree(p);
	freepages(p->mmufree, nil, 0);
	p->mmufree = nil;
}

void
flushmmu(void)
{
	int x;

	x = splhi();
	up->newtlb = 1;
	mmuswitch(up);
	splx(x);
}

void
checkmmu(uintptr, uintptr)
{
}
