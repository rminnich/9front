#include <u.h>
#include <libc.h>
#include <avl.h>
#include <fcall.h>
#include <bio.h>

#include "dat.h"
#include "fns.h"
#include "atomic.h"

Gefs *fs;

int	ream;
int	grow;
int	debug;
int	stdio;
int	noauth;
int	nproc;
int	permissive;
int	usereserve;
int	checkonly;
char	*reamuser;
char	*dev;
vlong	tracesz		= 16*MiB;
vlong	cachesz 	= 512*MiB;
char	*srvname 	= "gefs";
int	noneid		= 0;
int	nogroupid	= 9999;
int	admid		= -1;
Blk	*blkbuf;
Bfree	*bfbuf;
Errctx	**errctx;

void
_trace(char *msg, Bptr bp, vlong v0, vlong v1)
{
	Trace *t;
	ulong idx;

	idx = aincl(&fs->traceidx, 1);
	t = &fs->trace[(idx-1) % fs->ntrace];
	strecpy(t->msg, t->msg+sizeof(t->msg), msg);
	t->tid = (*errctx)->tid;
	t->qgen = agetv(&fs->qgen);
	t->bp = bp;
	t->v0 = v0;
	t->v1 = v1;
}

static void
nokill(void)
{
	char buf[128];
	int fd;

	snprint(buf, sizeof(buf), "/proc/%d/ctl", getpid());
	if((fd = open(buf, OWRITE)) == -1){
		fprint(2, "nokill: open %s: %r", buf);
		return;
	}
	if(fprint(fd, "noswap\n") == -1){
		fprint(2, "nokill: write %s: %r", buf);
		return;
	}
}

static uvlong
memsize(void)
{
	char *ln, *f[2];
	vlong mem;
	Biobuf *bp;

	mem = 512*MiB;
	if((bp = Bopen("/dev/swap", OREAD)) == nil)
		return mem;
	while((ln = Brdstr(bp, '\n', 1)) != nil){
		if(tokenize(ln, f, nelem(f)) != 2)
			continue;
		if(strcmp(f[1], "memory") == 0){
			mem = strtoll(f[0], 0, 0);
			free(ln);
			break;
		}
		free(ln);
	}
	Bterm(bp);
	return mem;
}

jmp_buf*
_waserror(void)
{
	Errctx *c;

	c = *errctx;
	c->nerrlab++;
	assert(c->nerrlab > 0 && c->nerrlab < Estacksz);
	return c->errlab + (c->nerrlab-1);
}

_Noreturn static void
errorv(char *fmt, va_list ap, int broke)
{
	Errctx *c;

	c = *errctx;
	vsnprint(c->err, sizeof(c->err), fmt, ap);
	if(broke){
		fprint(2, "%s\n", c->err);
		abort();
	}
	assert(c->nerrlab > 0 && c->nerrlab < Estacksz);
	longjmp(c->errlab[--c->nerrlab], -1);
}

_Noreturn void
broke(char *fmt, ...)
{
	va_list ap;

	aincl(&fs->rdonly, 1);
	va_start(ap, fmt);
	errorv(fmt, ap, 1);
}

_Noreturn void
error(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	errorv(fmt, ap, 0);
}

_Noreturn void
nexterror(void)
{
	Errctx *c;

	c = *errctx;
	assert(c->nerrlab > 0 && c->nerrlab < Estacksz);
	longjmp(c->errlab[--c->nerrlab], -1);
}

void*
emalloc(usize sz, int zero)
{
	void *p;

	if((p = mallocz(sz, zero)) == nil)
		error(Enomem);
	setmalloctag(p, getcallerpc(&sz));
	return p;
}

static void
initfs(vlong cachesz)
{
	Bfree *f, *g;
	Blk *b;

	if((fs = mallocz(sizeof(Gefs), 1)) == nil)
		sysfatal("malloc: %r");

	if(tracesz != 0){
		fs->trace = emalloc(tracesz, 1);
		fs->ntrace = tracesz/sizeof(Trace);
	}
	fs->lrurz.l = &fs->lrulk;
	fs->syncrz.l = &fs->synclk;
	fs->bfreerz.l = &fs->bfreelk;
	fs->noauth = noauth;
	fs->cmax = cachesz/Blksz;
	if(fs->cmax > (1<<30))
		sysfatal("cache too big");
	if((fs->bcache = mallocz(fs->cmax*sizeof(Bucket), 1)) == nil)
		sysfatal("malloc: %r");
	fs->dlcmax = fs->cmax/10;
	if(fs->dlcmax < 4)
		fs->dlcmax = 4;
	if(fs->dlcmax > 512)
		fs->dlcmax = 512;
	if((fs->dlcache = mallocz(fs->dlcmax*sizeof(Dlist*), 1)) == nil)
		sysfatal("malloc: %r");

	bfbuf = sbrk(fs->cmax * sizeof(Bfree));
	if(bfbuf == (void*)-1)
		sysfatal("sbrk: %r");

	g = nil;
	for(f = bfbuf; f != bfbuf+fs->cmax; f++){
		f->bp = Zb;
		f->next = g;
		g = f;
	}
	fs->bfree = g;

	blkbuf = sbrk(fs->cmax * sizeof(Blk));
	if(blkbuf == (void*)-1)
		sysfatal("sbrk: %r");
	for(b = blkbuf; b != blkbuf+fs->cmax; b++){
		b->bp = Zb;
		b->magic = Magic;
		lrutop(b);
	}
}

static void
launch(void (*f)(int, void *), void *arg, char *text)
{
	long pid, id;

	assert(fs->nworker < nelem(fs->lepoch));
	pid = rfork(RFPROC|RFMEM|RFNOWAIT);
	if (pid < 0)
		sysfatal("can't fork: %r");
	if (pid == 0) {
		nokill();
		id = aincl(&fs->nworker, 1);
		if((*errctx = mallocz(sizeof(Errctx), 1)) == nil)
			sysfatal("malloc: %r");
		(*errctx)->tid = id;
		procsetname("%s.%ld", text, id);
		(*f)(id, arg);
		exits("child returned");
	}
}

static int
postfd(char *name, char *suff, int mode)
{
	char buf[80];
	int fd[2];
	int cfd;

	if(pipe(fd) < 0)
		sysfatal("can't make a pipe");
	snprint(buf, sizeof buf, "/srv/%s%s", name, suff);
	if((cfd = create(buf, OWRITE|ORCLOSE|OCEXEC, mode)) == -1)
		sysfatal("create %s: %r", buf);
	if(fprint(cfd, "%d", fd[0]) == -1)
		sysfatal("write %s: %r", buf);
	close(fd[0]);
	return fd[1];
}

static void
runannounce(int, void *arg)
{
	char *ann, adir[40], ldir[40];
	int actl, lctl, fd;
	Conn *c;

	ann = arg;
	if((actl = announce(ann, adir)) < 0)
		sysfatal("announce %s: %r", ann);
	while(1){
		if((lctl = listen(adir, ldir)) < 0){
			fprint(2, "listen %s: %r", adir);
			break;
		}
		fd = accept(lctl, ldir);
		close(lctl);
		if(fd < 0){
			fprint(2, "accept %s: %r", ldir);
			continue;
		}
		if(!(c = newconn(fd, fd))){
			close(fd);
			fprint(2, "%r");
			continue;
		}

		launch(runfs, c, "netio");
	}
	close(actl);
}

static void
usage(void)
{
	fprint(2, "usage: %s [-SA] [-r user] [-m mem] [-n srv] [-a net]... -f dev\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i, srvfd, ctlfd, nann;
	char *s, *e, *ann[16];
	vlong v, memsz;
	Conn *c;

	nann = 0;
	memsz = memsize();
	cachesz = 25*memsz/100;
	ARGBEGIN{
	case 'a':
		if(nann == nelem(ann))
			sysfatal("too many announces");
		ann[nann++] = EARGF(usage());
		break;
	case 'r':
		ream = 1;
		reamuser = EARGF(usage());
		break;
	case 'c':
		checkonly = 1;
		break;
	case 'g':
		grow = 1;
		break;
	case 't':
		tracesz = strtoll(EARGF(usage()), &e, 0);
		tracesz *= MiB;
		break;
	case 'm':
		v = strtoll(EARGF(usage()), &e, 0);
		switch(*e){
		case 'M': case 'm': case 0:
			cachesz = v*MiB;
			break;
		case 'G': case 'g':
			cachesz = v*GiB;
			break;
		case '%':
			cachesz = v*memsz/100;
			break;
		default:
			sysfatal("unknown suffix %s", e);
		}
		break;
	case 'd':
		debug++;
		break;
	case 'n':
		srvname = EARGF(usage());
		break;
	case 's':
		stdio = 1;
		break;
	case 'A':
		noauth = 1;
		break;
	case 'S':
		permissive = 1;
		break;
	case 'f':
		dev = EARGF(usage());
		break;
	default:
		usage();
		break;
	}ARGEND;
	if(dev == nil)
		usage();

	/*
	 * sanity checks -- I've tuned these to stupid
	 * values in the past.
	 */
	assert(4*Kpmax < Pivspc);
	assert(2*Msgmax < Bufspc);
	assert(Treesz < Inlmax);

	initfs(cachesz);
	initshow();
	errctx = privalloc();
	if((*errctx = mallocz(sizeof(Errctx), 1)) == nil)
		sysfatal("malloc: %r");
	tmfmtinstall();
	fmtinstall('H', encodefmt);
	fmtinstall('B', Bconv);
	fmtinstall('M', Mconv);
	fmtinstall('P', Pconv);
	fmtinstall('K', Kconv);
	fmtinstall('R', Rconv);
	fmtinstall('F', fcallfmt);
	fmtinstall('Q', Qconv);

	if((s = getenv("NPROC")) != nil)
		nproc = atoi(s);
	free(s);

	/*
	 * too few procs, we can't parallelize io,
	 * too many, we suffer lock contention
	 */
	if(nproc < 2)
		nproc = 2;
	if(nproc > 8)
		nproc = 8;
	if(ream){
		reamfs(dev);
		exits(nil);
	}
	if(grow){
		growfs(dev);
		exits(nil);
	}
	if(checkonly){
		loadfs(dev);
		if(!checkfs(2))
			sysfatal("broken fs: %r");
		exits(nil);
	}

	rfork(RFNOTEG);
	nokill();
	loadfs(dev);
	fs->wrchan = mkchan(32);
	fs->admchan = mkchan(32);
	/*
	 * for spinning disks, parallel sync tanks performance
	 * for ssds, it doesn't help much.
	 */
	fs->nsyncers = 1;
	fs->nreaders = nproc/2;
	if(fs->nsyncers > fs->narena)
		fs->nsyncers = fs->narena;
	for(i = 0; i < fs->nsyncers; i++)
		qinit(&fs->syncq[i]);
	if((fs->rdchan = malloc(fs->nreaders*sizeof(Chan*))) == nil)
		sysfatal("malloc: %r");
	for(i = 0; i < fs->nreaders; i++)
		fs->rdchan[i] = mkchan(32);
	for(i = 0; i < fs->narena; i++)
		fs->arenas[i].sync = &fs->syncq[i%fs->nsyncers];
	srvfd = postfd(srvname, "", 0666);
	ctlfd = postfd(srvname, ".cmd", 0600);
	launch(runcons, (void*)ctlfd, "ctl");
	launch(runmutate, nil, "mutate");
	launch(runsweep, nil, "sweep");
	launch(runtasks, nil, "tasks");
	for(i = 0; i < fs->nreaders; i++)
		launch(runread, fs->rdchan[i], "readio");
	for(i = 0; i < fs->nsyncers; i++)
		launch(runsync, &fs->syncq[i], "syncio");
	for(i = 0; i < nann; i++)
		launch(runannounce, ann[i], "announce");
	if(srvfd != -1){
		if((c = newconn(srvfd, srvfd)) == nil)
			sysfatal("%r");
		launch(runfs, c, "srvio");
	}
	if(stdio){
		if((c = newconn(0, 1)) == nil)
			sysfatal("%r");
		launch(runfs, c, "stdio");
	}
	exits(nil);
}
