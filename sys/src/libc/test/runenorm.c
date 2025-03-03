#include <u.h>
#include <libc.h>
#include <bio.h>

//Annoying to get a gauge of how broken if we bail on the first failure
#define print sysfatal

static int
estrtoul(char *s)
{
	char *epr;
	Rune code;

	code = strtoul(s, &epr, 16);
	if(s == epr)
		sysfatal("bad code point hex string");
	return code;
}

#pragma	   varargck    type  "V"   Rune*
static int
vrunefmt(Fmt *f)
{
	Rune *s;
	int n;

	s = va_arg(f->args, Rune*);
	n = fmtprint(f, "%S(", s);
	for(; *s != 0; s++)
		n += fmtprint(f, "%X ", *s);
	n += fmtprint(f, ")");
	return n;
}

typedef struct {
	Rune src[64];
	Rune nfc[70];
	Rune nfd[70];
} Line;

typedef struct {
	Rune *s, *p;
	int n;
} Ctx;

static long
getrune(void *ctx)
{
	Ctx *c;

	c = ctx;
	if(c->p >= c->s + c->n)
		return -1;
	return *c->p++;
}

static void
testline(Line *l)
{
	Norm rd, rc;
	static Ctx ctx;
	Rune out1[70];
	Rune out2[70];
	int i, n;

	norminit(&rd, 0, &ctx, getrune);
	norminit(&rc, 1, &ctx, getrune);
	ctx.s = ctx.p = l->src;
	ctx.n = runestrlen(l->src) + 1;

	n = normpull(&rd, out1, nelem(out1), 1);
	if(out1[n-1] != '\0')
		sysfatal("norm ate null");
	if(runestrcmp(l->nfd, out1) != 0)
		print("(1) %V %V %V\n", l->src, l->nfd, out1);

	ctx.p = ctx.s;
	n = normpull(&rc, out2, nelem(out2), 1);
	if(out2[n-1] != '\0')
		sysfatal("norm ate null");
	if(runestrcmp(l->nfc, out2) != 0)
		print("(2) %V %V %V\n", l->src, l->nfc, out2);

	ctx.p = ctx.s;
	i = 0;
	do {
		n = normpull(&rd, out1 + i, 1, 1);
		i += n;
	} while(n != 0);
	if(runestrcmp(l->nfd, out1) != 0)
		print("rune-by-rune nfd fail %V %V\n", l->nfd, out1);

	ctx.p = ctx.s;
	i = 0;
	do {
		n = normpull(&rc, out2 + i, 1, 1);
		i += n;
	} while(n != 0);
	if(runestrcmp(l->nfc, out2) != 0)
		print("rune-by-rune nfc fail %V %V\n", l->nfc, out2);
}

static void
testutfline(Line *l)
{
	char out1[128], out2[128];
	char buf1[128], buf2[128], buf3[128];

	snprint(buf1, sizeof buf1, "%S", l->src);
	snprint(buf2, sizeof buf2, "%S", l->nfd);
	snprint(buf3, sizeof buf3, "%S", l->nfc);

	utfcomp(out1, sizeof out1, buf1, strlen(buf1)+1);
	utfdecomp(out2, sizeof out2, buf1, strlen(buf1)+1);

	if(strcmp(out1, buf3) != 0)
		print("utfline fail nfc: %s %s %s\n", buf1, buf3, out1);

	if(strcmp(out2, buf2) != 0)
		print("utfline fail nfd: %s %s %s\n", buf1, buf2, out2);
}

static void
testedge(void)
{
	Line l;
	int i;

	/*
	 * Test that we correctly break up long attacher
	 * runs with U+034F
	 */
	l.src[0] = L'U';
	for(i = 1; i < nelem(l.src)-1; i++)
		l.src[i] = 0x308;
	l.src[nelem(l.src)-1] = 0;

	memcpy(l.nfd, l.src, sizeof(l.src));
	l.nfd[31] = 0x34F;
	l.nfd[62] = 0x34F;
	l.nfd[63] = 0x308;
	l.nfd[64] = 0x308;
	l.nfd[65] = 0;

	memcpy(l.nfc, l.src, sizeof l.src);
	l.nfc[0] = 0xDC;
	l.nfc[30] = 0x34F;
	l.nfc[61] = 0x34F;
	l.nfc[62] = 0x308;
	l.nfc[63] = 0x308;
	l.nfc[64] = 0;

	testline(&l);

	for(i = 0; i < nelem(l.src)-1; i++)
		l.src[i] = 0x308;
	l.src[nelem(l.src)-1] = 0;
	memcpy(l.nfd, l.src, sizeof l.src);
	l.nfd[30] = 0x034F;
	l.nfd[61] = 0x034F;
	l.nfd[62] = 0x308;
	l.nfd[63] = 0x308;
	l.nfd[64] = 0x308;
	l.nfd[65] = 0;
	memcpy(l.nfc, l.nfd, sizeof l.nfd);

	testline(&l);

	l.src[0] = L'U';
	for(i = 1; i < 30; i++)
		l.src[i] = 0x300;
	l.src[i++] = 0x0344;
	l.src[i] = 0;
	memcpy(l.nfc, l.src, sizeof l.src);
	memcpy(l.nfd, l.src, sizeof l.src);
	l.nfd[30] = 0x034F;
	l.nfd[31] = 0x308;
	l.nfd[32] = 0x301;
	l.nfd[33] = 0;
	l.nfc[0] = 0xD9;
	l.nfc[29] = 0x034F;
	l.nfc[30] = 0x308;
	l.nfc[31] = 0x301;
	l.nfc[32] = 0;

	testline(&l);

	for(i = 0; i < 59; i++)
		l.src[i] = 0x300;
	l.src[i++] = 0x0344;
	l.src[i] = 0;
	memcpy(l.nfd, l.src, sizeof l.src);
	l.nfd[30] = 0x34F;
	l.nfd[59] = 0x300;
	l.nfd[60] = 0x34F;
	l.nfd[61] = 0x308;
	l.nfd[62] = 0x301;
	l.nfd[63] = 0x0;
	memcpy(l.nfc, l.nfd, sizeof l.nfd);

	testline(&l);

	l.src[0] = 0x16D63;
	for(i = 1; i < 33; i++)
		l.src[i] = 0x16D67;
	l.src[i] = 0;
	memcpy(l.nfd, l.src, sizeof l.src);

	l.nfc[0] = 0x16D6A;
	for(i = 1; i < 1+15; i++)
		l.nfc[i] = 0x16D68;
	l.nfc[i] = 0;

	testline(&l);
}

static void
testeof(void)
{
	Norm n;
	Ctx ctx;
	Rune buf[16], out[16], *p;

	buf[0] = L'u';
	ctx.s = ctx.p = buf;
	ctx.n = 1;

	norminit(&n, 1, &ctx, getrune);
	for(p = out; ctx.p < ctx.s + ctx.n;){
		p += normpull(&n, p, sizeof out - (p - out), 0);
	}
	if(p != out)
		print("norm flushed when we told it not to");
	buf[0] = L'̈';
	buf[1] = L'a';
	ctx.s = ctx.p = buf;
	ctx.n = 2;
	normpull(&n, p, sizeof out - (p - out), 1);
	if(out[0] != L'ü' || out[1] != 'a')
		print("eof test fail: %X\n", out[0]);
}

void
main(int, char)
{
	char *fields[10];
	char *runes[32];
	char *p;
	int n;
	int i;
	Biobuf *b;

	fmtinstall('V', vrunefmt);
	b = Bopen("/lib/ucd/NormalizationTest.txt", OREAD);
	if(b == nil)
		sysfatal("could not load composition exclusions: %r");

	Line test;
	while((p = Brdline(b, '\n')) != nil){
		p[Blinelen(b)-1] = 0;
		if(p[0] == 0 || p[0] == '#' || p[0] == '@')
			continue;
		getfields(p, fields, 6 + 1, 0, ";");
		n = getfields(fields[0], runes, nelem(runes), 0, " ");
		for(i = 0; i < n; i++)
			test.src[i] = estrtoul(runes[i]);
		test.src[i] = 0;

		n = getfields(fields[1], runes, nelem(runes), 0, " ");
		for(i = 0; i < n; i++)
			test.nfc[i] = estrtoul(runes[i]);
		test.nfc[i] = 0;

		n = getfields(fields[2], runes, nelem(runes), 0, " ");
		for(i = 0; i < n; i++)
			test.nfd[i] = estrtoul(runes[i]);
		test.nfd[i] = 0;

		testline(&test);
		testutfline(&test);
	}
	testedge();
	testeof();
	exits(nil);
}
