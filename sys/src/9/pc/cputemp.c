#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"

static int
intelcputempok(void)
{
	ulong regs[4];

	if(m->cpuiddx & Acpif)
	if(strcmp(m->cpuidid, "GenuineIntel") == 0){
		cpuid(6, 0, regs);
		return regs[0] & 1;
	}
	return 0;
}

static long
cputemprd0(Chan*, void *a, long n, vlong offset)
{
	char buf[32], *s;
	ulong msr, t, res, d;
	vlong emsr;
	ulong regs[4];
	static ulong tj;

	cpuid(6, 0, regs);
	if((regs[0] & 1) == 0)
		goto unsup;
	if(tj == 0){
		/*
		 * magic undocumented msr.  tj(max) is 100 or 85.
		 */
		tj = 100;
		d = m->cpuidmodel;
		d |= (m->cpuidax>>12) & 0xf0;
		if((d == 0xf && (m->cpuidax & 0xf)>1) || d == 0xe){
			if(rdmsr(0xee, &emsr) == 0){
				msr = emsr;
				if(msr & 1<<30)
					tj = 85;
			}
		}
	}
	if(rdmsr(0x19c, &emsr) < 0)
		goto unsup;
	msr = emsr;
	t = -1;
	if(msr & 1<<31){
		t = (msr>>16) & 127;
		t = tj - t;
	}
	res = (msr>>27) & 15;
	s = "";
	if((msr & 0x30) == 0x30)
		s = " alarm";
	snprint(buf, sizeof buf, "%ld±%uld%s\n", t, res, s);
	return readstr(offset, a, n, buf);
unsup:
	return readstr(offset, a, n, "-1±-1 unsupported\n");
}

static long
intelcputemprd(Chan *c, void *va, long n, vlong offset)
{
	int r, t, i, w;
	char *a;

	w = up->wired;
	a = va;
	t = 0;
	for(i = 0; i < conf.nmach; i++){
		procwired(up, i);
		sched();
		r = cputemprd0(c, a, n, offset);
		if(r == 0)
			break;
		offset -= r;
		if(offset < 0)
			offset = 0;
		n -= r;
		a = a + r;
		t += r;
	}
	up->wired = w;
	sched();
	return t;
}

/*
 * AMD exposes some sensors via PCI config space
 * on various devices depending on the CPU family.
 * This is largely undocumented, and has variance
 * depending on the motherboard vendor used.
 * Consumer grade often only has one sensor
 * per chip, however server grade can have up
 * to one sensor per core exposed this way.
 */
static Pcidev 	*amddevs[MAXMACH];
static int	namddevs;

static long
amd0ftemprd(Chan*, void *a, long n, vlong offset)
{
	static Lock lk;
	int i;
	char *s, *e, buf[16*4];
	long v, t, j, max;

	/* one sensor per core */
	max = 2;
	if(conf.nmach == 1)
		max = 1;
	else if(amddevs[1] != nil && conf.nmach == 2)
		max = 1;
	s = buf;
	e = buf + sizeof buf;
	lock(&lk);
	for(i = 0; i < namddevs; i++)
	for(j = 0; j < max; j++){
		pcicfgw32(amddevs[i], 0xe4, pcicfgr32(amddevs[i], 0xe4) & ~4 | j<<2);
		v = pcicfgr32(amddevs[i], 0xe4);
		if(m->cpuidstepping == 2)
			t = v>>16 & 0xff;
		else{
			t = v>>14 & 0x3ff;
			t *= 3;
			t /= 4;
		}
		t += -49;
		s = seprint(s, e, "%ld±1\n", t);
	}
	unlock(&lk);
	return readstr(offset, a, n, buf);
}

static long
amd10temprd(Chan*, void *a, long n, vlong offset)
{
	int i;
	char *s, *e, buf[16*nelem(amddevs)];
	u32int v;

	s = buf;
	e = buf + sizeof buf;
	for(i = 0; i < namddevs; i++){
		v = pcicfgr32(amddevs[i], 0xa4);
		v = ((v>>21)+4) / 8;
		s = seprint(s, e, "%ud±1\n", v);
	}
	return readstr(offset, a, n, buf);
}

static int
finddevs(void)
{
	Pcidev *p;

	for(p = nil; p = pcimatch(p, 0x1022, 0); )
		switch(p->did){
		case 0x1103:	/* 0f */
		case 0x1203:	/* 10 */
		case 0x1303:	/* 11 */
		case 0x1703:	/* 14 */
		case 0x1603:	/* 15 */
		case 0x1403:
		case 0x141d:
		case 0x1533:	/* 16 */
		case 0x1583:
		case 0x1480:	/* 17 */
		case 0x1450:
		case 0x15d0:
		case 0x1630:
		case 0x14a4:	/* 19 */
		case 0x14b5:
		case 0x14d8:
		case 0x14eb:
			amddevs[namddevs++] = p;
			if(namddevs == nelem(amddevs))
				return namddevs;
		}
	return namddevs;
}

static u32int
snmread(Pcidev *p, ulong addr)
{
	static Lock lk;
	u32int v;

	lock(&lk);
	pcicfgw32(p, 0x60, addr);
	v = pcicfgr32(p, 0x64);
	unlock(&lk);
	return v;
}

static long
amd17temprd(Chan*, void *a, long n, vlong offset)
{
	int i;
	char *s, *e, buf[16*nelem(amddevs)];
	u32int v, r;
	enum { Range = 1u<<19, Tjsel = 1u<<17 };

	s = buf;
	e = buf + sizeof buf;
	for(i = 0; i < namddevs; i++){
		r = snmread(amddevs[i], 0x59800);
		v = ((r >> 21)+4) / 8;
		if(r & (Range|Tjsel))
			v -= 49;
		s = seprint(s, e, "%ud±1\n", v);
	}
	return readstr(offset, a, n, buf);
}

typedef long Rdwrfn(Chan*, void*, long, vlong);

static Rdwrfn*
probe(void)
{
	if(intelcputempok())
		return intelcputemprd;

	if(strcmp(m->cpuidid,  "AuthenticAMD") == 0){
		if(finddevs() == 0)
			return nil;
		switch(m->cpuidfamily){
		case 0x0f:
			return amd0ftemprd;
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x14:
		case 0x15:
		case 0x16:
			return amd10temprd;
		case 0x17:
		case 0x19:
		case 0x1a:
			return amd17temprd;
		default:
			return nil;
		}
	}

	return nil;
}

void
cputemplink(void)
{
	Rdwrfn *fn;

	if((fn = probe()) != nil)
		addarchfile("cputemp", 0444, fn, nil);
}
