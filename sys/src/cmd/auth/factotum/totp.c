#include "dat.h"

typedef struct State State;
struct State {
	Key *key;
};

enum {
	HaveTotp,
	Maxphase,
};

enum {
	Maxdigits = 8,
	Sec = 1000*1000*1000,
};

static char *phasenames[Maxphase] ={
	[HaveTotp]	"HaveTotp",
};

static int
genhotp(uchar *key, int n, uvlong c, int len)
{
	uchar hash[SHA1dlen];
	uchar data[8];
	u32int h, m;
	int o;

	data[0] = (c>>56) & 0xff;
	data[1] = (c>>48) & 0xff;
	data[2] = (c>>40) & 0xff;
	data[3] = (c>>32) & 0xff;
	data[4] = (c>>24) & 0xff;
	data[5] = (c>>16) & 0xff;
	data[6] = (c>> 8) & 0xff;
	data[7] = (c>> 0) & 0xff;
	hmac_sha1(data, sizeof(data), key, n, hash, nil);
	
	o = hash[SHA1dlen - 1] & 0x0F;
	h = ((hash[o] & 0x7F) << 24)
		| (hash[o + 1] & 0xFF) << 16
		| (hash[o + 2] & 0xFF) << 8
		| hash[o + 3] & 0xFF;
	m = 1;
	while(len-- > 0)
		m *= 10;
	return h % m;
}

static int
gentotp(char *secret, vlong t, int len, vlong period)
{
	uchar key[512];
	int n;

	n = dec32(key, sizeof(key), secret, strlen(secret));
	if(n < 0){
		werrstr("invalid totp secret");
		return -1;
	}
	return genhotp(key, n, t/period, len);
}

static int
totpinit(Proto *p, Fsstate *fss)
{
	int ret;
	Key *k;
	Keyinfo ki;
	State *s;

	ret = findkey(&k, mkkeyinfo(&ki, fss, nil), "%s", p->keyprompt);
	if(ret != RpcOk)
		return ret;
	setattrs(fss->attr, k->attr);
	s = emalloc(sizeof(*s));
	s->key = k;
	fss->ps = s;
	fss->phase = HaveTotp;
	return RpcOk;
}

static void
totpclose(Fsstate *fss)
{
	State *s;

	s = fss->ps;
	if(s->key)
		closekey(s->key);
	free(s);
}

static int
totpread(Fsstate *fss, void *va, uint *n)
{
	char *secret, *digits, *period;
	int len, otp;
	vlong tdiv;
	State *s;

	s = fss->ps;
	len = 6;
	tdiv = 30ULL*Sec;
	switch(fss->phase){
	default:
		return phaseerror(fss, "read");

	case HaveTotp:
		digits = _strfindattr(s->key->attr, "digits");
		secret = _strfindattr(s->key->privattr, "!secret");
		period = _strfindattr(s->key->attr, "period");
		if(secret==nil)
			return failure(fss, "missing totp secret");
		if(digits != nil)
			len = atoi(digits);
		if(period != nil)
			tdiv = strtoll(period, nil, 0)*Sec;
		if(*n < len)
			return toosmall(fss, len);
		if(len < 1 || len > Maxdigits || tdiv <= 0)
			return failure(fss, "too many digits");
		otp = gentotp(secret, nsec(), len, tdiv);
		if(otp < 0)
			return failure(fss, "%r");
		*n = snprint(va, *n, "%.*d", len, otp);
		return RpcOk;
	}
}

static int
totpwrite(Fsstate *fss, void*, uint)
{
	return phaseerror(fss, "write");
}

Proto totp = {
	.name=		"totp",
	.init=		totpinit,
	.write=		totpwrite,
	.read=		totpread,
	.close=		totpclose,
	.addkey=	replacekey,
	.keyprompt=	"label? !secret?",
};
