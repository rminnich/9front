#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"
#include	"../port/usb.h"
#include	"../port/usbxhci.h"

static void
coreinit(u32int *reg)
{
	enum {
		GCTL	= 0xC110/4,
			PWRDNSCALE_SHIFT = 19,
			PWRDNSCALE_MASK = 0x3FFF << PWRDNSCALE_SHIFT,
			PRTCAPDIR_SHIFT = 12,
			PRTCAPDIR_MASK = 3 << PRTCAPDIR_SHIFT,
			DISSCRAMBLE = 1<<3,
			DSBLCLKGTNG = 1<<0,

		GUCTL	= 0xC12C/4,
			USBHSTINAUTORETRY = 1<<14,

		GFLADJ	= 0xC630/4,
			GFLADJ_30MHZ_SDBND_SEL = 1<<7,
			GFLADJ_30MHZ_SHIFT = 0,
			GFLADJ_30MHZ_MASK = 0x3F << GFLADJ_30MHZ_SHIFT,

	};
	reg[GCTL] &= ~(PWRDNSCALE_MASK | DISSCRAMBLE | DSBLCLKGTNG | PRTCAPDIR_MASK);
	reg[GCTL] |= 2<<PWRDNSCALE_SHIFT | 1<<PRTCAPDIR_SHIFT;
	reg[GUCTL] |= USBHSTINAUTORETRY;
	reg[GFLADJ] = (reg[GFLADJ] & ~GFLADJ_30MHZ_MASK) | 0x20<<GFLADJ_30MHZ_SHIFT | GFLADJ_30MHZ_SDBND_SEL;
}

static int
reset(Hci *hp)
{
	static Xhci *ctlrs[2];
	Xhci *ctlr;
	int i;

	for(i=0; i<nelem(ctlrs); i++){
		if(ctlrs[i] == nil){
			uintptr base = VIRTIO + 0x2100000 + i*0x10000;
			ctlr = xhcialloc((u32int*)base, base - KZERO, 0x10000);
			if(ctlr == nil)
				break;
			ctlrs[i] = ctlr;
			goto Found;
		}
	}
	return -1;

Found:
	hp->tbdf = BUSUNKNOWN;
	hp->irq = IRQusb1 + i;
	xhcilinkage(hp, ctlr);

	coreinit(ctlr->mmio);

	return 0;
}

void
usbxhcilx2klink(void)
{
	addhcitype("xhci", reset);
}
