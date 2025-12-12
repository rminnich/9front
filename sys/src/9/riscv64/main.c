#include "u.h"
#include "tos.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "pool.h"
#include "io.h"
#include "sysreg.h"
#include "ureg.h"

//#include "rebootcode.i"

Conf conf;

int
isaconfig(char *, int, ISAConf *)
{
	return 0;
}

/*
 *  starting place for first process
 */
void
init0(void)
{
#ifdef XXX
	char buf[2*KNAMELEN], **sp;

	chandevinit();

	if(!waserror()){
		snprint(buf, sizeof(buf), "%s %s", "ARM64", conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "arm64", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		setconfenv();
		poperror();
	}
	kproc("alarm", alarmkproc, 0);

	sp = (char**)(USTKTOP-sizeof(Tos) - 8 - sizeof(sp[0])*4);
	sp[3] = sp[2] = sp[1] = nil;
	strcpy(sp[1] = (char*)&sp[4], "boot");
	sp[0] = (void*)&sp[1];

	splhi();
	fpukexit(nil, nil);
	touser((uintptr)sp);
#endif
}

void
confinit(void)
{
	int userpcnt;
	ulong kpages;
	char *p;
	int i;

	memset(&conf, 0, sizeof(conf));
	conf.nmach = 1;
	if(p = getconf("*ncpu"))
		conf.nmach = strtol(p, 0, 0);
	if(conf.nmach > MAXMACH)
		conf.nmach = MAXMACH;

	if(p = getconf("service")){
		if(strcmp(p, "cpu") == 0)
			cpuserver = 1;
		else if(strcmp(p,"terminal") == 0)
			cpuserver = 0;
	}

	if(p = getconf("*kernelpercent"))
		userpcnt = 100 - strtol(p, 0, 0);
	else
		userpcnt = 0;

	if(userpcnt < 10)
		userpcnt = 60 + cpuserver*10;
	print("confinit start npage crap\n");
	conf.npage = 0;
	for(i = 0; i < nelem(conf.mem); i++)
		conf.npage += conf.mem[i].npage;

	print("conf.npage 0x%lx\n", conf.npage);
	kpages = conf.npage - (conf.npage*userpcnt)/100;
	if(kpages > ((uintptr)-VDRAM)/BY2PG)
		kpages = ((uintptr)-VDRAM)/BY2PG;

	print("kpages 0x%lx\n", kpages);
	conf.upages = conf.npage - kpages;
	conf.ialloc = (kpages/2)*BY2PG;
	print("kpages 0x%lx conf.upages 0x%lx\n", kpages, conf.upages);

	/* set up other configuration parameters */
	conf.nproc = 100 + ((conf.npage*BY2PG)/MB)*5;
	if(cpuserver)
		conf.nproc *= 3;
	if(conf.nproc > 4000)
		conf.nproc = 4000;
	conf.nswap = conf.npage*3;
	conf.nswppo = 4096;
	conf.nimage = 200;

	conf.copymode = conf.nmach > 1;

	/*
	 * Guess how much is taken by the large permanent
	 * datastructures. Mntcache and Mntrpc are not accounted for.
	 */
	// We did all that work above, and here we throw it away. Sod that.
	if (0) {
	kpages = conf.npage - conf.upages;
	print("kpages to start is %lx = %lx - %lx \n", kpages, conf.npage, conf.upages);
	kpages *= BY2PG;
	kpages -= conf.upages*sizeof(Page)
		+ conf.nproc*sizeof(Proc*)
		+ conf.nimage*sizeof(Image)
		+ conf.nswap
		+ conf.nswppo*sizeof(Page*);
	}
	kpages *= BY2PG;
	print("exit with kpages %lx\n", kpages);
	mainmem->maxsize = kpages;
	imagmem->maxsize = kpages;
}

void
machinit(void)
{
	m->ticks = 1;
	m->perf.period = 1;
	active.machs[m->machno] = 1;
}

void
mpinit(void)
{
#ifdef XXX
	extern void _start(void);
	int i;

	for(i = 1; i < conf.nmach; i++){
		Ureg u = {0};

		MACHP(i)->machno = i;
		cachedwbinvse(MACHP(i), MACHSIZE);

		u.r0 = 0x84000003;	/* CPU_ON */
		u.r1 = (sysrd(MPIDR_EL1) & ~(0xFF0000FFULL)) | i;
		u.r2 = PADDR(_start);
		u.r3 = i;
		hvccall(&u);
	}
	synccycles();
#endif
}

void
cpuidprint(void)
{
	iprint("cpu%d: 1000MHz QEMU\n", m->machno);
}

char *
getconf(char * )
{
	return nil;
}

int i = 1;
void
main(void)
{
	while(i == 0); // BUG: does not reload i
	i = 1; // in case you use GDB to bump PC, make sure i is updated.
	while (i < 32) {
		sbiputc('b');
		i++;
	}
	machinit();
	while (i < 64) {
		sbiputc('c');
		i++;
	}
#ifdef XXX
	if(m->machno){
		trapinit();
		fpuinit();
		intrinit();
		clockinit();
		cpuidprint();
		synccycles();
		timersinit();
		mmu1init();
		m->ticks = MACHP(0)->ticks;
		schedinit();
		return;
	}
#endif
	uartconsinit();
	sbiputc('^');
	quotefmtinstall();
	sbiputc('q');
	print("hi there\n");
//	bootargsinit();
	meminit();
	print("meminit done\n");
	confinit();
	print("confinit done\n");
	xinit();
	print("xinit done\n");
	printinit();
	print("\nPlan 9\n");
	print("\nPlan %d\n", 9);
	print("conf.mem[0].base %p, conf.mem[0].limit %p, conf.mem[0].npage %lud\n", conf.mem[0].base, conf.mem[0].limit, conf.mem[0].npage);
#ifdef xxx
	trapinit(); print("DONE 	trapinit();\n"); 
	fpuinit(); print("DONE 	fpuinit();\n"); 
	intrinit(); print("DONE 	intrinit();\n"); 
#endif
	clockinit(); print("DONE 	clockinit();\n"); 
	cpuidprint(); print("DONE 	cpuidprint();\n"); 
	timersinit(); print("DONE 	timersinit();\n"); 
	pageinit(); print("DONE 	pageinit();\n"); 
	procinit0(); print("DONE 	procinit0();\n"); 
	initseg(); print("DONE 	initseg();\n"); 
	links(); print("DONE 	links();\n"); 
	chandevreset(); print("DONE 	chandevreset();\n"); 
	userinit(); print("DONE 	userinit();\n"); 
	mpinit(); print("DONE 	mpinit();\n"); 
	mmu1init(); print("DONE 	mmu1init();\n"); 
	schedinit();
}

void
exit(int)
{
#ifdef XXX
	Ureg u = { .r0 = 0x84000002 };	/* CPU_OFF */

	cpushutdown();
	splfhi();

	if(m->machno == 0){
		/* clear secrets */
		zeroprivatepages();
		poolreset(secrmem);

		u.r0 = 0x84000009;	/* SYSTEM RESET */
	}
	hvccall(&u);
#endif
}

static void
rebootjump(void *entry, void *code, ulong size)
{
	print("rebootjump: %p, %p, %ld\n", entry, code, size);
	panic("rebootjmp");
#ifdef XXX
	void (*f)(void*, void*, ulong);

	intrcpushutdown();

	/* redo identity map */
	setttbr(PADDR(L1BOT));

	/* setup reboot trampoline function */
	f = (void*)REBOOTADDR;
	memmove(f, rebootcode, sizeof(rebootcode));

	cachedwbinvse(f, sizeof(rebootcode));
	cacheiinvse(f, sizeof(rebootcode));

	(*f)(entry, code, size);
#endif
}

void
reboot(void*p, void *code, ulong size)
{
	print("reboot %p %p %ld\n", p, code, size);
	panic("reboot");
#ifdef XXX
	writeconf();
	while(m->machno != 0){
		procwired(up, 0);
		sched();
	}

	cpushutdown();
	delay(2000);

	splfhi();

	/* turn off buffered serial console */
	serialoq = nil;

	/* shutdown devices */
	chandevshutdown();

	/* stop the clock */
	clockshutdown();
	intrsoff();

	/* clear secrets */
	zeroprivatepages();
	poolreset(secrmem);

	/* off we go - never to return */
	rebootjump((void*)(KTZERO-KZERO), code, size);
#endif
}

void
dmaflush(int clean, void *p, ulong len)
{
	print("dmaflush %d %p %ld\n", clean, p, len);
	panic("dmaflush");
#ifdef XXX
	uintptr s = (uintptr)p;
	uintptr e = (uintptr)p + len;

	if(clean){
		s &= ~(BLOCKALIGN-1);
		e += BLOCKALIGN-1;
		e &= ~(BLOCKALIGN-1);
		cachedwbse((void*)s, e - s);
		return;
	}
	if(s & BLOCKALIGN-1){
		s &= ~(BLOCKALIGN-1);
		cachedwbinvse((void*)s, BLOCKALIGN);
		s += BLOCKALIGN;
	}
	if(e & BLOCKALIGN-1){
		e &= ~(BLOCKALIGN-1);
		if(e < s)
			return;
		cachedwbinvse((void*)e, BLOCKALIGN);
	}
	if(s < e)
		cachedinvse((void*)s, e - s);
#endif
}
