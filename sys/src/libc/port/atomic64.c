#include <u.h>
#include <libc.h>

static Lock locktab[128];

static u32int
ihash(void *p)
{
	uintptr x = (uintptr)p;

	/* constants from splitmix32 rng */
	x = (x ^ (x >> 16)) * 0x85ebca6b;
	x = (x ^ (x >> 13)) * 0xc2b2ae35;
	x = (x ^ (x >> 16));
	return x & (nelem(locktab)-1);
}

#define GET(A, T, n) \
	T n(A *p)			\
	{				\
		uintptr h;		\
		T r;			\
					\
		h = ihash(p);		\
		lock(&locktab[h]);	\
		r = p->v;		\
		unlock(&locktab[h]);	\
		return r;		\
	}

#define SET(A, T, n) \
	T n(A *p, T v)			\
	{				\
		uintptr h;		\
		T r;			\
					\
		h = ihash(p);		\
		lock(&locktab[h]);	\
		r = p->v;		\
		p->v = v;		\
		unlock(&locktab[h]);	\
		return r;		\
	}

#define INC(A, T, n) \
	T n(A *p, T dv)			\
	{				\
		uintptr h;		\
		T r;			\
					\
		h = ihash(p);		\
		lock(&locktab[h]);	\
		p->v += dv;		\
		r = p->v;		\
		unlock(&locktab[h]);	\
		return r;		\
	}

#define CAS(A, T, n) \
	int n(A *p, T ov, T nv)		\
	{				\
		uintptr h;		\
		int r;			\
					\
		h = ihash(p);		\
		lock(&locktab[h]);	\
		if(p->v == ov){		\
			p->v = nv;	\
			r = 1;		\
		}else			\
			r = 0;		\
		unlock(&locktab[h]);	\
		return r;		\
	}

GET(Avlong, vlong, agetv)

SET(Avlong, vlong, aswapv)

INC(Avlong, vlong, aincv)

CAS(Avlong, vlong, acasv)
