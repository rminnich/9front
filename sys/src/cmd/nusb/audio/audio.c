#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <pcm.h>
#pragma varargck type "!" Pcmdesc
#include "usb.h"

enum {
	Paudio1 = 0x00,
	Paudio2 = 0x20,

	Csamfreq = 0x01,
	Cclockvalid = 0x02,

	/* audio 1 */
	Rsetcur	= 0x01,
	Rgetcur = 0x81,
	Rgetmin,
	Rgetmax,
	Rgetres,

	/* audio 2 */
	Rcur = 0x01,
	Rrange = 0x02,

	/* feature unit control values */
	Cmute = 1,
	Cvolume,
	Cbass,
	Cmid,
	Ctreble,
	Cagc = 7,
	Cbassboost = 9,
	Cloudness,
	Cnum,

	Conlycur = 1<<Cmute | 1<<Cagc | 1<<Cbassboost | 1<<Cloudness,
	Cszone = Conlycur | 1<<Cbass | 1<<Cmid | 1<<Ctreble,
	Cmask1 = (1<<Cvolume | Conlycur | Cszone)>>1,
	Cmask2 = (
		3<<Cmute*2 | 3<<Cvolume*2 | 3<<Cbass*2 |
		3<<Cmid*2 | 3<<Ctreble*2 | 3<<Cagc*2 |
		3<<Cbassboost*2 | 3<<Cloudness*2
	)>>2,

	Silence = 0x8000,
};

char *ctrlname[Cnum] = {
	[Cmute] = "mute",
	[Cvolume] = "volume",
	[Cbass] = "bass",
	[Cmid] = "mid",
	[Ctreble] = "treble",
	[Cagc] = "agc",
	[Cbassboost] = "bassboost",
	[Cloudness] = "loudness",
};

typedef struct Range Range;
struct Range
{
	uint	min;
	uint	max;
};

typedef struct Ctrl Ctrl;
struct Ctrl
{
	uchar	id;
	uchar	cs;
	uchar	cn;
	short	cur;
	short	min;
	short	max;
	short	res;
};

typedef struct Aconf Aconf;
struct Aconf
{
	Pcmdesc;

	Ep	*ep;
	int	bps;	/* subslot size (bytes per sample) */
	int	terminal;
	Range	*freq;
	int	nfreq;
	Iface	*zb;

	/* audio 1 */
	int	controls;

	/* audio 2 */
	int	clock;
};

int audiodelay = 1764;	/* 40 ms for 44.1kHz */

char user[] = "audio";

Dev *adev;
Iface *ac;
Ep *epin;
Ep *epout;
int inoff, outoff;
File *ctl;
File *status;
File *volume;
Ctrl ctrl[32];
int nctrl;

Iface*
findiface(Conf *conf, int class, int subclass, int id)
{
	int i;
	Iface *iface;

	for(i = 0; i < nelem(conf->iface); i++){
		iface = conf->iface[i];
		if(iface == nil || Class(iface->csp) != class || Subclass(iface->csp) != subclass)
			continue;
		if(id == -1 || iface->id == id)
			return iface;
	}
	return nil;
}

Desc*
findiad(int csp)
{
	int i;
	Desc *dd;
	uchar *b;

	for(i = 0; i < nelem(adev->usb->ddesc); i++){
		dd = adev->usb->ddesc[i];
		if(dd == nil || dd->data.bDescriptorType != 11 || dd->data.bLength != 8)
			continue;
		b = dd->data.bbytes;
		if(b[0] == ac->id && b[0]+b[1] <= Niface && csp == CSP(b[2], b[3], b[4]))
			return dd;
	}
	return nil;
}

Desc*
findacheader(void)
{
	Desc *dd;
	uchar *b;
	int i;

	for(i = 0; i < nelem(adev->usb->ddesc); i++){
		dd = adev->usb->ddesc[i];
		if(dd == nil || dd->iface != ac || dd->data.bDescriptorType != 0x24)
			continue;
		if(dd->data.bLength < 8 || dd->data.bbytes[0] != 1)
			continue;
		b = dd->data.bbytes;
		switch(Proto(ac->csp)){
		case Paudio1:
			if(dd->data.bLength == 8+b[5])
				return dd;
			break;
		case Paudio2:
			if(dd->data.bLength == 9)
				return dd;
			break;
		}
	}
	return nil;
}

Desc*
findterminal(int id)
{
	Desc *dd;
	uchar *b;
	int i;

	for(i = 0; i < nelem(adev->usb->ddesc); i++){
		dd = adev->usb->ddesc[i];
		if(dd == nil || dd->iface != ac)
			continue;
		if(dd->data.bDescriptorType != 0x24 || dd->data.bLength < 4)
			continue;
		b = dd->data.bbytes;
		if(b[1] != id)
			continue;
		/* check descriptor length according to type and proto */
		switch(b[0]<<16 | dd->data.bLength<<8 | Proto(ac->csp)){
		case 0x020C00|Paudio1:
		case 0x030900|Paudio1:
		case 0x021100|Paudio2:
		case 0x030c00|Paudio2:
			return dd;
		}
	}
	return nil;
}

Desc*
findclocksource(int id)
{
	Desc *dd;
	uchar *b;
	int i;

	for(i = 0; i < nelem(adev->usb->ddesc); i++){
		dd = adev->usb->ddesc[i];
		if(dd == nil || dd->iface != ac)
			continue;
		if(dd->data.bDescriptorType != 0x24 || dd->data.bLength != 8)
			continue;
		b = dd->data.bbytes;
		if(b[0] == 0x0A && b[1] == id)
			return dd;
	}
	return nil;
}

Rune
tofmt(int v)
{
	switch(v){
	case 1: return L's';
	case 2: return L'u';
	case 3: return L'f';
	case 4: return L'a';
	case 5: return L'µ';
	}
	return 0;
}

int
parseasdesc1(Desc *dd, Aconf *c)
{
	uchar *b;
	Range *f;

	b = dd->data.bbytes;
	switch(dd->data.bDescriptorType<<8 | b[0]){
	case 0x2501:	/* CS_ENDPOINT, EP_GENERAL */
		if(dd->data.bLength != 7)
			return -1;
		c->controls = b[1];
		break;

	case 0x2401:	/* CS_INTERFACE, AS_GENERAL */
		if(dd->data.bLength != 7)
			return -1;
		c->terminal = b[1];
		c->fmt = tofmt(GET2(&b[3]));
		if(!c->fmt)
			return -1;
		break;

	case 0x2402:	/* CS_INTERFACE, FORMAT_TYPE */
		if(dd->data.bLength < 8 || b[1] != 1)
			return -1;
		c->channels = b[2];
		c->bps = b[3];
		c->bits = b[4];
		if(b[5] == 0){	/* continuous frequency range */
			c->nfreq = 1;
			c->freq = emallocz(sizeof(*f), 0);
			c->freq->min = b[6] | (int)b[7]<<8 | (int)b[8]<<16;
			c->freq->max = b[9] | (int)b[10]<<8 | (int)b[11]<<16;
		} else {		/* discrete sampling frequencies */
			c->nfreq = b[5];
			c->freq = emallocz(c->nfreq * sizeof(*f), 0);
			b += 6;
			for(f = c->freq; f < c->freq+c->nfreq; f++, b += 3)
				f->min = f->max = b[0] | (int)b[1]<<8 | (int)b[2]<<16;
		}
		break;
	}
	return 0;
}

int
parseasdesc2(Desc *dd, Aconf *c)
{
	uchar *b;

	b = dd->data.bbytes;
	switch(dd->data.bDescriptorType<<8 | b[0]){
	case 0x2401:	/* CS_INTERFACE, AS_GENERAL */
		if(dd->data.bLength != 16 || b[3] != 1)
			return -1;
		c->terminal = b[1];
		c->channels = b[8];
		c->fmt = tofmt(GET4(&b[4]));
		if(!c->fmt)
			return -1;
		break;

	case 0x2402:	/* CS_INTERFACE, FORMAT_TYPE */
		if(dd->data.bLength != 6 || b[1] != 1)
			return -1;
		c->bps = b[2];
		c->bits = b[3];
		break;
	}
	return 0;
}

int
setclock(Aconf *c, int speed)
{
	uchar b[4];
	int index;

	switch(Proto(ac->csp)){
	case Paudio1:
		if((c->controls & 1) == 0)
			break;
		b[0] = speed;
		b[1] = speed >> 8;
		b[2] = speed >> 16;
		index = c->ep->id & Epmax;
		if(c->ep->dir == Ein)
			index |= 0x80;
		if(usbcmd(adev, Rh2d|Rclass|Rep, Rsetcur, Csamfreq<<8, index, b, 3) < 0)
			break;
		if(usbcmd(adev, Rd2h|Rclass|Rep, Rgetcur, Csamfreq<<8, index, b, 3) != 3)
			break;
		return b[0] | b[1]<<8 | b[2]<<16;
	case Paudio2:
		PUT4(b, speed);
		index = c->clock<<8 | ac->id;
		if(usbcmd(adev, Rh2d|Rclass|Riface, Rcur, Csamfreq<<8, index, b, 4) < 0)
			break;
		if(usbcmd(adev, Rd2h|Rclass|Riface, Rcur, Csamfreq<<8, index, b, 4) != 4)
			break;
		return GET4(b);
	}
	return -1;
}

int
getclockrange(Aconf *c)
{
	uchar b[2 + 32*12];
	int i, n, rc;

	rc = usbcmd(adev, Rd2h|Rclass|Riface, Rrange, Csamfreq<<8, c->clock<<8 | ac->id, b, sizeof(b));
	if(rc < 0)
		return -1;
	if(rc < 2 || rc < 2 + (n = GET2(b))*12){
		werrstr("invalid response");
		return -1;
	}
	c->nfreq = n;
	c->freq = emallocz(n*sizeof(Range), 0);
	for(i = 0; i < n; i++)
		c->freq[i] = (Range){GET4(&b[2 + i*12]), GET4(&b[6 + i*12])};
	return 0;
}

int
setvalue(uchar id, uchar hi, uchar lo, short value, int sz)
{
	uchar b[2];

	if(sz == 1)
		b[0] = value;
	else
		PUT2(b, value);

	if(Proto(ac->csp) == Paudio1)
		return usbcmd(adev, Rh2d|Rclass|Riface, Rsetcur, hi<<8 | lo, id<<8 | ac->id, b, sz);

	return usbcmd(adev, Rh2d|Rclass|Riface, Rcur, hi<<8 | lo, id<<8 | ac->id, b, sz);
}

int
getvalue(int r, uchar id, uchar cs, uchar cn, short *value)
{
	uchar b[2 + 1*3*2];
	int rc, i, n, sz;

	*value = 0;
	i = 0;
	sz = (Cszone & (1<<cs)) ? 1 : 2;
	if(Proto(ac->csp) == Paudio2){
		sz = 2 + 1*3*sz;
		i = r - Rgetmin;
		r = r == Rgetcur ? Rcur : Rrange;
	}
	rc = usbcmd(adev, Rd2h|Rclass|Riface, r, cs<<8 | cn, id<<8 | ac->id, b, sz);
	if(rc < 0)
		return -1;
	if(rc < 1)
		goto Invalid;
	if(r == Rrange){
		rc -= 2;
		if(rc < 3 || (n = GET2(b)) < 1)
			goto Invalid;
		else if(rc == n*3*2)
			*value = GET2(&b[2 + i*2]);
		else if(rc == n*3*1)
			*value = b[2 + i];
		else
			goto Invalid;
	} else
		*value = rc > 1 ? GET2(b) : b[0];
	return 0;
Invalid:
	werrstr("invalid response");
	return -1;
}

int
getvalues(Ctrl *c)
{
	char *s;
	int onlycur;

	onlycur = Conlycur & (1<<c->cs);
	if(((s = "cur") && getvalue(Rgetcur, c->id, c->cs, c->cn, &c->cur) < 0) ||
		(!onlycur && (
			((s = "min") && getvalue(Rgetmin, c->id, c->cs, c->cn, &c->min) < 0) ||
			((s = "max") && getvalue(Rgetmax, c->id, c->cs, c->cn, &c->max) < 0) ||
			((s = "res") && getvalue(Rgetres, c->id, c->cs, c->cn, &c->res) < 0)))){
		fprint(2, "getvalue: %s: %s: %r\n", ctrlname[c->cs], s);
		return -1;
	}
	if(onlycur){
		c->min = 0;
		c->max = 1;
		c->res = 1;
	} else if(c->res < 1){
		fprint(2, "getvalue: %s: invalid res: %d\n", ctrlname[c->cs], c->res);
		return -1;
	}
	return 0;
}

int
cmpctrl(void *a_, void *b_)
{
	Ctrl *a, *b;
	a = a_;
	b = b_;
	if(a->id != b->id)
		return a->id - b->id;
	if(a->cs != b->cs)
		return a->cs - b->cs;
	return a->cn - b->cn;
}

void
findcontrols1(void)
{
	int i, k, n, x;
	uchar cs, cn;
	uchar *b;
	Desc *dd;
	Ctrl *c;

	for(i = 0; i < nelem(adev->usb->ddesc); i++){
		dd = adev->usb->ddesc[i];
		if(dd == nil)
			continue;
		b = dd->data.bbytes;
		switch(dd->data.bDescriptorType<<8 | b[0]){
		case 0x2406:	/* CS_INTERFACE, FEATURE_UNIT */
			if(dd->data.bLength < 8 || nctrl >= nelem(ctrl) || (n = b[3]) < 1)
				continue;
			for(k = 0; k < nctrl; k++)
				if(ctrl[k].id == b[1])
					break;
			if(k < nctrl)
				break;
			for(k = 4, cn = 0; k <= dd->data.bLength-2-n-1; k += n, cn++){
				x = (n > 1 ? GET2(b+k) : b[k]) & Cmask1;
				if(x == 0)
					continue;
				for(cs = 1; cs < Cnum && nctrl < nelem(ctrl); cs++, x >>= 1){
					if((x & 1) != 1)
						continue;
					c = &ctrl[nctrl++];
					c->cs = cs;
					c->cn = cn;
					c->id = b[1];
					if(getvalues(c) != 0)
						nctrl--;
				}
			}
			break;
		}
	}
}

void
findcontrols2(void)
{
	int i, k, n, x;
	uchar cs, cn;
	uchar *b;
	Desc *dd;
	Ctrl *c;

	for(i = 0; i < nelem(adev->usb->ddesc); i++){
		dd = adev->usb->ddesc[i];
		if(dd == nil)
			continue;
		b = dd->data.bbytes;
		switch(dd->data.bDescriptorType<<8 | b[0]){
		case 0x2406:	/* CS_INTERFACE, FEATURE_UNIT */
			if(dd->data.bLength < 9 || nctrl >= nelem(ctrl))
				continue;
			for(k = 0; k < nctrl; k++)
				if(ctrl[k].id == b[1])
					break;
			if(k < nctrl)
				break;
			n = 4;
			for(k = 3, cn = 0; k <= dd->data.bLength-2-n-1; k += n, cn++){
				x = GET4(b+k) & Cmask2;
				if(x == 0)
					continue;
				for(cs = 1; cs < Cnum && nctrl < nelem(ctrl); cs++, x >>= 2){
					if((x & 3) != 3)
						continue;
					c = &ctrl[nctrl++];
					c->cs = cs;
					c->cn = cn;
					c->id = b[1];
					if(getvalues(c) != 0)
						nctrl--;
				}
			}
			break;
		}
	}
}

void
parseterminal2(Desc *dd, Aconf *c)
{
	uchar *b;

	b = dd->data.bbytes;
	switch(b[0]){
	case 0x02:	/* INPUT_TERMINAL */
		c->clock = b[5];
		break;
	case 0x03:	/* OUTPUT_TERMINAL */
		c->clock = b[6];
		break;
	}
}

void
parsestream(int id)
{
	Iface *as, *zb;
	Desc *dd;
	Ep *e;
	Aconf *c;
	uchar *b;
	int i;

	/* find AS interface */
	as = findiface(adev->usb->conf[0], Claudio, 2, id);

	/* find zero-bandwidth setting, if any */
	for(zb = as; zb != nil && zb->alt != 0; zb = zb->next);

	/* enumerate through alt. settings */
	for(; as != nil; as = as->next){
		c = emallocz(sizeof(*c), 1);
		as->aux = c;

		/* find AS endpoint */
		for(i = 0; i < nelem(as->ep); i++){
			e = as->ep[i];
			if(e != nil && e->type == Eiso && (e->attrib>>4 & 3) == Edata){
				c->ep = e;
				break;
			}
		}
		if(c->ep == nil){
		Skip:
			free(c);
			as->aux = nil;
			continue;
		}
		c->zb = zb;

		/* parse AS descriptors */
		for(i = 0; i < nelem(adev->usb->ddesc); i++){
			dd = adev->usb->ddesc[i];
			if(dd == nil || dd->iface != as)
				continue;
			switch(Proto(ac->csp)){
			case Paudio1:
				if(parseasdesc1(dd, c) != 0)
					goto Skip;
				break;
			case Paudio2:
				if(parseasdesc2(dd, c) != 0)
					goto Skip;
				break;
			}
		}

		if(Proto(ac->csp) == Paudio1)
			continue;

		dd = findterminal(c->terminal);
		if(dd == nil)
			goto Skip;
		parseterminal2(dd, c);

		dd = findclocksource(c->clock);
		if(dd == nil)
			goto Skip;
		b = dd->data.bbytes;
		/* check that clock has rw frequency control */
		if((b[3] & 3) != 3)
			goto Skip;
		if(getclockrange(c) != 0){
			fprint(2, "getclockrange %d: %r\n", c->clock);
			goto Skip;
		}
	}
}

int
fmtcmp(Pcmdesc *a, Pcmdesc *b)
{
	if(a->rate != b->rate)
		return a->rate - b->rate;
	if(a->channels != b->channels)
		return a->channels - b->channels;
	if(a->bits != b->bits)
		return a->bits - b->bits;
	if(a->fmt != b->fmt){
		if(a->fmt == L's' || a->fmt == L'f')
			return 1;
		if(b->fmt == L's' || b->fmt == L'f')
			return -1;
	}
	return a->fmt - b->fmt;
}

Dev*
setupep(Ep *e, Pcmdesc *fmt, int exact)
{
	Aconf *c, *bestc;
	Ep *beste, *ep;
	int n, r, dir;
	Pcmdesc p;
	Range *f;
	Dev *d;

	if(e == epout && outoff){
		werrstr("output disabled");
		return nil;
	}
	if(e == epin && inoff){
		werrstr("input disabled");
		return nil;
	}

	dir = e->dir;
	ep = e;
	bestc = nil;
	beste = nil;
	r = -1;

	for(;e != nil; e = e->next){
		c = e->iface->aux;
		if(c == nil || e != c->ep || e->dir != dir || c->bits != 8*c->bps)
			continue;
		for(f = c->freq; f != c->freq+c->nfreq; f++){
			p = *c;
			if(f->min >= fmt->rate)
				p.rate = f->min;
			else if(f->max <= fmt->rate)
				p.rate = f->max;
			else
				p.rate = fmt->rate;
			if((n = fmtcmp(&p, fmt)) == 0 || bestc == nil){
Better:
				c->rate = p.rate;
				bestc = c;
				beste = e;
				if((r = n) == 0)
					goto Done;
				continue;
			}
			/* both better, but the new is closer */
			if(n > 0 && (r > 0 && fmtcmp(&p, bestc) < 0))
				goto Better;
			/* both worse, but the new one is better so far */
			if(n < 0 && (r < 0 && fmtcmp(&p, bestc) > 0))
				goto Better;
		}
	}

Done:
	if(bestc == nil || (exact && r != 0)){
		werrstr("no altc found");
		return nil;
	}
	e = beste;
	c = bestc;

	/* jump to alt 0 before trying to do anything */
	if(c->zb != nil)
		setalt(adev, c->zb);

	if(setalt(adev, e->iface) < 0){
		werrstr("setalt: %r");
		return nil;
	}

	/* ignore errors as updated clock isn't always required */
	r = setclock(c, c->rate);

	if((d = openep(adev, e)) == nil){
		werrstr("openep: %r");
		return nil;
	}

	/* update the rate only if the value makes sense */
	if(r >= c->rate*9/10 && r <= c->rate*10/9)
		c->rate = r;

	ep->aux = c;
	devctl(d, "samplesz %d", c->channels*c->bits/8);
	devctl(d, "sampledelay %d", audiodelay);
	devctl(d, "hz %d", c->rate);
	devctl(d, "name audio%sU%s", e->dir==Ein ? "in" : "", adev->hname);
	return d;
}

char *
ctrlvalue(Ctrl *c)
{
	static char v[64];
	int x, n;

	if((Conlycur & (1<<c->cs)) == 0){
		if((ushort)c->cur == Silence)
			x = 0;
		else {
			n = (c->max - c->min);
			x = (c->cur - c->min) * 100;
			if(n == 0)
				x = 100;
			else
				x /= n;
		}
	}else{
		x = !!c->cur;
	}
	snprint(v, sizeof(v), "%d", x);
	return v;
}

char *
seprintaconf(char *s, char *e, Ep *ep)
{
	Pcmdesc d;
	Range *f;
	Aconf *c;
	int dir;

	dir = ep->dir;
	for(; ep != nil; ep = ep->next){
		c = ep->iface->aux;
		if(c == nil || ep != c->ep || ep->dir != dir || c->bits != 8*c->bps)
			continue;
		for(f = c->freq; f != c->freq+c->nfreq; f++){
			d = c->Pcmdesc;
			d.rate = f->min;
			s = seprint(s, e, " %!", d);
			if(f->min < f->max){
				d.rate = f->max;
				s = seprint(s, e, "-%!", d);
			}
		}
	}
	return seprint(s, e, "\n");
}

void
fsread(Req *r)
{
	static char msg[2048];
	Ctrl *cur, *prev;
	char *s, *e;
	Aconf *c;
	int i;

	s = msg;
	e = msg+sizeof(msg);
	*s = 0;

	if(r->fid->file == ctl){
		if(epout != nil)
			s = seprint(s, e, "out %s\n", outoff ? "off" : "on");
		if(epin != nil)
			seprint(s, e, "in %s\n", inoff ? "off" : "on");
	} else if(r->fid->file == status){
		s = seprint(s, e, "bufsize %6d buffered %6d\n", 0, 0);
		if(epout != nil)
			s = seprintaconf(seprint(s, e, "fmtout"), e, epout);
		if(epin != nil)
			seprintaconf(seprint(s, e, "fmtin"), e, epin);
	} else if(r->fid->file == volume){
		if(epout != nil){
			c = epout->aux;
			s = seprint(s, e, "delay %d\nfmtout %!\nspeed %d\n",
				audiodelay, c->Pcmdesc, c->rate);
		}
		if(epin != nil){
			c = epin->aux;
			s = seprint(s, e, "fmtin %!\n", c->Pcmdesc);
		}

		prev = nil;
		for(i = 0; i < nctrl; i++, prev = cur){
			cur = ctrl+i;
			if(prev == nil || prev->id != cur->id || prev->cs != cur->cs)
				s = seprint(s, e, "%s%s%d", i?"\n":"", ctrlname[cur->cs], cur->id);
			s = seprint(s, e, " %s", ctrlvalue(cur));
		}
		if(i > 0)
			seprint(s, e, "\n");
	} else {
		respond(r, "protocol botch");
		return;
	}

	readstr(r, msg);
	respond(r, nil);
}

int
setctrl(char *f[8], int nf)
{
	int i, j, id, n, x, sz;
	char *s, *e;
	uchar cs;
	Ctrl *c;

	id = -1;
	s = nil;
	for(e = f[0]; *e; e++){
		if(*e >= '0' && *e <= '9'){
			s = e;
			id = strtol(e, &e, 10);
			break;
		}
	}
	if(id < 0 || *e != 0){
Invalid:
		werrstr("invalid name");
		goto Error;
	}
	*s = 0;
	for(i = 0, c = ctrl; i < nctrl; i++, c++){
		if(ctrl[i].id == id && strcmp(ctrlname[c->cs], f[0]) == 0)
			break;
	}
	if(i == nctrl)
		goto Invalid;

	cs = c->cs;
	for(j = 1; j < nf; j++){
		x = atoi(f[j]);
		if((Conlycur & (1<<c->cs)) == 0){
			sz = (Cszone & (1<<c->cs)) ? 1 : 2;
			if(x <= 0 && sz == 2)
				x = Silence;
			else{
				n = (c->max - c->min) / c->res;
				x = (x * n * c->res)/100 + c->min;
				if(x < c->min)
					x = c->min;
				else if(x > c->max)
					x = c->max;
			}
		} else {
			x = !!x;
			sz = 1;
		}
Groupset:
		if(setvalue(c->id, c->cs, c->cn, x, sz) < 0)
			goto Error;
		if(getvalue(Rgetcur, c->id, c->cs, c->cn, &c->cur) < 0)
			goto Error;

		c++;
		if(c >= ctrl+nctrl || c->id != id || c->cs != cs)
			break; /* just ignore anything extra */
		if(nf == 2)
			goto Groupset;
	}

	return 0;
Error:
	werrstr("%s: %r", f[0]);
	return -1;
}

void
fswrite(Req *r)
{
	char msg[256], *f[8];
	int nf, off;
	Pcmdesc pd;
	Aconf *c;
	Dev *d;
	Ep *e;

	snprint(msg, sizeof(msg), "%.*s",
		utfnlen((char*)r->ifcall.data, r->ifcall.count), (char*)r->ifcall.data);
	nf = tokenize(msg, f, nelem(f));
	if(nf < 2){
Invalid:
		respond(r, "invalid ctl message");
		return;
	}
	c = epout->aux;

	if(r->fid->file == ctl){
		if(strcmp(f[0], "out") == 0)
			e = epout;
		else if(strcmp(f[0], "in") == 0)
			e = epin;
		else
			goto Invalid;
		if(strcmp(f[1], "on") == 0)
			off = 0;
		else if(strcmp(f[1], "off") == 0)
			off = 1;
		else
			goto Invalid;
		c = e->aux;
		if(c->zb == nil){
			respond(r, "no zero-bandwidth config");
			return;
		}
		if(setalt(adev, c->zb) < 0)
			goto Error;
		if(e == epout)
			outoff = off;
		else
			inoff = off;
	} else if(r->fid->file != volume){
		werrstr("protocol botch");
		goto Error;
	} else if((strcmp(f[0], "speed") == 0 || strcmp(f[0], "fmtout") == 0) && epout != nil){
		if(f[0][0] == 's'){
			pd = c->Pcmdesc;
			pd.rate = atoi(f[1]);
		}else if(mkpcmdesc(f[1], &pd) != 0)
			goto Error;
Setup:
		if((d = setupep(epout, &pd, 1)) == nil)
			goto Error;
		closedev(d);
	} else if(strcmp(f[0], "fmtin") == 0 && epin != nil){
		if(mkpcmdesc(f[1], &pd) != 0)
			goto Error;
		if((d = setupep(epin, &pd, 1)) == nil)
			goto Error;
		closedev(d);
	} else if(strcmp(f[0], "delay") == 0 && epout != nil){
		pd = c->Pcmdesc;
		audiodelay = atoi(f[1]);
		goto Setup;
	} else if(setctrl(f, nf) < 0){
		goto Error;
	}

	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
	return;
Error:
	responderror(r);
}

Srv fs = {
	.read = fsread,
	.write = fswrite,
};

void
usage(void)
{
	fprint(2, "%s devid\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	char buf[32];
	Dev *ed;
	Desc *dd;
	Conf *conf;
	Aconf *c;
	Ep *e;
	uchar *b;
	int i;

	ARGBEGIN {
	case 'D':
		chatty9p++;
		break;
	case 'd':
		usbdebug++;
		break;
	} ARGEND;

	if(argc == 0)
		usage();

	fmtinstall('!', pcmdescfmt);
	if((adev = getdev(*argv)) == nil)
		sysfatal("getdev: %r");

	conf = adev->usb->conf[0];
	ac = findiface(conf, Claudio, 1, -1);
	if(ac == nil)
		sysfatal("no audio control interface");

	switch(Proto(ac->csp)){
	case Paudio1:
		dd = findacheader();
		if(dd == nil)
			sysfatal("no audio control header");
		b = dd->data.bbytes;
		for(i = 6; i < dd->data.bLength-2; i++)
			parsestream(b[i]);
		break;
	case Paudio2:
		dd = findiad(CSP(Claudio, 0, Paudio2));
		if(dd == nil)
			sysfatal("no audio function");
		b = dd->data.bbytes;
		for(i = b[0]+1; i < b[0]+b[1]; i++)
			parsestream(i);
		break;
	}

	for(i = 0; i < nelem(adev->usb->ep); i++){
		for(e = adev->usb->ep[i]; e != nil; e = e->next){
			c = e->iface->aux;
			if(c != nil && c->ep == e)
				break;
		}
		if(e == nil)
			continue;
		switch(e->dir){
		case Ein:
			if(epin != nil)
				continue;
			epin = e;
			break;
		case Eout:
			if(epout != nil)
				continue;
			epout = e;
			break;
		}
		if((ed = setupep(e, &pcmdescdef, 0)) == nil){
			fprint(2, "setupep: %s: %r\n", epout == e ? "out" : "in");
			if(e == epin)
				epin = nil;
			if(e == epout)
				epout = nil;
			continue;
		}
		closedev(ed);
	}
	if(epout == nil && epin == nil)
		sysfatal("no streams found");

	switch(Proto(ac->csp)){
	case Paudio1:
		findcontrols1();
		break;
	case Paudio2:
		findcontrols2();
		break;
	}
	qsort(ctrl, nctrl, sizeof(Ctrl), cmpctrl);

	fs.tree = alloctree(user, "usb", DMDIR|0555, nil);
	snprint(buf, sizeof buf, "audioctlU%s", adev->hname);
	ctl = createfile(fs.tree->root, buf, user, 0666, nil);
	snprint(buf, sizeof buf, "audiostatU%s", adev->hname);
	status = createfile(fs.tree->root, buf, user, 0444, nil);
	snprint(buf, sizeof buf, "volumeU%s", adev->hname);
	volume = createfile(fs.tree->root, buf, user, 0666, nil);

	snprint(buf, sizeof buf, "%d.audio", adev->id);
	postsharesrv(&fs, nil, "usb", buf);

	exits(nil);
}
