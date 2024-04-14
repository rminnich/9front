#include <u.h>
#include <libc.h>
#include <ip.h>
#include "dat.h"
#include "protos.h"

typedef struct Hdr	Hdr;
struct Hdr
{	uchar	type;
	uchar	timeout;
	uchar	cksum[2];	/* Checksum */
	uchar	group[4];
};

enum
{
	IGMPLEN=	8,
};

enum
{
	Ot,		/* type */
	Oto,		/* timeout */
	Ock,		/* cksum */
	Og,		/* group */
};

static Field p_fields[] = 
{
	{"t",		Fnum,	Ot,	"type",	},
	{"to",		Fnum,	Oto,	"timeout", },
	{"ck",		Fnum,	Ock,	"cksum", },
	{"g",		Fv4ip,	Og,	"group", },
	{0}
};

enum
{
	Query = 0x11,
	Report = 0x12,
	ReportV2 = 0x16,
	Leave = 0x17,
};

static Mux p_mux[] =
{
	{0},
};

char *igmpmsg[256] =
{
[Query]		"Query",
[Report]	"Report",
[ReportV2]	"ReportV2",
[Leave]		"Leave",
};

static void
p_compile(Filter *f)
{
	if(f->op == '='){
		compile_cmp(igmp.name, f, p_fields);
		return;
	}
	sysfatal("unknown igmp field or protocol: %s", f->s);
}

static int
p_filter(Filter *f, Msg *m)
{
	Hdr *h;

	if(m->pe - m->ps < IGMPLEN)
		return 0;

	h = (Hdr*)m->ps;
	m->ps += IGMPLEN;

	switch(f->subop){
	case Ot:
		if(h->type == f->ulv)
			return 1;
		break;
	}
	return 0;
}

static int
p_seprint(Msg *m)
{
	Hdr *h;
	char *tn;
	char *p = m->p;
	char *e = m->e;
	ushort cksum2, cksum;

	h = (Hdr*)m->ps;
	if(m->pe - m->ps < IGMPLEN)
		return -1;

	m->ps += IGMPLEN;
	m->pr = &dump;

	tn = igmpmsg[h->type];
	if(tn == nil)
		p = seprint(p, e, "t=%ud to=%d ck=%4.4ux g=%V", h->type,
			h->timeout, (ushort)NetS(h->cksum), h->group);
	else
		p = seprint(p, e, "t=%s to=%d ck=%4.4ux g=%V", tn,
			h->timeout, (ushort)NetS(h->cksum), h->group);
	if(Cflag){
		cksum = NetS(h->cksum);
		h->cksum[0] = 0;
		h->cksum[1] = 0;
		cksum2 = ~ptclbsum((uchar*)h, m->pe - m->ps + IGMPLEN) & 0xffff;
		if(cksum != cksum2)
			p = seprint(p,e, " !ck=%4.4ux", cksum2);
	}
	m->p = p;
	return 0;
}

Proto igmp =
{
	"igmp",
	p_compile,
	p_filter,
	p_seprint,
	p_mux,
	"%lud",
	p_fields,
	defaultframer,
};
