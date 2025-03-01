#include <u.h>
#include <libc.h>
#include <thread.h>
#include "usb.h"

int usbdebug;

static char *edir[] = {"in", "out", "inout"};
static char *etype[] = {"ctl", "iso", "bulk", "intr"};
static char* cnames[] =
{
	[0x00] "none",
	[0x01] "audio",
	[0x02] "comms",
	[0x03] "hid",
	[0x05] "phys",
	[0x06] "image",
	[0x07] "printer",
	[0x08] "storage",
	[0x09] "hub",
	[0x0A] "data",
	[0x0B] "smartcard",
	[0x0D] "drm",
	[0x0E] "video",
	[0x0F] "healthcare",
	[0x10] "av",
	[0x11] "billboard",
	[0x12] "usbc",
	[0x13] "display",
	[0x14] "mctp",
};
static char* devstates[] =
{
	"detached", "attached", "enabled", "assigned", "configured"
};

char*
classname(int c)
{
	static char buf[12];

	if(c >= 0 && c < nelem(cnames) && cnames[c] != nil && cnames[c][0] != '\0')
		return cnames[c];
	switch(c){
	case 0x3C:	/* I3C Device Class */
		return "i3c";
	case 0xDC:	/* Diagnostic Device */
		return "debug";
	case 0xE0:	/* Wireless Controller */
		return "wireless";
	case 0xEF:	/* Miscellaneous */
		return "misc";
	case 0xFE:	/* Application specific */
		return "application";
	case 0xFF:	/* Vendor specific */
		return "vendor";
	default:
		snprint(buf, sizeof(buf), "%d", c);
		return buf;
	}
}

static void
fmtprintiface(Fmt *f, Iface *i)
{
	int	j;
	Ep	*ep;
	char	*eds, *ets;

	fmtprint(f, "\t\tiface csp %s.%uld.%uld alt %d\n",
		classname(Class(i->csp)), Subclass(i->csp), Proto(i->csp), i->alt);
	for(j = 0; j < nelem(i->ep); j++){
		ep = i->ep[j];
		if(ep == nil)
			break;
		eds = ets = "";
		if(ep->dir <= nelem(edir))
			eds = edir[ep->dir];
		if(ep->type <= nelem(etype))
			ets = etype[ep->type];
		fmtprint(f, "\t\t  ep id %d addr %d dir %s type %s"
			"  attrib %x maxpkt %d ntds %d pollival %d\n",
			ep->id, ep->id & Epmax, eds, ets, ep->attrib,
			ep->maxpkt, ep->ntds, ep->pollival);
	}
}

static void
fmtprintconf(Fmt *f, Usbdev *d, int ci)
{
	int i;
	Conf *c;
	Iface *fc;

	c = d->conf[ci];
	fmtprint(f, "\tconf: cval %d attrib %x %d mA\n",
		c->cval, c->attrib, c->milliamps);
	for(i = 0; i < Niface; i++){
		for(fc = c->iface[i]; fc != nil; fc = fc->next)
			fmtprintiface(f, fc);
	}
	for(i = 0; i < Nddesc; i++)
		if(d->ddesc[i] == nil)
			break;
		else if(d->ddesc[i]->conf == c){
			fmtprint(f, "\t\tdev desc %x[%d]: %.*H\n",
				d->ddesc[i]->data.bDescriptorType,
				d->ddesc[i]->data.bLength,
				d->ddesc[i]->data.bLength, &d->ddesc[i]->data);
		}
}

int
Ufmt(Fmt *f)
{
	int i;
	Dev *d;
	Usbdev *ud;

	d = va_arg(f->args, Dev*);
	if(d == nil)
		return fmtprint(f, "<nildev>\n");
	fmtprint(f, "%s", d->dir);
	ud = d->usb;
	if(ud == nil)
		return fmtprint(f, " %ld refs\n", d->ref);
	fmtprint(f, " csp %s.%uld.%uld",
		classname(Class(ud->csp)), Subclass(ud->csp), Proto(ud->csp));
	fmtprint(f, " vid %#ux did %#ux", ud->vid, ud->did);
	fmtprint(f, " refs %ld\n", d->ref);
	fmtprint(f, "\t%s %s %s\n", ud->vendor, ud->product, ud->serial);
	for(i = 0; i < Nconf; i++){
		if(ud->conf[i] == nil)
			break;
		else
			fmtprintconf(f, ud, i);
	}
	return 0;
}

char*
estrdup(char *s)
{
	char *d;

	d = strdup(s);
	if(d == nil)
		sysfatal("strdup: %r");
	setmalloctag(d, getcallerpc(&s));
	return d;
}

void*
emallocz(ulong size, int zero)
{
	void *x;

	x = mallocz(size, zero);
	if(x == nil)
		sysfatal("malloc: %r");
	setmalloctag(x, getcallerpc(&size));
	return x;
}

