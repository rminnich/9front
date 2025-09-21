#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "../port/pci.h"
#include "../port/error.h"
#include "../port/virtio10.h"

u8int
vin8(Vio *r, int p)
{
	assert(r->type == Vio_mem);
	return *(u8int *)(r->mem+p);
}


u16int
vin16(Vio *r, int p)
{
	assert(r->type == Vio_mem);
	return *(u16int *)(r->mem+p);
}

u32int
vin32(Vio *r, int p)
{
	assert(r->type == Vio_mem);
	return *(u32int *)(r->mem+p);
}

u64int
vin64(Vio *r, int p)
{
	assert(r->type == Vio_mem);
	return *(u64int*)(r->mem+p);
	
}

void
vout8(Vio *r, int p, u8int v)
{
	assert(r->type == Vio_mem);
	*(uchar *)(r->mem+p) = v;
}

void
vout16(Vio *r, int p, u16int v)
{
	assert(r->type == Vio_mem);
	*(u16int *)(r->mem+p) = v;
}

void
vout32(Vio *r, int p, u32int v)
{
	assert(r->type == Vio_mem);
	*(u32int *)(r->mem+p) = v;
}

void
vout64(Vio *r, int p, u64int v)
{
	assert(r->type == Vio_mem);
	*(u64int *)(r->mem+p) = v;
}

void
virtiounmap(Vio *r, usize sz)
{
	assert(r->type == Vio_mem);
	vunmap(r->mem, sz);	
}

Vio*
virtiomapregs(Pcidev *p, int cap, int size, Vio *v)
{
	int bar, len;
	uvlong addr;

	if(cap < 0)
		return nil;
	bar = pcicfgr8(p, cap+4) % nelem(p->mem);
	addr = pcicfgr32(p, cap+8);
	len = pcicfgr32(p, cap+12);
	if(size <= 0)
		size = len;
	else if(len < size)
		return nil;
	if(p->mem[bar].bar & 1 ||  addr+len > p->mem[bar].size)
		return nil;

	addr += p->mem[bar].bar & ~0xFULL;
	v->type = Vio_mem;
	v->mem = vmap(addr, size);
	if(v->mem == nil)
		return nil;

	return v;	
}
