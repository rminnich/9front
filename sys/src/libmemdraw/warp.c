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
	ulong (*getpixel)(Sampler*, Point);
};

struct Blitter
{
	Memimage *i;
	uchar *a;
	int bpl;
	int cmask;
	void (*putpixel)(Blitter*, Point, ulong);
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
getpixel_k1(Sampler *s, Point pt)
{
	uchar *p;
	ulong off, npack, v;

	p = s->a + pt.y*s->bpl + (pt.x >> 3);
	npack = 8;
	off = pt.x % npack;
	v = p[0] >> (npack-1-off) & 0x1;
	v *= 0xFFFFFFFF;
	return v|0xFF;
}

static ulong
getpixel_k2(Sampler *s, Point pt)
{
	uchar *p, v;
	ulong off, npack;

	p = s->a + pt.y*s->bpl + (pt.x*2 >> 3);
	npack = 8/2;
	off = pt.x % npack;
	v = p[0] >> 2*(npack-1-off) & 0x3;
	v |= v<<2;
	v |= v<<4;
	return (v<<24)|(v<<16)|(v<<8)|0xFF;
}

static ulong
getpixel_k4(Sampler *s, Point pt)
{
	uchar *p, v;
	ulong off, npack;

	p = s->a + pt.y*s->bpl + (pt.x*4 >> 3);
	npack = 8/4;
	off = pt.x % npack;
	v = p[0] >> 4*(npack-1-off) & 0xF;
	v |= v<<4;
	return (v<<24)|(v<<16)|(v<<8)|0xFF;
}

static ulong
getpixel_k8(Sampler *s, Point pt)
{
	uchar *p, v;

	p = s->a + pt.y*s->bpl + pt.x;
	v = p[0];
	return (v<<24)|(v<<16)|(v<<8)|0xFF;
}

static ulong
getpixel_m8(Sampler *s, Point pt)
{
	uchar *p, m, r, g, b;

	p = s->a + pt.y*s->bpl + pt.x;
	m = p[0];
	p = s->i->cmap->cmap2rgb+3*m;
	r = p[0];
	g = p[1];
	b = p[2];
	return (r<<24)|(g<<16)|(b<<8)|0xFF;
}

static ulong
getpixel_x1r5g5b5(Sampler *s, Point pt)
{
	uchar *p, r, g, b;
	ulong val;

	p = s->a + pt.y*s->bpl + pt.x*2;
	val = p[0]|(p[1]<<8);
	b = val&0x1F; b = (b<<3)|(b>>2);
	val >>= 5;
	g = val&0x1F; g = (g<<3)|(g>>2);
	val >>= 5;
	r = val&0x1F; r = (r<<3)|(r>>2);
	return (r<<24)|(g<<16)|(b<<8)|0xFF;
}

static ulong
getpixel_r5g6b5(Sampler *s, Point pt)
{
	uchar *p, r, g, b;
	ulong val;

	p = s->a + pt.y*s->bpl + pt.x*2;
	val = p[0]|(p[1]<<8);
	b = val&0x1F; b = (b<<3)|(b>>2);
	val >>= 5;
	g = val&0x3F; g = (g<<2)|(g>>4);
	val >>= 6;
	r = val&0x1F; r = (r<<3)|(r>>2);
	return (r<<24)|(g<<16)|(b<<8)|0xFF;
}

static ulong
getpixel_r8g8b8(Sampler *s, Point pt)
{
	uchar *p, r, g, b;

	p = s->a + pt.y*s->bpl + pt.x*3;
	b = p[0];
	g = p[1];
	r = p[2];
	return (r<<24)|(g<<16)|(b<<8)|0xFF;
}

static ulong
getpixel_r8g8b8a8(Sampler *s, Point pt)
{
	uchar *p, r, g, b, a;

	p = s->a + pt.y*s->bpl + pt.x*4;
	a = p[0];
	b = p[1];
	g = p[2];
	r = p[3];
	return (r<<24)|(g<<16)|(b<<8)|a;
}

static ulong
getpixel_a8r8g8b8(Sampler *s, Point pt)
{
	uchar *p, r, g, b, a;

	p = s->a + pt.y*s->bpl + pt.x*4;
	b = p[0];
	g = p[1];
	r = p[2];
	a = p[3];
	return (r<<24)|(g<<16)|(b<<8)|a;
}

static ulong
getpixel_x8r8g8b8(Sampler *s, Point pt)
{
	uchar *p, r, g, b;

	p = s->a + pt.y*s->bpl + pt.x*4;
	b = p[0];
	g = p[1];
	r = p[2];
	return (r<<24)|(g<<16)|(b<<8)|0xFF;
}

static ulong
getpixel_b8g8r8(Sampler *s, Point pt)
{
	uchar *p, r, g, b;

	p = s->a + pt.y*s->bpl + pt.x*3;
	r = p[0];
	g = p[1];
	b = p[2];
	return (r<<24)|(g<<16)|(b<<8)|0xFF;
}

static ulong
getpixel_a8b8g8r8(Sampler *s, Point pt)
{
	uchar *p, r, g, b, a;

	p = s->a + pt.y*s->bpl + pt.x*4;
	r = p[0];
	g = p[1];
	b = p[2];
	a = p[3];
	return (r<<24)|(g<<16)|(b<<8)|a;
}

static ulong
getpixel_x8b8g8r8(Sampler *s, Point pt)
{
	uchar *p, r, g, b;

	p = s->a + pt.y*s->bpl + pt.x*4;
	r = p[0];
	g = p[1];
	b = p[2];
	return (r<<24)|(g<<16)|(b<<8)|0xFF;
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
	switch(bpp = s->i->depth){
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

	while(bpp < 32){
		val |= val<<bpp;
		bpp *= 2;
	}

	/* imgtorgba() */
	for(chan = s->i->chan; chan; chan >>= 8){
		if((ctype = TYPE(chan)) == CIgnore){
			val >>= s->i->nbits[ctype];
			continue;
		}
		nb = s->i->nbits[ctype];
		ov = v = val & s->i->mask[ctype];
		val >>= nb;

		while(nb < 8){
			v |= v<<nb;
			nb *= 2;
		}
		v >>= nb-8;

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
putpixel_k1(Blitter *blt, Point dp, ulong rgba)
{
	uchar *p, r, g, b, m;
	int off, npack, sh, mask;

	p = blt->a + dp.y*blt->bpl + (dp.x >> 3);

	r = rgba>>24;
	g = rgba>>16;
	b = rgba>>8;
	m = RGB2K(r,g,b);
	m >>= 8-1;

	mask = 0x1;
	npack = 8;
	off = dp.x%npack;
	sh = npack-1-off;
	mask <<= sh;
	m <<= sh;
	p[0] = (p[0] ^ m) & mask ^ p[0];
}

static void
putpixel_k2(Blitter *blt, Point dp, ulong rgba)
{
	uchar *p, r, g, b, m;
	int off, npack, sh, mask;

	p = blt->a + dp.y*blt->bpl + (dp.x*2 >> 3);

	r = rgba>>24;
	g = rgba>>16;
	b = rgba>>8;
	m = RGB2K(r,g,b);
	m >>= 8-2;

	mask = 0x3;
	npack = 8/2;
	off = dp.x%npack;
	sh = 2*(npack-1-off);
	mask <<= sh;
	m <<= sh;
	p[0] = (p[0] ^ m) & mask ^ p[0];
}

static void
putpixel_k4(Blitter *blt, Point dp, ulong rgba)
{
	uchar *p, r, g, b, m;
	int off, npack, sh, mask;

	p = blt->a + dp.y*blt->bpl + (dp.x*4 >> 3);

	r = rgba>>24;
	g = rgba>>16;
	b = rgba>>8;
	m = RGB2K(r,g,b);
	m >>= 8-4;

	mask = 0xF;
	npack = 8/4;
	off = dp.x%npack;
	sh = 4*(npack-1-off);
	mask <<= sh;
	m <<= sh;
	p[0] = (p[0] ^ m) & mask ^ p[0];
}

static void
putpixel_k8(Blitter *blt, Point dp, ulong rgba)
{
	uchar *p, r, g, b, m;

	p = blt->a + dp.y*blt->bpl + dp.x;

	r = rgba>>24;
	g = rgba>>16;
	b = rgba>>8;
	m = RGB2K(r,g,b);
	p[0] = m;
}

static void
putpixel_m8(Blitter *blt, Point dp, ulong rgba)
{
	uchar *p, r, g, b, m;

	p = blt->a + dp.y*blt->bpl + dp.x;

	r = rgba>>24;
	g = rgba>>16;
	b = rgba>>8;
	m = blt->i->cmap->rgb2cmap[(r>>4)*256+(g>>4)*16+(b>>4)];
	p[0] = m;
}

static void
putpixel_x1r5g5b5(Blitter *blt, Point dp, ulong rgba)
{
	uchar *p, r, g, b;
	ushort v;

	p = blt->a + dp.y*blt->bpl + dp.x*2;
	r = rgba>>24;
	g = rgba>>16;
	b = rgba>>8;
	v = r>>(8-5);
	v = (v<<5)|(g>>(8-5));
	v = (v<<5)|(b>>(8-5));
	p[0] = v;
	p[1] = v>>8;
}

static void
putpixel_r5g6b5(Blitter *blt, Point dp, ulong rgba)
{
	uchar *p, r, g, b;
	ushort v;

	p = blt->a + dp.y*blt->bpl + dp.x*2;
	r = rgba>>24;
	g = rgba>>16;
	b = rgba>>8;
	v = r>>(8-5);
	v = (v<<6)|(g>>(8-6));
	v = (v<<5)|(b>>(8-5));
	p[0] = v;
	p[1] = v>>8;
}

static void
putpixel_r8g8b8(Blitter *blt, Point dp, ulong rgba)
{
	uchar *p;

	p = blt->a + dp.y*blt->bpl + dp.x*3;
	p[0] = rgba>>8;
	p[1] = rgba>>16;
	p[2] = rgba>>24;
}

static void
putpixel_r8g8b8a8(Blitter *blt, Point dp, ulong rgba)
{
	uchar *p;

	p = blt->a + dp.y*blt->bpl + dp.x*4;
	p[0] = rgba;
	p[1] = rgba>>8;
	p[2] = rgba>>16;
	p[3] = rgba>>24;
}

static void
putpixel_a8r8g8b8(Blitter *blt, Point dp, ulong rgba)
{
	uchar *p;

	p = blt->a + dp.y*blt->bpl + dp.x*4;
	p[0] = rgba>>8;
	p[1] = rgba>>16;
	p[2] = rgba>>24;
	p[3] = rgba;
}

static void
putpixel_x8r8g8b8(Blitter *blt, Point dp, ulong rgba)
{
	uchar *p;

	p = blt->a + dp.y*blt->bpl + dp.x*4;
	p[0] = rgba>>8;
	p[1] = rgba>>16;
	p[2] = rgba>>24;
}

static void
putpixel_b8g8r8(Blitter *blt, Point dp, ulong rgba)
{
	uchar *p;

	p = blt->a + dp.y*blt->bpl + dp.x*3;
	p[0] = rgba>>24;
	p[1] = rgba>>16;
	p[2] = rgba>>8;
}

static void
putpixel_a8b8g8r8(Blitter *blt, Point dp, ulong rgba)
{
	uchar *p;

	p = blt->a + dp.y*blt->bpl + dp.x*4;
	p[0] = rgba>>24;
	p[1] = rgba>>16;
	p[2] = rgba>>8;
	p[3] = rgba;
}

static void
putpixel_x8b8g8r8(Blitter *blt, Point dp, ulong rgba)
{
	uchar *p;

	p = blt->a + dp.y*blt->bpl + dp.x*4;
	p[0] = rgba>>24;
	p[1] = rgba>>16;
	p[2] = rgba>>8;
}

static void
putpixel(Blitter *blt, Point dp, ulong rgba)
{
	uchar *p, r, g, b, a, m;
	ulong chan, ov, v;
	int off, npack, sh, mask;

	p = blt->a + dp.y*blt->bpl + (dp.x*blt->i->depth >> 3);

	/* rgbatoimg() */
	v = 0;
	r = rgba>>24;
	g = rgba>>16;
	b = rgba>>8;
	a = rgba;
	for(chan=blt->i->chan; chan; chan>>=8){
		switch(TYPE(chan)){
		case CIgnore:
			continue;
		case CRed:
			v |= (r>>(8-blt->i->nbits[CRed])) << blt->i->shift[CRed];
			break;
		case CGreen:
			v |= (g>>(8-blt->i->nbits[CGreen])) << blt->i->shift[CGreen];
			break;
		case CBlue:
			v |= (b>>(8-blt->i->nbits[CBlue])) << blt->i->shift[CBlue];
			break;
		case CAlpha:
			v |= (a>>(8-blt->i->nbits[CAlpha])) << blt->i->shift[CAlpha];
			break;
		case CMap:
			m = blt->i->cmap->rgb2cmap[(r>>4)*256+(g>>4)*16+(b>>4)];
			v |= (m>>(8-blt->i->nbits[CMap])) << blt->i->shift[CMap];
			break;
		case CGrey:
			m = RGB2K(r,g,b);
			v |= (m>>(8-blt->i->nbits[CGrey])) << blt->i->shift[CGrey];
			break;
		}
	}

	/* blit op */
	ov = p[0]|p[1]<<8|p[2]<<16|p[3]<<24;

	mask = blt->cmask;
	if(blt->i->depth < 8){
		npack = 8/blt->i->depth;
		off = dp.x%npack;
		sh = blt->i->depth*(npack-1-off);
		mask <<= sh;
		v <<= sh;
	}
	v = (ov ^ v) & mask ^ ov;	/* ≡ (ov &~ mask) | (v & mask) */
	p[0] = v;
	p[1] = v>>8;
	p[2] = v>>16;
	p[3] = v>>24;
}

static void *
initsampfn(ulong chan)
{
	switch(chan){
	case GREY1: return getpixel_k1;
	case GREY2: return getpixel_k2;
	case GREY4: return getpixel_k4;
	case GREY8: return getpixel_k8;
	case CMAP8: return getpixel_m8;
	case RGB15: return getpixel_x1r5g5b5;
	case RGB16: return getpixel_r5g6b5;
	case RGB24: return getpixel_r8g8b8;
	case RGBA32: return getpixel_r8g8b8a8;
	case ARGB32: return getpixel_a8r8g8b8;
	case XRGB32: return getpixel_x8r8g8b8;
	case BGR24: return getpixel_b8g8r8;
	case ABGR32: return getpixel_a8b8g8r8;
	case XBGR32: return getpixel_x8b8g8r8;
	}
	return getpixel;
}

static void *
initblitfn(ulong chan)
{
	switch(chan){
	case GREY1: return putpixel_k1;
	case GREY2: return putpixel_k2;
	case GREY4: return putpixel_k4;
	case GREY8: return putpixel_k8;
	case CMAP8: return putpixel_m8;
	case RGB15: return putpixel_x1r5g5b5;
	case RGB16: return putpixel_r5g6b5;
	case RGB24: return putpixel_r8g8b8;
	case RGBA32: return putpixel_r8g8b8a8;
	case ARGB32: return putpixel_a8r8g8b8;
	case XRGB32: return putpixel_x8r8g8b8;
	case BGR24: return putpixel_b8g8r8;
	case ABGR32: return putpixel_a8b8g8r8;
	case XBGR32: return putpixel_x8b8g8r8;
	}
	return putpixel;
}

static ulong
sample1(Sampler *s, Point p)
{
	if(p.x >= s->r.min.x && p.x < s->r.max.x
	&& p.y >= s->r.min.y && p.y < s->r.max.y)
		return s->getpixel(s, p);
	else if(s->i->flags & Frepl){
		p = drawrepl(s->r, p);
		return s->getpixel(s, p);
	}
	/* edge handler: constant */
	return 0;
}

static ulong
bilinear(Sampler *s, Point p)
{
	ulong c00, c01, c10, c11;
	uchar c0₀, c0₁, c0₂, c0₃, c1₀, c1₁, c1₂, c1₃;

	c00 = sample1(s, p);
	p.x++;
	c01 = sample1(s, p);
	p.x--; p.y++;
	c10 = sample1(s, p);
	p.x++;
	c11 = sample1(s, p);

	c0₀ = c00>>24;
	c0₁ = c00>>16;
	c0₂ = c00>>8;
	c0₃ = c00;
	c1₀ = c10>>24;
	c1₁ = c10>>16;
	c1₂ = c10>>8;
	c1₃ = c10;
	c0₀ = lerp(c0₀, c01>>24 & 0xFF, s->Δx);
	c0₁ = lerp(c0₁, c01>>16 & 0xFF, s->Δx);
	c0₂ = lerp(c0₂, c01>>8  & 0xFF, s->Δx);
	c0₃ = lerp(c0₃, c01     & 0xFF, s->Δx);
	c1₀ = lerp(c1₀, c11>>24 & 0xFF, s->Δx);
	c1₁ = lerp(c1₁, c11>>16 & 0xFF, s->Δx);
	c1₂ = lerp(c1₂, c11>>8  & 0xFF, s->Δx);
	c1₃ = lerp(c1₃, c11     & 0xFF, s->Δx);
	return    (lerp(c0₀, c1₀, s->Δy)) << 24
		| (lerp(c0₁, c1₁, s->Δy)) << 16
		| (lerp(c0₂, c1₂, s->Δy)) << 8
		| (lerp(c0₃, c1₃, s->Δy));
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
	samp.getpixel = initsampfn(s->chan);

	blit.i = d;
	blit.a = d->data->bdata + d->zero;
	blit.bpl = sizeof(ulong)*d->width;
	blit.cmask = (1ULL << d->depth) - 1;
	blit.putpixel = initblitfn(d->chan);

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
		blit.putpixel(&blit, dp, c);

		p2.x += m[0][0];
		p2.y += m[1][0];
	}
		p2.x = p2₀.x += m[0][1];
		p2.y = p2₀.y += m[1][1];
	}
	return 0;
}
