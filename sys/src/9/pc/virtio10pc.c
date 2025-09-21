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
	if(r->type == Vio_port)
		return inb(r->port+p);
	else
		return *(u8int *)(r->mem+p);
}


u16int
vin16(Vio *r, int p)
{
	if(r->type == Vio_port)
		return ins(r->port+p);
	else 
		return *(u16int *)(r->mem+p);
}

u32int
vin32(Vio *r, int p)
{
	if(r->type == Vio_port)
		return inl(r->port+p);
	else
		return *(u32int *)((uchar *)r->mem+p);
}

u64int
vin64(Vio *r, int p)
{
	u64int v;
	if(r->type == Vio_port){
		v = inl(r->port+p);
		v |= (u64int)inl(r->port+p+4) << 32;
	}
	else
		v = *(u64int*)(r->mem+p);
	return v;
}

void
vout8(Vio *r, int p, u8int v)
{
	if(r->type == Vio_port)
		outb(r->port+p, v);
	else
		*(u8int *)(r->mem+p) = v;
}

void
vout16(Vio *r, int p, u16int v)
{
	if(r->type == Vio_port)
		outs(r->port+p, v);
	else
		*(u16int *)(r->mem+p) = v;
}

void
vout32(Vio *r, int p, u32int v)
{
	if(r->type == Vio_port)
		outl(r->port+p, v);
	else
		*(u32int *)(r->mem+p) = v;
}

void
vout64(Vio *r, int p, u64int v)
{
	if(r->type == Vio_port){
		outl(r->port+p, v);
		outl(r->port+p+4, v >> 32);
	}
	else
		*(u64int *)(r->mem+p) = v;
}

void
virtiounmap(Vio *r, usize sz)
{
	if(r->type == Vio_port)
		iofree(r->port);
	else
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
	if(addr+len > p->mem[bar].size)
		return nil;

	if(p->mem[bar].bar & 1){
		addr += p->mem[bar].bar & ~3;
		v->type = Vio_port;
		v->port = addr;
		ioalloc(v->port, size, 0, "ethervirtio10");
	}
	else{
		addr += p->mem[bar].bar & ~0xFULL;
		v->type = Vio_mem;
		v->mem = vmap(addr, size);
		if(v->mem == nil)
			return nil;
	}
	

	return v;	
}
