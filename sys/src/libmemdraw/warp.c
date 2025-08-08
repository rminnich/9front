#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>

#define lerp(a,b,t)	((a) + ((((b) - (a))*(t))>>16))

typedef struct Sampler Sampler;
struct Sampler
{
	Memimage *i;
	uchar *a;
	int bpp;
	int bpl;
	int cmask;
};

static ulong
edgehandler(Sampler*, Point*)
{
	return 0;	/* constant */
}

static ulong
sample(Sampler *s, Point p)
{
	ulong c;

	if(ptinrect(p, s->i->r)){
		c = *(ulong*)(s->a + p.y*s->bpl + p.x*s->bpp);
		c &= s->cmask;
	}else
		c = edgehandler(s, &p);
	return c;
}

int
memaffinewarp(Memimage *d, Rectangle r, Memimage *s, Point sp0, uchar *ma)
{
	Sampler samp;
	Matrix m;
	Point sp, dp;
	Point2 p2;
	double Δx, Δy;
	int Δx2, Δy2;
	ulong c00, c01, c10, c11;
	uchar c0, c1, c, *a;

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
	samp.cmask = (1ULL << 8*s->nchan) - 1;

	memmove(m, ma, 2*3*8);
	m[2][0] = 0; m[2][1] = 0; m[2][2] = 1;
	invm(m);

	for(dp.y = r.min.y; dp.y < r.max.y; dp.y++)
	for(dp.x = r.min.x; dp.x < r.max.x; dp.x++){
		a = byteaddr(d, dp);

		p2 = xform((Point2){dp.x - r.min.x, dp.y - r.min.y, 1}, m);
		sp.x = p2.x;
		sp.y = p2.y;

		Δx = p2.x - sp.x;
		Δx2 = Δx * (1<<16);
		Δy = p2.y - sp.y;
		Δy2 = Δy * (1<<16);

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
			c0 = c00 >> 8*3 & 0xFF; c0 = lerp(c0, c01 >> 8*3 & 0xFF, Δx2);
			c1 = c10 >> 8*3 & 0xFF; c1 = lerp(c1, c11 >> 8*3 & 0xFF, Δx2);
			c  = lerp(c0, c1, Δy2);
			*(a + 3) = c;
		case 3:
			c0 = c00 >> 8*2 & 0xFF; c0 = lerp(c0, c01 >> 8*2 & 0xFF, Δx2);
			c1 = c10 >> 8*2 & 0xFF; c1 = lerp(c1, c11 >> 8*2 & 0xFF, Δx2);
			c  = lerp(c0, c1, Δy2);
			*(a + 2) = c;
		case 2:
			c0 = c00 >> 8*1 & 0xFF; c0 = lerp(c0, c01 >> 8*1 & 0xFF, Δx2);
			c1 = c10 >> 8*1 & 0xFF; c1 = lerp(c1, c11 >> 8*1 & 0xFF, Δx2);
			c  = lerp(c0, c1, Δy2);
			*(a + 1) = c;
		case 1:
			c0 = c00 >> 8*0 & 0xFF; c0 = lerp(c0, c01 >> 8*0 & 0xFF, Δx2);
			c1 = c10 >> 8*0 & 0xFF; c1 = lerp(c1, c11 >> 8*0 & 0xFF, Δx2);
			c  = lerp(c0, c1, Δy2);
			*(a + 0) = c;
		}
	}

	return 0;
}
