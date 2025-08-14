#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>

/* 25.7 fixed-point number operations */

#define FMASK		((1<<7) - 1)
#define int2fix(n)	((vlong)(n)<<7)
#define fix2int(n)	((n)>>7)
#define fixmul(a,b)	((vlong)(a)*(vlong)(b) >> 7)
#define fixfrac(n)	((n)&FMASK)
#define lerp(a,b,t)	((a) + ((((b) - (a))*(t))>>7))

typedef struct Foint Foint;
typedef struct Sampler Sampler;

struct Foint
{
	long x, y, w;
};

struct Sampler
{
	Memimage *i;
	uchar *a;
	int bpp;
	int bpl;
	int cmask;
};

static Foint
fix_xform(Foint p, Warp m)
{
	return (Foint){
		fixmul(p.x, m[0][0]) + fixmul(p.y, m[0][1]) + fixmul(p.w, m[0][2]),
		fixmul(p.x, m[1][0]) + fixmul(p.y, m[1][1]) + fixmul(p.w, m[1][2]),
		fixmul(p.x, m[2][0]) + fixmul(p.y, m[2][1]) + fixmul(p.w, m[2][2])
	};
}

static ulong
edgehandler(Sampler*, Point*)
{
	return 0;	/* constant */
}

static ulong
sample(Sampler *s, Point p)
{
	ulong c;

	if(p.x >= s->i->r.min.x && p.x < s->i->r.max.x
	&& p.y >= s->i->r.min.y && p.y < s->i->r.max.y){
		c = *(ulong*)(s->a + p.y*s->bpl + p.x*s->bpp);
		c &= s->cmask;
	}else
		c = edgehandler(s, &p);
	return c;
}

int
memaffinewarp(Memimage *d, Rectangle r, Memimage *s, Point sp0, Warp m)
{
	Sampler samp;
	Point sp, dp;
	Foint p2;
	long Δx, Δy;
	ulong c00, c01, c10, c11;
	uchar c0, c1, c, *a;
	int bpp, bploff;

	if(s->depth < 8 || (s->depth & 3) != 0){
		werrstr("unsupported image format");
		return -1;
	}

	if(d->chan != s->chan){
		werrstr("image formats differ");
		return -1;
	}

	if(rectclip(&r, d->r) == 0)
		return 0;

	samp.i = s;
	samp.a = s->data->bdata + s->zero;
	samp.bpp = s->depth >> 3;
	samp.bpl = sizeof(ulong)*s->width;
	samp.cmask = (1ULL << s->depth) - 1;
	a = d->data->bdata + d->zero;
	bpp = d->depth >> 3;
	bploff = sizeof(ulong)*d->width - Dx(d->r)*bpp;

	r = rectsubpt(r, r.min);

	for(dp.y = 0; dp.y < r.max.y; dp.y++, a += bploff)
	for(dp.x = 0; dp.x < r.max.x; dp.x++, a += bpp){
		p2 = fix_xform((Foint){int2fix(dp.x), int2fix(dp.y), 1<<7}, m);

		Δx = fixfrac(p2.x);
		Δy = fixfrac(p2.y);

		sp.x = fix2int(p2.x);
		sp.y = fix2int(p2.y);
		sp.x += sp0.x;
		sp.y += sp0.y;
		c00 = sample(&samp, sp);
		sp.x++;
		c01 = sample(&samp, sp);
		sp.x--; sp.y++;
		c10 = sample(&samp, sp);
		sp.x++;
		c11 = sample(&samp, sp);

		switch(s->nchan){
		case 4:
			c0 = c00 >> 8*3 & 0xFF; c0 = lerp(c0, c01 >> 8*3 & 0xFF, Δx);
			c1 = c10 >> 8*3 & 0xFF; c1 = lerp(c1, c11 >> 8*3 & 0xFF, Δx);
			c  = lerp(c0, c1, Δy);
			*(a + 3) = c;
		case 3:
			c0 = c00 >> 8*2 & 0xFF; c0 = lerp(c0, c01 >> 8*2 & 0xFF, Δx);
			c1 = c10 >> 8*2 & 0xFF; c1 = lerp(c1, c11 >> 8*2 & 0xFF, Δx);
			c  = lerp(c0, c1, Δy);
			*(a + 2) = c;
		case 2:
			c0 = c00 >> 8*1 & 0xFF; c0 = lerp(c0, c01 >> 8*1 & 0xFF, Δx);
			c1 = c10 >> 8*1 & 0xFF; c1 = lerp(c1, c11 >> 8*1 & 0xFF, Δx);
			c  = lerp(c0, c1, Δy);
			*(a + 1) = c;
		case 1:
			c0 = c00 >> 8*0 & 0xFF; c0 = lerp(c0, c01 >> 8*0 & 0xFF, Δx);
			c1 = c10 >> 8*0 & 0xFF; c1 = lerp(c1, c11 >> 8*0 & 0xFF, Δx);
			c  = lerp(c0, c1, Δy);
			*(a + 0) = c;
		}
	}

	return 0;
}
