#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "../port/pci.h"
#include "../port/error.h"
#include "../port/virtio10.h"

enum {
	Spew = 0,
};

u8int
vin8(Vio *r, int p)
{
	u8int i;
	assert(r->type == Vio_mem);
	coherence();
	i = *(u8int *)(r->mem+p);
	if (Spew)print("read8@%p:%02x\n", r->mem+p, i);
	return i;
}


u16int
vin16(Vio *r, int p)
{
	u16int i;
	assert(r->type == Vio_mem);
	coherence();
	i = *(u16int *)(r->mem+p);
	if (Spew)print("read16@%p:%04x\n", r->mem+p, i);
	return i;
}

u32int
vin32(Vio *r, int p)
{
	u32int i;
	assert(r->type == Vio_mem);
	coherence();
	i = *(u32int *)(r->mem+p);
	if (Spew)print("read32@%p:%08x\n", r->mem+p, i);
	return i;
}

u64int
vin64(Vio *r, int p)
{
	u64int i;
	assert(r->type == Vio_mem);
	coherence();
	i = *(u64int*)(r->mem+p);
	if (Spew)print("read64@%p:%llx\n", r->mem+p, i);
	return i;	
}

void
vout8(Vio *r, int p, u8int v)
{
	assert(r->type == Vio_mem);
	*(uchar *)(r->mem+p) = v;
	if (Spew)print("write8@%p:%#02x\n", (r->mem+p), v);
	coherence();
}

void
vout16(Vio *r, int p, u16int v)
{
	assert(r->type == Vio_mem);
	*(u16int *)(r->mem+p) = v;
	if (Spew)print("write16@%p:%#04x\n", (r->mem+p), v);
	coherence();
}

void
vout32(Vio *r, int p, u32int v)
{
	assert(r->type == Vio_mem);
	*(u32int *)(r->mem+p) = v;
	if (Spew)print("write32@%p:%#08x\n", (r->mem+p), v);
	coherence();
}

void
vout64(Vio *r, int p, u64int v)
{
	assert(r->type == Vio_mem);
	*(u64int *)(r->mem+p) = v;
	if (Spew)print("write64@%p:%#llx\n", (r->mem+p), v);
	coherence();
}

void
virtiounmap(Vio *r, usize sz)
{
	assert(r->type == Vio_mem);
	vunmap(r->mem, sz);	
	coherence();
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
