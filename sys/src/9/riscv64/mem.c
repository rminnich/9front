#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#define INITMAP	(ROUND((uintptr)end + BY2PG, PGLSZ(1)))

void
mmu0init(uintptr *l1)
{
	uintptr va, pa, pe, attr;

	/* VDRAM */
	attr = PTEREAD | PTEWRITE | PTEEXEC;
	pe = -KZERO;
	for(pa = VDRAM - KZERO; pa < pe; pa += PGLSZ(PTLEVELS-1))
		l1[PTLX(pa, PTLEVELS-1)] = PA2PTE(pa) | PTEVALID | attr;

	/* DRAM - INITMAP */
	pe = INITMAP;
	for(pa = VDRAM - KZERO, va = VDRAM; pa < pe; pa += PGLSZ(1), va += PGLSZ(1))
		l1[PTL1X(va, 1)] = PA2PTE(pa) | PTEVALID | attr;

	/* VIRTIO */
	attr = PTEREAD | PTEWRITE | PTEDEVICE;
	pe = PHYSIOEND;
	for(pa = PHYSIO, va = VIRTIO; pa < pe; pa += PGLSZ(1), va += PGLSZ(1)){
		if(((pa|va) & PGLSZ(1)-1) != 0){
			l1[PTL1X(va, 1)] = PA2PTE((uintptr)l1) | PTEVALID;
			for(; pa < pe && ((va|pa) & PGLSZ(1)-1) != 0; pa += PGLSZ(0), va += PGLSZ(0)){
//				assert(l1[PTLX(va, 0)] == 0);
				l1[PTLX(va, 0)] = PA2PTE(pa) | PTEVALID | attr;
			}
			break;
		}
		l1[PTL1X(va, 1)] = PA2PTE(pa) | PTEVALID | attr;
	}

	if(PTLEVELS > 2)
	for(va = KSEG0; va != 0; va += PGLSZ(2))
		l1[PTLX(va, 2)] = PA2PTE((uintptr)&l1[L1TABLEX(va, 1)]) | PTEVALID;
}

void
meminit(void)
{
	char *p;
	/* note: this is the LIMIT: physical address of top of memory. 
	 * 0xc000_0000 + 0x0800_0000 = 0xc800_0000 */
	uintptr l = 3*GiB + 128 * MiB;
	l = 2 * GiB + 512 * MiB;

	if(p = getconf("*maxmem"))
		l = strtoull(p, 0, 0);
	print("meminit: end %p, KZERO %p, PGROUND %p\n", end, KZERO, PGROUND((uintptr)end - KTZERO));
	conf.mem[0].base = ROUND(PGROUND((uintptr)end - KTZERO) + (uintptr)0x804FFFFF, 0x200000);
	conf.mem[0].limit = l;

	print("CONF ZERO BASE is 0x%llx LIMIT is 0x%llx\n", conf.mem[0].base, l);
	if(l > KLIMIT)
		l = KLIMIT;
	print("l is now %#llx\n", l);
	kmapram(conf.mem[0].base, l);

	conf.mem[0].npage = (conf.mem[0].limit - conf.mem[0].base)/BY2PG;
	print("fuck 0x%llx 0x%llx\n", (conf.mem[0].limit - conf.mem[0].base),(conf.mem[0].limit - conf.mem[0].base)/BY2PG);
	print("base %p limit %p npage %lud\n", conf.mem[0].base,  conf.mem[0].limit,conf.mem[0].npage);
}
