#include <u.h>
#include <libc.h>

#include "runenormdata"

//Unicode Standard: Section 3.12 Conjoining Jamo Behavior
enum {
	SBase = 0xAC00,
	LBase = 0x1100,
	VBase = 0x1161,
	TBase = 0x11A7,

	LCount = 19,
	VCount = 21,
	TCount = 28,
	NCount = VCount * TCount,
	SCount = LCount * NCount,

	LLast = LBase + LCount - 1,
	SLast = SBase + SCount - 1,
	VLast = VBase + VCount - 1,
	TLast = TBase + TCount - 1,
};

/*
 * Most runes decompose in to one/two
 * other runes with codepoints < 0xFFFF,
 * however there are some exceptions.
 * To keep the table size down we instead
 * store an index in to an exception range
 * within the private use section and use
 * an exception table.
 */
enum {
	Estart = 0xEEEE,
	Estop = 0xF8FF,
};

static Rune
_runedecomp(Rune c, Rune *r2)
{
	uint x;

	if(c < Runeself){
		*r2 = 0;
		return 0;
	}

	//korean
	if(c >= SBase && c <= SLast){
		c -= SBase;
		x = c % TCount;
		if(x){
			*r2 = TBase + x;
			return SBase + (c - x);
		}
		*r2 = VBase + ((c % NCount) / TCount);
		return LBase + (c / NCount);
	}

	x = decomplkup(c);
	if((x & 0xFFFF) != 0){
		*r2 = x & 0xFFFF;
		return x>>16;
	}
	x >>= 16;
	if(x >= Estart && x < Estop){
		Rune *r;
		r = _decompexceptions[x - Estart];
		*r2 = r[1];
		return r[0];
	}
	*r2 = 0;
	return x;
}

static Rune
_runerecomp(Rune r0, Rune r1)
{
	uint x, y, *p, next;

	if(r0 >= LBase && r0 <= LLast){
		if(r1 < VBase || r1 > VLast)
			return 0;
		x = (r0 - LBase) * NCount + (r1 - VBase) * TCount;
		return SBase + x;
	}
	if(r0 >= SBase && r0 <= SLast && (r0 - SBase) % TCount == 0){
		if(r1 > TBase && r1 <= TLast)
			return r0 + (r1 - TBase);
		return 0;
	}
	if(r0 > 0xFFFF || r1 > 0xFFFF){
		for(x = 0; x < nelem(_recompexceptions); x++)
			if(r0 == _recompexceptions[x][1] && r1 == _recompexceptions[x][2])
				return  _recompexceptions[x][0];
		return 0;
	}
	y = x = r0<<16 | r1;
	x ^= x >> 16;
	x *= 0x21f0aaad;
	x ^= x >> 15;
	x *= 0xd35a2d97;
	x ^= x >> 15;
	p = _recompdata + (x%512)*2;
	while(p[0] != y){
		next = p[1]>>16;
		if(!next)
			return 0;
		p = _recompcoll + (next-1)*2;
	}
	return p[1] & 0xFFFF;
}

static void
runecccsort(Rune *a, int len)
{
	Rune r;
	int i, j;

	for(i = 1; i < len; i++){
		r = a[i];
		for(j = i; j > 0 && ccclkup(a[j-1]) > ccclkup(r); j--)
			a[j] = a[j-1];
		a[j] = r;
	}
}

static int
boundary(Rune r)
{
	return !(qclkup(r) & (Qnfcno|Qnfcmay));
}

/*
 * Stk stores the entire context for a chunk of
 * an input string that is being normalized.
 * In accordance to the standard, Unicode text
 * has no upper bound for the amount of conjoining
 * (also called non-starter) elements associated with
 * a base rune. Thus to implement normalization within
 * reasonable memory constraints we implement the
 * "Stream-Safe Text Format" as defined in UAX #15 § 13.
 */
typedef struct {
	Rune a[Maxnormctx];
	Rune *e;
} Stk;

static int
push(Stk *s, Rune c)
{
	int n, l;
	Rune r2, b[Maxdecomp];
	Rune *p = b + nelem(b) - 1;

	for(*p = c; c = _runedecomp(c, &r2); *p = c){
		assert(p > b);
		if(r2 != 0)
			*p-- = r2;
	}

	n = b + nelem(b) - p;
	l = nelem(s->a) - (s->e - s->a);
	if(n > l){
		werrstr("runenorm: buffer overflow");
		return -1;
	}
	l -= n;
	for(; n > 0; n--)
		*s->e++ = *p++;
	return l;
}

/*
 * Worst case recomposition, this happens when we have to compose
 * two runes who both have a CCC of zero.
 */
static void
worstrecomp(Stk *s)
{
	int done;
	Rune c, *p, *rp;

	for(done = 0; done == 0;){
		done = 1;
		for(p = s->a; p+1 < s->e; p++){
			c = _runerecomp(p[0], p[1]);
			if(c == 0)
				continue;
			done = 0;
			*p = c;
			for(rp = p+1; rp < s->e-1; rp++)
				rp[0] = rp[1];
			s->e--;
			p--;
		}
	}
}

static void
cccrecomp(Stk *s)
{
	Rune c, *p, *rp;

	for(p = s->a + 1; p < s->e; p++){
		c  = _runerecomp(s->a[0], *p);
		if(c != 0){
			s->a[0] = c;
			for(rp = p; rp < s->e-1; rp++){
				rp[0] = rp[1];
			}
			s->e--;
			p--;
		} else while(p + 1 < s->e && ccclkup(p[0]) == ccclkup(p[1]))
			p++;
	}
}

void
norminit(Norm *n, int compose, void *ctx, long (*getrune)(void*))
{
	memset(n, 0, sizeof *n);
	n->ctx = ctx;
	n->getrune = getrune;
	n->compose = compose;
	n->obuf.e = n->obuf.a;
	n->ibuf.e = n->ibuf.a;
}

int NORMDEBUG;

static long
peekrune(Norm *n)
{
	long r;

	if(n->ibuf.e > n->ibuf.a)
		return n->ibuf.e[-1];

	r = n->getrune(n->ctx);
	if(r >= 0)
		*n->ibuf.e++ = r;
	return r;
}

static long
getrune(Norm *n)
{
	if(n->ibuf.e > n->ibuf.a)
		return *--n->ibuf.e;
	return n->getrune(n->ctx);
}

long
normpull(Norm *n, Rune *rdst, long max, int flush)
{
	Rune *rp, *re;
	Stk stk;
	Rune *dot;
	int r;
	long c;

	rp = rdst;
	re = rdst + max;
	dot = nil;
	c = 0;
	while(rp < re){
		if(n->obuf.e != n->obuf.a){
			memcpy(stk.a, n->obuf.a, (n->obuf.e - n->obuf.a)*sizeof(Rune));
			stk.e = stk.a + (n->obuf.e - n->obuf.a);
			n->obuf.e = n->obuf.a;
			c = stk.a[0];
			goto Flush;
		}

		stk.e = stk.a;
		c = getrune(n);
		if(c < 0)
			break;
		push(&stk, c);
		c = peekrune(n);
		if(stk.e == stk.a+1 && stk.a[0] < Runeself && c < Runeself && c >= 0)
			goto Flush;
		while(c >= 0 && ccclkup(c) != 0){
			r = push(&stk, getrune(n));
			c = peekrune(n);
			if(r > 2)
				continue;
			if(ccclkup(stk.a[0]) != 0){
				assert(r > 0);
				r--;
			} else
				assert(r >= 0);
			if(r == 0 || (c == 0x0344 && r < 2)){
				/* in reverse */
				if(r > 0){
					getrune(n);
					*n->ibuf.e++ = 0x301;
					*n->ibuf.e++ = 0x308;
				}
				*n->ibuf.e++ = 0x034F;
				break;
			}
		}
		if(stk.e - stk.a > 1)
			runecccsort(stk.a, stk.e - stk.a);

		if(!n->compose)
			goto Flush;

		if(ccclkup(stk.e[-1]) == 0){
			Rune tmp;
			while(c >= 0 && (!boundary(c) || !boundary(_runedecomp(c, &tmp)))){
				if(push(&stk, getrune(n)) == -1){
					*n->ibuf.e++ = c;
					for(r = 0; r < Maxdecomp; r++)
						*n->ibuf.e++ = *--stk.e;
					break;
				}
				c = peekrune(n);
			}
			worstrecomp(&stk);
		} else if(ccclkup(stk.a[0]) == 0)
			cccrecomp(&stk);

Flush:
		if(flush || c >= 0)
			for(dot = stk.a; dot < stk.e; dot++){
				if(rp == re)
					goto Out;
				*rp++ = *dot;
			}
		dot = nil;
		if(c < 0)
			break;
	}
Out:
	if(c < 0 && !flush){
		while(stk.e > stk.a)
			*n->ibuf.e++ = *--stk.e;
	}
	if(dot != nil){
		memcpy(n->obuf.a, dot, (stk.e - dot) * sizeof(Rune));
		n->obuf.e = n->obuf.a + (stk.e - dot);
	}

	return rp - rdst;
}

typedef struct {
	Rune *s, *p;
	int n;
} Rctx;

static long
runegetrune(void *ctx)
{
	Rctx *c;

	c = ctx;
	if(c->p >= c->s + c->n)
		return -1;
	return *c->p++;
}

static long
runedostr(Rune *dst, long ndst, Rune *src, long nsrc, int comp)
{
	Rctx c;
	Norm n;

	c.s = c.p = src;
	c.n = nsrc;
	norminit(&n, comp, &c, runegetrune);
	return normpull(&n, dst, ndst, 1);
}

long
runecomp(Rune *dst, long ndst, Rune *src, long nsrc)
{
	return runedostr(dst, ndst, src, nsrc, 1);
}

long
runedecomp(Rune *dst, long ndst, Rune *src, long nsrc)
{
	return runedostr(dst, ndst, src, nsrc, 0);
}

typedef struct {
	char *s, *p;
	int n;
} Uctx;

static long
utfgetrune(void *ctx)
{
	Uctx *c;
	Rune r;

	c = ctx;
	if(c->p >= c->s + c->n)
		return -1;
	c->p += chartorune(&r, c->p);
	return r;
}

static long
utfdostr(char *dst, long ndst, char *src, long nsrc, int comp)
{
	Uctx c;
	Norm n;
	Rune buf[Maxnormctx];
	long i, w;
	char *e, *p;

	c.s = c.p = src;
	c.n = nsrc;
	norminit(&n, comp, &c, utfgetrune);
	for(p = dst, e = dst + ndst; p < e;){
		w = normpull(&n, buf, nelem(buf), 1);
		if(w == 0)
			break;
		for(i = 0; i < w; i++){
			if(p + runelen(buf[i]) >= e)
				break;
			p += runetochar(p, buf+i);
		}
	}
	return p - dst;
}

long
utfcomp(char *dst, long ndst, char *src, long nsrc)
{
	return utfdostr(dst, ndst, src, nsrc, 1);
}

long
utfdecomp(char *dst, long ndst, char *src, long nsrc)
{
	return utfdostr(dst, ndst, src, nsrc, 0);
}
