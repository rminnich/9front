#include <u.h>
#include <libc.h>

#include "atomic.h"

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

#define GET(T, n) \
	T n(T *p)			\
	{				\
		uintptr h;		\
		T r;			\
					\
		h = ihash(p);		\
		lock(&locktab[h]);	\
		r = *p;			\
		unlock(&locktab[h]);	\
		return r;		\
	}

#define SET(T, n) \
	T n(T *p, T v)			\
	{				\
		uintptr h;		\
		T r;			\
					\
		h = ihash(p);		\
		lock(&locktab[h]);	\
		r = *p;			\
		*p = v;			\
		unlock(&locktab[h]);	\
		return r;		\
	}

#define INC(T, n) \
	T n(T *p, T dv)			\
	{				\
		uintptr h;		\
		T r;			\
					\
		h = ihash(p);		\
		lock(&locktab[h]);	\
		*p += dv;		\
		r = *p;			\
		unlock(&locktab[h]);	\
		return r;		\
	}

#define CAS(T, n) \
	int n(T *p, T ov, T nv)		\
	{				\
		uintptr h;		\
		int r;			\
					\
		h = ihash(p);		\
		lock(&locktab[h]);	\
		if(*p == ov){		\
			*p = nv;	\
			r = 1;		\
		}else			\
			r = 0;		\
		unlock(&locktab[h]);	\
		return r;		\
	}

GET(int, ageti)
GET(long, agetl)
GET(vlong, agetv)
GET(void*, agetp)

SET(int, aseti)
SET(long, asetl)
SET(vlong, asetv)
SET(void*, asetp)

INC(int, ainci)
INC(long, aincl)
INC(vlong, aincv)

CAS(int, acasi)
CAS(long, acasl)
CAS(vlong, acasv)
CAS(void*, acasp)
