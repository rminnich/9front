#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"

typedef struct Intvec Intvec;
struct Intvec
{
	Pcidev *p;
	void (*f)(Ureg*, void*);
	void *a;
};

typedef struct Ctlr Ctlr;
struct Ctlr
{
	uvlong	mem_base;
	uvlong	mem_size;
	uvlong	cfg_base;
	uvlong	cfg_size;
	uvlong	io_base;
	uvlong	io_size;

	int	bno, ubn;
	int	irq;

	u32int	*dbi;
	u32int	*cfg;
	Pcidev	*bridge;

	Lock;
	Intvec	vec[32];
};

static Ctlr ctlrs[2] = {
	{
		0x9040000000ULL, 0x40000000,
		0x9000000000ULL, 0x2000,
		0x9000020000ULL, 0x10000,
		0, 127, IRQpci3,
		(u32int*)(VIRTIO + 0x2600000),
	},
	{
		0xa040000000ULL, 0x40000000,
		0xa000000000ULL, 0x2000,
		0xa000020000ULL, 0x10000,
		128, 255, IRQpci5,
		(u32int*)(VIRTIO + 0x2800000),
	},
};

enum {
	IATU_MAX		= 8,
	IATU_INBOUND		= 1<<31,

	IATU_OFFSET		= 0x900/4,

	IATU_REGION_INDEX	= 0x00/4,

	IATU_REGION_CTRL_1	= 0x04/4,
		CTRL_1_INCREASE_REGION_SIZ	= 1<<13,

		CTRL_1_TYPE_SHIFT		= 0,
		CTRL_1_TYPE_MASK		= 0x1F<<CTRL_1_TYPE_SHIFT,
		CTRL_1_TYPE_MEM			= 0x0<<CTRL_1_TYPE_SHIFT,
		CTRL_1_TYPE_IO			= 0x2<<CTRL_1_TYPE_SHIFT,
		CTRL_1_TYPE_CFG0		= 0x4<<CTRL_1_TYPE_SHIFT,
		CTRL_1_TYPE_CFG1		= 0x5<<CTRL_1_TYPE_SHIFT,

	IATU_REGION_CTRL_2	= 0x08/4,
		CTRL_2_REGION_EN		= 1<<31,

	IATU_LWR_BASE_ADDR	= 0x0C/4,
	IATU_UPPER_BASE_ADDR	= 0x10/4,
	IATU_LWR_LIMIT_ADDR	= 0x14/4,
	IATU_LWR_TARGET_ADDR	= 0x18/4,
	IATU_UPPER_TARGET_ADDR	= 0x1C/4,
};

/* disable all iATU's */
static void
iatuinit(Ctlr *ctlr)
{
	u32int *reg = &ctlr->dbi[IATU_OFFSET];
	int index;

	for(index=0; index < IATU_MAX; index++){
		reg[IATU_REGION_INDEX] = index;
		reg[IATU_REGION_CTRL_2] &= ~CTRL_2_REGION_EN;

		reg[IATU_REGION_INDEX] |= IATU_INBOUND;
		reg[IATU_REGION_CTRL_2] &= ~CTRL_2_REGION_EN;
	}
}

static void
iatucfg(Ctlr *ctlr, int index, u32int type, uvlong target, uvlong base, uvlong size)
{
	uvlong limit = base + size - 1;
	u32int *reg = &ctlr->dbi[IATU_OFFSET];

	assert(size > 0);
	assert(index < IATU_MAX);
	assert((index & IATU_INBOUND) == 0);

	reg[IATU_REGION_INDEX] = index;
	reg[IATU_REGION_CTRL_2] &= ~CTRL_2_REGION_EN;

	reg[IATU_LWR_BASE_ADDR] = base;
	reg[IATU_UPPER_BASE_ADDR] = base >> 32;
	reg[IATU_LWR_LIMIT_ADDR] = limit;
	reg[IATU_LWR_TARGET_ADDR] = target;
	reg[IATU_UPPER_TARGET_ADDR] = target >> 32;

	type &= CTRL_1_TYPE_MASK;
	if(((size-1)>>32) != 0)
		type |= CTRL_1_INCREASE_REGION_SIZ;

	reg[IATU_REGION_CTRL_1] = type;
	reg[IATU_REGION_CTRL_2] = CTRL_2_REGION_EN;

	while((reg[IATU_REGION_CTRL_2] & CTRL_2_REGION_EN) == 0)
		microdelay(10);
}

static Ctlr*
bus2ctlr(int bno)
{
	Ctlr *ctlr;

	for(ctlr = ctlrs; ctlr < &ctlrs[nelem(ctlrs)]; ctlr++)
		if(bno >= ctlr->bno && bno <= ctlr->ubn)
			return ctlr;
	return nil;
}

static void*
cfgaddr(int tbdf, int rno)
{
	Ctlr *ctlr;

	ctlr = bus2ctlr(BUSBNO(tbdf));
	if(ctlr == nil)
		return nil;

	if(pciparentdev == nil){
		if(BUSDNO(tbdf) != 0 || BUSFNO(tbdf) != 0)
			return nil;
		return (uchar*)ctlr->dbi + rno;
	}

	iatucfg(ctlr, 0,
		pciparentdev->parent==nil? CTRL_1_TYPE_CFG0: CTRL_1_TYPE_CFG1,
		BUSBNO(tbdf)<<20 | BUSDNO(tbdf)<<15 | BUSFNO(tbdf)<<12,
		ctlr->cfg_base, ctlr->cfg_size);

	return (uchar*)ctlr->cfg + rno;
}

int
pcicfgrw32(int tbdf, int rno, int data, int read)
{
	u32int *p;

	if((p = cfgaddr(tbdf, rno & ~3)) != nil){
		if(read)
			data = *p;
		else
			*p = data;
	} else {
		data = -1;
	}
	return data;
}

int
pcicfgrw16(int tbdf, int rno, int data, int read)
{
	u16int *p;

	if((p = cfgaddr(tbdf, rno & ~1)) != nil){
		if(read)
			data = *p;
		else
			*p = data;
	} else {
		data = -1;
	}
	return data;
}

int
pcicfgrw8(int tbdf, int rno, int data, int read)
{
	u8int *p;

	if((p = cfgaddr(tbdf, rno)) != nil){
		if(read)
			data = *p;
		else
			*p = data;
	} else {
		data = -1;
	}
	return data;
}

enum {
	MISC_CONTROL_1		= 0x8BC/4,
		DBI_RO_WR_EN 	= 1<<0,
};

static void
pciinterrupt(Ureg *ureg, void *arg)
{
	Ctlr *ctlr = arg;
	Intvec *vec;

	ilock(ctlr);
	for(vec = ctlr->vec; vec < &ctlr->vec[nelem(ctlr->vec)]; vec++){
		if(vec->f != nil)
			(*vec->f)(ureg, vec->a);
	}
	iunlock(ctlr);
}

static void
pciintrinit(Ctlr *ctlr)
{

	intrenable(ctlr->irq+0, pciinterrupt, ctlr, BUSUNKNOWN, "pci");
	intrenable(ctlr->irq+1, pciinterrupt, ctlr, BUSUNKNOWN, "pci");
	intrenable(ctlr->irq+2, pciinterrupt, ctlr, BUSUNKNOWN, "pci");
	intrenable(ctlr->irq+3, pciinterrupt, ctlr, BUSUNKNOWN, "pci");
}

void
pciintrenable(int tbdf, void (*f)(Ureg*, void*), void *a)
{
	Ctlr *ctlr;
	Intvec *vec;
	Pcidev *p;

	ctlr = bus2ctlr(BUSBNO(tbdf));
	if(ctlr == nil){
		print("pciintrenable: %T: unknown controller\n", tbdf);
		return;
	}

	if((p = pcimatchtbdf(tbdf)) == nil){
		print("pciintrenable: %T: unknown device\n", tbdf);
		return;
	}
	if(pcimsidisable(p) < 0){
		print("pciintrenable: %T: device doesnt support vec\n", tbdf);
		return;
	}

	ilock(ctlr);
	for(vec = ctlr->vec; vec < &ctlr->vec[nelem(ctlr->vec)]; vec++){
		if(vec->p == p){
			vec->p = nil;
			break;
		}
	}
	for(vec = ctlr->vec; vec < &ctlr->vec[nelem(ctlr->vec)]; vec++){
		if(vec->p == nil){
			vec->p = p;
			vec->a = a;
			vec->f = f;
			break;
		}
	}
	iunlock(ctlr);

	if(vec >= &ctlr->vec[nelem(ctlr->vec)]){
		print("pciintrenable: %T: out of isr slots\n", tbdf);
		return;
	}
}

void
pciintrdisable(int tbdf, void (*f)(Ureg*, void*), void *a)
{
	Ctlr *ctlr;
	Intvec *vec;

	ctlr = bus2ctlr(BUSBNO(tbdf));
	if(ctlr == nil){
		print("pciintrenable: %T: unknown controller\n", tbdf);
		return;
	}

	ilock(ctlr);
	for(vec = ctlr->vec; vec < &ctlr->vec[nelem(ctlr->vec)]; vec++){
		if(vec->p == nil)
			continue;
		if(vec->p->tbdf == tbdf && vec->f == f && vec->a == a){
			vec->f = nil;
			vec->a = nil;
			vec->p = nil;
			break;
		}
	}
	iunlock(ctlr);
}

static void
rootinit(Ctlr *ctlr)
{
	uvlong base;
	ulong ioa;

	iatuinit(ctlr);

	ctlr->cfg = vmap(ctlr->cfg_base, ctlr->cfg_size);
	if(ctlr->cfg == nil)
		return;

	ctlr->dbi[MISC_CONTROL_1] |= DBI_RO_WR_EN;

	/* bus number */
	ctlr->dbi[PciPBN/4] &= ~0xFFFFFF;
	ctlr->dbi[PciPBN/4] |= ctlr->bno | (ctlr->bno+1)<<8 | ctlr->ubn<<16;

	/* command */
	ctlr->dbi[PciPCR/4] &= ~0xFFFF;
	ctlr->dbi[PciPCR/4] |= IOen | MEMen | MASen | SErrEn;

	/* device class/subclass */
	ctlr->dbi[PciRID/4] &= ~0xFFFF0000;
	ctlr->dbi[PciRID/4] |=  0x06040000;

	ctlr->dbi[PciBAR0/4] = 0;
	ctlr->dbi[PciBAR1/4] = 0;

	ctlr->dbi[MISC_CONTROL_1] &= ~DBI_RO_WR_EN;

	ctlr->ubn = pciscan(ctlr->bno, &ctlr->bridge, nil);
	if(ctlr->bridge == nil || ctlr->bridge->bridge == nil)
		return;

	pciintrinit(ctlr);

	iatucfg(ctlr, 1, CTRL_1_TYPE_IO, ctlr->io_base, ctlr->io_base, ctlr->io_size);
	iatucfg(ctlr, 2, CTRL_1_TYPE_MEM, ctlr->mem_base, ctlr->mem_base, ctlr->mem_size);

	ioa = ctlr->io_base;
	base = ctlr->mem_base;
	pcibusmap(ctlr->bridge, &base, &ioa, 1);

	pcihinv(ctlr->bridge);
}

static void
pcicfginit(void)
{
	int i;

	fmtinstall('T', tbdffmt);
	for(i = 0; i < nelem(ctlrs); i++)
		rootinit(&ctlrs[i]);
}

void
pcilx2klink(void)
{
	pcicfginit();
}
