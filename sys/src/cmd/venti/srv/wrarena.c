#include "stdinc.h"
#include "dat.h"
#include "fns.h"
#include "whack.h"

#define NWORKERS 0
#define NSENDERS 4
#define	U32GET(p)	((u32int)(((p)[0]<<24)|((p)[1]<<16)|((p)[2]<<8)|(p)[3]))
#define	U64GET(p)	(((u64int)U32GET(p)<<32)|(u64int)U32GET((p)+4))

char *host;
int readonly = 1;	/* for part.c */
int mainstacksize = 256*1024;
Channel *c;
VtConn *z;
Arena *arena;
uchar *data;
Channel *fin;
u64int arenaend;
int haveaoffset;
int maxwrites = -1;
int verbose;

typedef struct ZClump ZClump;
struct ZClump
{
	uchar *data;
	Clump cl;
	u64int aa;
};

static int
fastreadclumpinfo(u64int aa, Clump *cl)
{
	u32int n = ClumpSize;
	if(aa + n > arenaend){
		werrstr("clump extends past arena");
		return 0;
	}
	if(unpackclump(cl, &data[aa], arena->clumpmagic) < 0)
		return 0;
	return 1;
}

static uchar*
readcompressedclump(u64int aa, Clump *cl)
{
	Unwhack uw;
	int nunc;
	uchar *buf;
	buf = malloc(cl->info.uncsize);
	unwhackinit(&uw);
	nunc = unwhack(&uw, buf, cl->info.uncsize, &data[aa + ClumpSize], cl->info.size);
	if(nunc != cl->info.uncsize){
		if(nunc < 0)
			sysfatal("decompression of %llud failed: %s", aa, uw.err);
		else
			sysfatal("decompression of %llud gave partial block: %d/%d\n", aa, nunc, cl->info.uncsize);
	}
	return buf;
}

static void
verifyclump(u64int aa, uchar *buf, Clump *cl)
{
	u8int bh[VtScoreSize];
	scoremem(bh, buf, cl->info.uncsize);
	if(scorecmp(cl->info.score, bh) != 0)
		sysfatal("clump corrupt at %s %llud; expected=%V got=%V", arena->name, aa, cl->info.score, bh);
	if(vttypevalid(cl->info.type) < 0)
		sysfatal("loading lump at %s %llud: invalid lump type %d", arena->name, aa, cl->info.type);
}

static void
ensurenotcorrupt(u64int aa, Clump *cl)
{
	if(cl->info.type == VtCorruptType)
		sysfatal("clump %lld corrupt", aa);
}

static uchar*
fastloadclump(u64int aa, Clump *cl, u8int *score)
{
	uchar *buf = nil;
	fastreadclumpinfo(aa, cl);
	ensurenotcorrupt(aa, cl);
	scorecp(score, cl->info.score);
	if(cl->encoding == ClumpECompress)
		buf = readcompressedclump(aa, cl);
	else if (cl->encoding == ClumpENone)
		buf = &data[aa + ClumpSize];
	else
		sysfatal("Unrecognized encoding %d for clump %llx", cl->encoding, aa);
	verifyclump(aa, buf, cl);
	return buf;
}

void
usage(void)
{
	fprint(2, "usage: wrarena [-h host] arenafile [offset]\n");
	threadexitsall("usage");
}

void
vtsendthread(void *v)
{
	ZClump zcl;

	USED(v);
	while(recv(c, &zcl) == 1){
		if(vtwrite(z, zcl.cl.info.score, zcl.cl.info.type, zcl.data, zcl.cl.info.uncsize) < 0)
			sysfatal("failed writing clump %llud: %r", zcl.aa);
		if(zcl.cl.encoding == ClumpECompress)
			free(zcl.data);
		if(verbose)
			fprint(2, "%V\n", zcl.cl.info.score);
	}
	send(fin, nil);
	threadexits(nil);
}

static int
directreadci(Arena *arena, int clump, ClumpInfo *ci)
{
	u32int block, off;
	if(clump >= arena->memstats.clumps){
		seterr(EOk, "clump out of range");
		return -1;
	}
	block = clump / arena->clumpmax;
	off = (clump - (block * arena->clumpmax)) * ClumpInfoSize;
	unpackclumpinfo(ci, &data[arena->size - (block+1)*arena->blocksize + off]);
	return 0;
}

static void
rdarenanew(u64int offset)
{
	int i;
	u64int a, aa, e;
	ClumpInfo ci;
	ZClump zcl;
	uchar score[VtScoreSize];

	fprint(2, "wrarena: copying %s to venti\n", arena->name);
	printarena(2, arena);

	a = arena->base;
	e = arena->base + arena->size;
	if(offset != ~(u64int)0) {
		if(offset >= e - a)
			sysfatal("bad offset %#llx >= %#llx", offset, e - a);
		aa = offset;
	} else
		aa = 0;

	i = 0;
	for(a = 0; i < arena->memstats.clumps;
	    a += ClumpSize + ci.size){
		if(directreadci(arena, i++, &ci) < 0)
			break;
		if(a < aa)
			continue;
		zcl.data = fastloadclump(a, &zcl.cl, score);
		if(z && zcl.data)
			send(c, &zcl);
		if(ci.type == VtCorruptType){
			fprint(2, "%s: corrupt clump read at %#llx: +%d\n",
					argv0, a, ClumpSize+ci.size);
			continue;
		}
		if(maxwrites > 0)
			--maxwrites;
	}
	if(a > aa)
		aa = a;
	if(haveaoffset)
		print("%#llx", aa);
}

void
threadmain(int argc, char *argv[])
{
	int i;
	char *file;
	u64int offset, aoffset;
	Part *part;
	uchar buf[2][512];
	ArenaHead head;

	aoffset = 0;
	ARGBEGIN{
	case 'f':
		ventidoublechecksha1 = 0;
		break;
	case 'h':
		host = EARGF(usage());
		break;
	case 'o':
		haveaoffset = 1;
		aoffset = strtoull(EARGF(usage()), 0, 0);
		break;
	case 'M':
		maxwrites = atoi(EARGF(usage()));
		break;
	case 'v':
		verbose = 1;
		break;
	default:
		usage();
		break;
	}ARGEND

	offset = ~(u64int)0;
	switch(argc) {
	default:
		usage();
	case 2:
		offset = strtoull(argv[1], 0, 0);
		/* fall through */
	case 1:
		file = argv[0];
	}

	part = initpart(file, OREAD);
	if(part == nil)
		sysfatal("can't open file %s: %r", file);
	if(readpart(part, aoffset, buf[0], 512) < 0)
		sysfatal("can't read file %s: %r", file);

	head.blocksize = U32GET(buf[0] + 2 * U32Size + ANameSize);
	head.size = U64GET(buf[0] + 3 * U32Size + ANameSize);
	namecp(head.name, (char*)buf[0] + 2 * U32Size);
	fprint(2, "processing %s...\n", head.name);

	if(aoffset+head.size > part->size)
		sysfatal("arena is truncated: want %llud bytes have %llud", head.size, part->size);
	
	if(readpart(part, aoffset - head.blocksize + head.size, buf[1], 512) >= 0){
		u32int version = U32GET(buf[1] + U32Size);
		u8int *addr = buf[1] + 6 * U32Size + ANameSize;
		if(version == 5)
			addr += U32Size;
		if(offset == U64GET(addr))
			threadexitsall(0);
	}

	ventifmtinstall();
	fmtinstall('V', vtscorefmt);

	if(unpackarenahead(&head, buf[0]) < 0)
		sysfatal("corrupted arena header: %r");

	arena = initarena(part, aoffset, head.size, head.blocksize);
	if(arena == nil)
		sysfatal("initarena: %r");
	arenaend = arena->size - arenadirsize(arena, arena->memstats.clumps);

	data = malloc(arena->size);
	if(data == nil)
		sysfatal("malloc failed");
	uvlong before = nsec();
	if(readpart(part, arena->base, data, arena->size) < 0){
		sysfatal("failed to cache arena in memory");
	}
	uvlong after = nsec() - before;
	fprint(2, "cached arena in %fs\n", ((double)after)/1000000000);

	z = nil;
	if(host==nil || strcmp(host, "/dev/null") != 0){
		z = vtdial(host);
		if(z == nil)
			sysfatal("could not connect to server: %r");
		if(vtconnect(z) < 0)
			sysfatal("vtconnect: %r");
	}
	
	c = chancreate(sizeof(ZClump), NSENDERS * 100);
	fin = chancreate(1, NWORKERS + NSENDERS);
	for(i=0; i<NSENDERS; i++)
		vtproc(vtsendthread, nil);

	rdarenanew(offset);
	for(i = 0; i < NWORKERS; i += 1)
		recv(fin, nil);
	chanclose(c);
	for(i = 0; i < NSENDERS; i += 1)
		recv(fin, nil);
	fprint(2, "syncing... ");
	if(vtsync(z) < 0)
		sysfatal("executing sync: %r");
	fprint(2, " hanging up... ");
	if(z)
		vthangup(z);
	
	fprint(2, "done!\n");

	threadexitsall(0);
}
