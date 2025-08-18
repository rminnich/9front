#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>

/* perfect approximation to NTSC = .299r+.587g+.114b when 0 ≤ r,g,b < 256 */
#define RGB2K(r,g,b)	((156763*(r)+307758*(g)+59769*(b))>>19)

/* 25.7 fixed-point number operations */

#define FMASK		((1<<7) - 1)
#define int2fix(n)	((vlong)(n)<<7)
#define fix2int(n)	((n)>>7)
#define fixmul(a,b)	((vlong)(a)*(vlong)(b) >> 7)
#define fixfrac(n)	((n)&FMASK)
#define lerp(a,b,t)	((a) + ((((b) - (a))*(t))>>7))

typedef struct Sampler Sampler;
typedef struct Blitter Blitter;

struct Sampler
{
	Memimage *i;
	uchar *a;
	Rectangle r;
	int bpl;
	int cmask;
	long Δx, Δy;
};

struct Blitter
{
	Memimage *i;
	uchar *a;
	int bpl;
	int cmask;
};

static Point
fix_xform(Point p, Warp m)
{
	return (Point){
		fixmul(p.x, m[0][0]) + fixmul(p.y, m[0][1]) + m[0][2],
		fixmul(p.x, m[1][0]) + fixmul(p.y, m[1][1]) + m[1][2]
	};
}

static ulong
getpixel(Sampler *s, Point pt)
{
	uchar *p, r, g, b, a;
	ulong val, chan, ctype, ov, v;
	int nb, off, bpp, npack;

	val = 0;
	a = 0xFF;
	r = g = b = 0xAA;	/* garbage */
	p = s->a + pt.y*s->bpl + (pt.x*s->i->depth >> 3);

	/* pixelbits() */
	switch(bpp=s->i->depth){
	case 1:
	case 2:
	case 4:
		npack = 8/bpp;
		off = pt.x%npack;
		val = p[0] >> bpp*(npack-1-off);
		val &= s->cmask;
		break;
	case 8:
		val = p[0];
		break;
	case 16:
		val = p[0]|(p[1]<<8);
		break;
	case 24:
		val = p[0]|(p[1]<<8)|(p[2]<<16);
		break;
	case 32:
		val = p[0]|(p[1]<<8)|(p[2]<<16)|(p[3]<<24);
		break;
	}

	/* fast path for true color images */
	switch(s->i->chan){
	case RGBA32:
		return val;
	case XRGB32:
	case RGB24:
		return val<<8|0xFF;
	case ABGR32:
		return p[3]|(p[2]<<8)|(p[1]<<16)|(p[0]<<24);
	case XBGR32:
	case BGR24:
		return p[2]<<8|(p[1]<<16)|(p[0]<<24)|0xFF;
	}

	while(bpp<32){
		val |= val<<bpp;
		bpp *= 2;
	}

	/* imgtorgba() */
	for(chan=s->i->chan; chan; chan>>=8){
		ctype = TYPE(chan);
		nb = s->i->nbits[ctype];
		ov = v = val & s->i->mask[ctype];
		val >>= nb;

		while(nb < 8){
			v |= v<<nb;
			nb *= 2;
		}
		v >>= (nb-8);

		switch(ctype){
		case CRed:
			r = v;
			break;
		case CGreen:
			g = v;
			break;
		case CBlue:
			b = v;
			break;
		case CAlpha:
			a = v;
			break;
		case CGrey:
			r = g = b = v;
			break;
		case CMap:
			p = s->i->cmap->cmap2rgb+3*ov;
			r = p[0];
			g = p[1];
			b = p[2];
			break;
		}
	}
	return (r<<24)|(g<<16)|(b<<8)|a;
}

static void
putpixel(Blitter *blt, Point dp, ulong rgba)
{
	uchar *p, r, g, b, a, m;
	ulong chan, ctype, ov, v;
	int nb, off, bpp, npack, sh, mask;

	bpp = blt->i->depth;
	p = blt->a + dp.y*blt->bpl + (dp.x*bpp >> 3);

	/* fast path for true color images */
	switch(blt->i->chan){
	case RGBA32:
		p[0] = rgba;
		p[1] = rgba>>8;
		p[2] = rgba>>16;
		p[3] = rgba>>24;
		return;
	case XRGB32:
	case RGB24:
		p[0] = rgba>>8;
		p[1] = rgba>>16;
		p[2] = rgba>>24;
		return;
	case ABGR32:
		p[0] = rgba>>24;
		p[1] = rgba>>16;
		p[2] = rgba>>8;
		p[3] = rgba;
		return;
	case XBGR32:
	case BGR24:
		p[0] = rgba>>24;
		p[1] = rgba>>16;
		p[2] = rgba>>8;
		return;
	}

	/* rgbatoimg() */
	v = 0;
	r = rgba>>24;
	g = rgba>>16;
	b = rgba>>8;
	a = rgba;
	for(chan=blt->i->chan; chan; chan>>=8){
		ctype = TYPE(chan);
		nb = blt->i->nbits[ctype];
		switch(ctype){
		case CRed:
			v |= (r>>(8-nb)) << blt->i->shift[CRed];
			break;
		case CGreen:
			v |= (g>>(8-nb)) << blt->i->shift[CGreen];
			break;
		case CBlue:
			v |= (b>>(8-nb)) << blt->i->shift[CBlue];
			break;
		case CAlpha:
			v |= (a>>(8-nb)) << blt->i->shift[CAlpha];
			break;
		case CMap:
			m = blt->i->cmap->rgb2cmap[(r>>4)*256+(g>>4)*16+(b>>4)];
			v |= (m>>(8-nb)) << blt->i->shift[CMap];
			break;
		case CGrey:
			m = RGB2K(r,g,b);
			v |= (m>>(8-nb)) << blt->i->shift[CGrey];
			break;
		}
	}

	/* blit op */
	ov = p[0]|p[1]<<8|p[2]<<16|p[3]<<24;

	mask = blt->cmask;
	if(blt->i->depth < 8){
		npack = 8/bpp;
		off = dp.x%npack;
		sh = bpp*(npack-1-off);
		mask <<= sh;
		v <<= sh;
	}
	v = (ov &~ mask) | (v & mask);
	p[0] = v;
	p[1] = v>>8;
	p[2] = v>>16;
	p[3] = v>>24;
}

static ulong
sample1(Sampler *s, Point p)
{
	if(p.x >= s->r.min.x && p.x < s->r.max.x
	&& p.y >= s->r.min.y && p.y < s->r.max.y)
		return getpixel(s, p);
	else if(s->i->flags & Frepl){
		p = drawrepl(s->r, p);
		return getpixel(s, p);
	}
	/* edge handler: constant */
	return 0;
}

static ulong
bilinear(Sampler *s, Point p)
{
	ulong c00, c01, c10, c11, o;
	uchar c0, c1, c;
	int i;

	c00 = sample1(s, p);
	p.x++;
	c01 = sample1(s, p);
	p.x--; p.y++;
	c10 = sample1(s, p);
	p.x++;
	c11 = sample1(s, p);

	/* avoid processing alpha if possible */
	i = s->i->flags & Falpha? 0: 8;
	o = s->i->flags & Falpha? 0: 0xFF;
	for(; i < 4*8; i += 8){
		c0 = c00>>i; c0 = lerp(c0, c01>>i & 0xFF, s->Δx);
		c1 = c10>>i; c1 = lerp(c1, c11>>i & 0xFF, s->Δx);
		c  = lerp(c0, c1, s->Δy);
		o |= (ulong)c << i;
	}
	return o;
}

static ulong
nearest(Sampler *s, Point p)
{
	return sample1(s, p);
}

static ulong (*sample)(Sampler*, Point) = bilinear;

int
memaffinewarp(Memimage *d, Rectangle r, Memimage *s, Point sp0, Warp m)
{
	Sampler samp;
	Blitter blit;
	Point sp, dp, p2, p2₀;
	Rectangle dr;
	ulong c;

	dr = d->clipr;
	rectclip(&dr, d->r);
	if(rectclip(&r, dr) == 0)
		return 0;

	samp.r = s->clipr;
	if(rectclip(&samp.r, s->r) == 0)
		return 0;

	samp.i = s;
	samp.a = s->data->bdata + s->zero;
	samp.bpl = sizeof(ulong)*s->width;
	samp.cmask = (1ULL << s->depth) - 1;

	blit.i = d;
	blit.a = d->data->bdata + d->zero;
	blit.bpl = sizeof(ulong)*d->width;
	blit.cmask = (1ULL << d->depth) - 1;

	/*
	 * incremental affine warping technique from:
	 * 	“Fast Affine Transform for Real-Time Machine Vision Applications”,
	 * 	Lee, S., Lee, GG., Jang, E.S., Kim, WY,
	 * 	Intelligent Computing.  ICIC 2006. LNCS, vol 4113.
	 */
	p2 = p2₀ = fix_xform((Point){int2fix(r.min.x - dr.min.x), int2fix(r.min.y - dr.min.y)}, m);
	for(dp.y = r.min.y; dp.y < r.max.y; dp.y++){
	for(dp.x = r.min.x; dp.x < r.max.x; dp.x++){
		samp.Δx = fixfrac(p2.x);
		samp.Δy = fixfrac(p2.y);

		sp.x = sp0.x + fix2int(p2.x);
		sp.y = sp0.y + fix2int(p2.y);

		c = sample(&samp, sp);
		putpixel(&blit, dp, c);

		p2.x += m[0][0];
		p2.y += m[1][0];
	}
		p2.x = p2₀.x += m[0][1];
		p2.y = p2₀.y += m[1][1];
	}
	return 0;
}
