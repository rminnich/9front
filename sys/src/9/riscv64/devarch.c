/*
 * #P/cputype, shutdown and timing.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "riscv64.h"

#include <ctype.h>

enum {
	Qdir = 0,
	Qbase,

	Qmax = 16,
};

typedef long Rdwrfn(Chan*, void*, long, vlong);

static Rdwrfn *readfn[Qmax];
static Rdwrfn *writefn[Qmax];

static Dirtab archdir[Qmax] = {
	".",		{ Qdir, 0, QTDIR },	0,	0555,
};
Lock archwlock;	/* the lock is only for changing archdir */
int narchdir = Qbase;

extern Dev archdevtab;

/*
 * Add a file to the #P listing.  Once added, you can't delete it.
 * You can't add a file with the same name as one already there,
 * and you get a pointer to the Dirtab entry so you can do things
 * like change the Qid version.  Changing the Qid path is disallowed.
 */
Dirtab*
addarchfile(char *name, int perm, Rdwrfn *rdfn, Rdwrfn *wrfn)
{
	int i;
	Dirtab d;
	Dirtab *dp;

	memset(&d, 0, sizeof d);
	strcpy(d.name, name);
	d.perm = perm;

	lock(&archwlock);
	if(narchdir >= Qmax){
		unlock(&archwlock);
		return nil;
	}

	for(i=0; i<narchdir; i++)
		if(strcmp(archdir[i].name, name) == 0){
			unlock(&archwlock);
			return nil;
		}

	d.qid.path = narchdir;
	archdir[narchdir] = d;
	readfn[narchdir] = rdfn;
	writefn[narchdir] = wrfn;
	dp = &archdir[narchdir++];
	unlock(&archwlock);
	return dp;
}

static Chan*
archattach(char* spec)
{
	return devattach(archdevtab.dc, spec);
}

Walkqid*
archwalk(Chan* c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, archdir, narchdir, devgen);
}

static int
archstat(Chan* c, uchar* dp, int n)
{
	return devstat(c, dp, n, archdir, narchdir, devgen);
}

static Chan*
archopen(Chan* c, int omode)
{
	return devopen(c, omode, archdir, narchdir, devgen);
}

static void
archclose(Chan*)
{
}

enum
{
	Linelen= 31,
};

static long
archread(Chan *c, void *a, long n, vlong offset)
{
	Rdwrfn *fn;

	switch((ulong)c->qid.path){

	case Qdir:
		return devdirread(c, a, n, archdir, narchdir, devgen);
	default:
		if(c->qid.path < narchdir && (fn = readfn[c->qid.path]))
			return fn(c, a, n, offset);
		error(Eperm);
		panic("archread notreached");
	}
}

static long
archwrite(Chan *c, void *a, long n, vlong offset)
{
	Rdwrfn *fn;

	switch((ulong)c->qid.path){
	default:
		if(c->qid.path < narchdir && (fn = writefn[c->qid.path]))
			return fn(c, a, n, offset);
		error(Eperm);
	}
}

Dev archdevtab = {
	'P',
	"arch",

	devreset,
	devinit,
	devshutdown,
	archattach,
	archwalk,
	archstat,
	archopen,
	devcreate,
	archclose,
	archread,
	devbread,
	archwrite,
	devbwrite,
	devremove,
	devwstat,
};

void
nop(void)				/* unused */
{
}

vlong
vlabs(vlong vl)				/* unused */
{
	return vl >= 0? vl: -vl;
}

static char *cputype = "riscv64";

static long
cputyperead(Chan*, void *a, long n, vlong off)
{
	char str[100];
	extern int (*archclz)(Clzuint);

	snprint(str, sizeof str, "%s %ud\next %s\n"
		"idle %s %ssending IPIs over %lld ns\n",
		cputype, m->cpumhz, (archclz == clzzbb? "clz": ""),
		"wfi", soc.idlewake? "": "not ", sys->nsthresh);
	return readstr(off, a, n, str);
}

static ulong exts[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 0x10,
	HSM,
	's'<<16|'P'<<8|'I',		/* sPI */
	'R'<<24|'F'<<16|'N'<<8|'C',	/* RFNC */
	SRST,
};

static void
sbiext(ulong ext)
{
	char c;

	if (sbiprobeext(ext) == 0)
		return;
	if (ext < 256)
		print(" %#lux", ext);
	else {
		print(" ");
		for (; ext > 0; ext <<= 8) {
			c = ext >> 24;
			if (c && isprint(c))
				print("%c", c);
		}
	}
}

static void
sbiinit(void)
{
	int i;

	if (!bootmachmode && !nosbi) {
		soc.havesbihsm  = sbiprobeext(HSM) != 0;
		soc.havesbisrst = sbiprobeext(SRST) != 0;
		if (!soc.newmach)
			return;
		print("sbi extensions present:");
		for (i = 0; i < nelem(exts); i++)
			sbiext(exts[i]);
		print("\n");
	}
}

void
archinit(void)
{
	addarchfile("cputype", 0444, cputyperead, nil);
	sbiinit();
}

/*
 * attempt to reset the whole system.
 * failing that, just hang.
 */
void
archreset(void)
{
	iprint("archreset...");
	cacheflush();
	spllo();
	delay(20);
	if (m->machno == 0 && prstackmax)
		prstackmax();
	delay(20);
iprint("arch-specific reset...");
	if (rvreset)
		rvreset();
	if (!nosbi && m->machno == 0) {
		if (soc.havesbisrst) {
			iprint("sbi cold reboot...");
			delay(20);
			sbireset(SRSTCOLD, 0);
			sbireset(SRSTWARM, 0);
			/* shouldn't get here */
		}
		iprint("trying sbi shutdown.\n");
		delay(20);
		sbishutdown();			/* shuts down all cpus */
	}
	iprint("last resort for reset: into wfi.\n");
	delay(20);
	for (;;)
		halt();
}

static void
haltsys(void)
{
	int i;

	panic("iprint(\"mpshutdown: cpu%d: %d active cpus\n\", m->machno, sys->nonline);");
	delay(5);
	panic("implement devtabshutdown();");
	splhi();

	/* paranoia: wait for other cpus */
#ifdef xxx
	for (i = 100; i > 0 && sys->nonline > 1; i--) {
		iprint(" %d", sys->nonline);
		delay(100);
	}
#endif
	/* we can now use the uniprocessor reset */
	archreset();
	panic("	notreached();");
}

/*
 * secondary cpus should all idle (here or in shutdown, called from reboot).
 * for a true shutdown, reset and boot, cpu 0 should then reset itself or the
 * system as a whole.  if plan 9 is rebooting (loading a new kernel and
 * starting it), cpu 0 needs to keep running to do that work, so we return
 * in that case only.
 *
 * we assume shutdown has previously been called on this cpu.
 */
void
mpshutdown(void)
{
	//void (*tramp)(Reboot *);

		panic("implement all this mpshutdown jazz");
#ifdef xxx
	if(m->machno == 0) {
		static QLock mpshutdownlock;

		qlock(&mpshutdownlock);
		/* we are the first proc on cpu0 to begin shutdown */
		if(!active.rebooting) {
			haltsys();
			notreached();
		}
	} else {
		/*
		 * we're secondary, we should go into sys->reboottramp to enter
		 * wfi.  if idling, sys->Reboot won't be set and that's okay.
		 */
		cacheflush();
		mmulowidmap();			/* disables user mapping too */
		usephysdevaddrs();

		/* await populated sys->Reboot */
		/* secstall is cleared by schedcpus, settrampargs from reboot */
		while (sys->secstall == RBFLAGSET)
			coherence();
		//tramp = (void (*)(Reboot *))PADDR(sys->reboottramp);
		//(*tramp)(&sys->Reboot);
		panic("mpshutdown FIXME");
	}
#endif
}

/*
 * there are two main timers: the system-wide clint clock (mtime)
 * and the per-core cpu clock (rdtsc).  The clint clock is typically
 * something like 1 or 10 MHz and the cpu clock 600 - 1800 MHz.
 */

#ifdef NO
/*
 *  return value and frequency of timer (via hz)
 */
uvlong
fastticks(uvlong* hz)
{
	uvlong clticks;

	if(hz != nil)
		*hz = timebase;
	coherence();
	clticks = 0; // FIX ME WHAT A FUCKING MESS CLOCKS ARE rdcltime();
	if (sys->epoch == 0)
		return clticks;
	if (clticks >= sys->epoch)
		clticks -= sys->epoch;		/* clticks since boot */
	else
		iprint("fastticks: cpu%d: clticks %,lld < sys->epoch %,lld\n",
			m->machno, clticks, sys->epoch);
	return clticks;
}
#endif

#ifdef WHICHONETHISORCLOCKC
ulong
µs(void)
{
	return fastticks2us(fastticks(nil));
}
#endif
