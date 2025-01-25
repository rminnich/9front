#include "a.h"

/*
 * Translate troff to Unicode by looking in troff's utfmap.
 * This way we don't have yet another hard-coded table.
 */
typedef struct Trtab Trtab;
struct Trtab
{
	char t[UTFmax];
	Rune r;
};

static Trtab trtab[200];
int ntrtab;

static Trtab trinit[] =
{
	"pl",		Upl,
	"eq",	Ueq,
	"em",	0x2014,
	"en",	0x2013,
	"mi",	Umi,
	"fm",	0x2032,
};

Rune
troff2rune(Rune *rs)
{
	char *file, *f[10], *p, s[3];
	int i, nf;
	Biobuf *b;
	
	if(rs[0] >= Runeself || rs[1] >= Runeself)
		return Runeerror;
	s[0] = rs[0];
	s[1] = rs[1];
	s[2] = 0;
	if(ntrtab == 0){
		for(i=0; i<nelem(trinit) && ntrtab < nelem(trtab); i++){
			trtab[ntrtab] = trinit[i];
			ntrtab++;
		}
		file = "/sys/lib/troff/font/devutf/utfmap";
		if((b = Bopen(file, OREAD)) == nil)
			sysfatal("open %s: %r", file);
		while((p = Brdline(b, '\n')) != nil){
			p[Blinelen(b)-1] = 0;
			nf = getfields(p, f, nelem(f), 0, "\t");
			for(i=0; i+2<=nf && ntrtab<nelem(trtab); i+=2){
				chartorune(&trtab[ntrtab].r, f[i]);
				memmove(trtab[ntrtab].t, f[i+1], 2);
				ntrtab++;
			}
		}
		Bterm(b);
		
		if(ntrtab >= nelem(trtab))
			fprint(2, "%s: trtab too small\n", argv0);
	}
	
	for(i=0; i<ntrtab; i++)
		if(strcmp(s, trtab[i].t) == 0)
			return trtab[i].r;
	return Runeerror;
}

