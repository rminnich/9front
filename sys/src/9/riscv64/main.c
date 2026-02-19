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
	char buf[2*KNAMELEN], **sp, **usp;
	Page *p;

	print("chandevinit ...\n");
	chandevinit();

	if(!waserror()){
		snprint(buf, sizeof(buf), "%s %s", "ARM64", conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "riscv64", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		print("setconfenv\n");
		setconfenv();
		print("DONE ...\n");
		poperror();
	}
	print("alarm kproc\n");
	kproc("alarm", alarmkproc, 0);
	print("done\n");

	p = newpage(USTKTOP-BY2PG, nil);
	segpage(up->seg[SSEG], p);
	sp = (char**)(p->pa + BY2PG - sizeof(Tos) - 8 - sizeof(sp[0])*4);
	print("sp is %p for usp %p\n", sp, sp);
	extern int block;
	sp[3] = sp[2] = sp[1] = nil;
	strcpy(sp[1] = (char*)&sp[4], "boot");
	sp[0] = (void*)&sp[1];
	print("sp all set up\n");
	print("fpukexit ...\n");

	splhi();
	fpukexit(nil, nil);
	print("done ... call mmuswitch\n");
	mmuswitch(up);
	if (1) {
	print("fault %d\n", fault(UTZERO, UTZERO, 1));
	print("fault sp %d\n", fault(p->va, UTZERO, 0));
	u64int* pte = userpte((void *)UTZERO);
	print("pte is %p *pte %llx\n", pte, *pte);
	}
	print("touser baby MACH m is %p mmuto p%p\n", m, m->mmutop);

	print("islo %d, now enable clock\n", islo());
	block = 1;
	if (0)while(! block);
	if(1)clockenable();
	//print("now to user\n");
	touser((uintptr)USTKTOP-BY2PG);
}

void
confinit(void)
{
	int userpcnt;
	ulong kpages;
	char *p;
	int i;
	
	sys = &asys;

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
//	if(kpages > ((uintptr)-VDRAM)/BY2PG)
//		kpages = ((uintptr)-VDRAM)/BY2PG;

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
	kpages = conf.npage - conf.upages;
	print("kpages to start is %lx = %lx - %lx \n", kpages, conf.npage, conf.upages);
	kpages *= BY2PG;
	print("%lx -= %lx + %lx + %lx + %lx + %x\n", 
		kpages,conf.upages*sizeof(Page)
		, conf.nproc*sizeof(Proc*)
		, conf.nimage*sizeof(Image)
		, conf.nswap
		, conf.nswppo*sizeof(Page*));
	kpages -= conf.upages*sizeof(Page)
		+ conf.nproc*sizeof(Proc*)
		+ conf.nimage*sizeof(Image)
		+ conf.nswap
		+ conf.nswppo*sizeof(Page*);
	print("kpages now is ... %lx\n", kpages);
	//kpages *= BY2PG;
	print("exit with kpages %lx\n", kpages);
	mainmem->maxsize = kpages;
	imagmem->maxsize = kpages;
}

void
machinit(void)
{
	extern void strap(void);
	wstvec((uintptr)strap);
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
int once = 0;
void
main(void)
{
	if (once) panic("main entered twice");
	once++;
	while(i == 0); // BUG: does not reload i
	i = 1; // in case you use GDB to bump PC, make sure i is updated.
	while (i < 32) {
		sbiputc('b');
		i++;
	}
	if (!m->machno) {
		extern char bdata[], edata[], end[], etext[];
		static ulong vfy = 0xcafebabe;
	
		if (vfy != 0xcafebabe){
			sbiputc('Z');
			memmove(bdata, etext, edata - bdata);
		}
		if (vfy != 0xcafebabe) {
			sbiputc('?');
			panic("misaligned data segment");
		}
		memset(edata, 0, end - edata);		/* zero bss */
		vfy = 0;
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
	print("let's try an sbigetc()\n");
	if (0)for(int i = 0; i < 16; i++) {
		int c;
		int tries;
		sbiputc('=');
		for(c = sbigetc(), tries = 0; tries < 1<<16 && c < 0; tries++, c = sbigetc())
			;
		print("Got %#x, tries %d\n", c, tries);
	}
//	bootargsinit();
	meminit();
	check();
	print("meminit done\n");
	confinit();
	print("confinit done\n");
	xinit();
	print("xinit done\n");
	check();
	xsummary();
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
	timebase = 10*Mhz;
	timebase * 100; // XXX XXX XXX
	calibrate(); print("DONE calibrate\n");
	clocksanity(); print("DONE clocksanity\n");
	cpuidprint(); print("DONE 	cpuidprint();\n"); 
	check();
	timersinit(); print("DONE 	timersinit();\n");
	check(); 
	xsummary();
	pageinit(); print("DONE 	pageinit();\n"); 
	check();
	procinit0(); print("DONE 	procinit0();\n"); 
	check();
	initseg(); print("DONE 	initseg();\n"); 
	check();
	links(); print("DONE 	links();\n"); 
	check();
	chandevreset(); print("DONE 	chandevreset();\n"); 
	check();
	userinit(); print("DONE 	userinit();\n"); 
	check();
	mpinit(); print("DONE 	mpinit();\n"); 
	u64int tm = rdtime();
	print("tm is %#llx\n", tm);
	u64int cm = rdstimecmp();
	print("cm is %#llx\n", cm);
	mmu1init(); print("DONE 	mmu1init(); islo %d m %p\n", islo(), m); 
	cpuinit(0); print("DONE cpuinit(0) -- FIXME\n");
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
	/* not needed. -- satp never changes*/
	//setttbr(PADDR(L1BOT));

	extern void strap(void);
	wstvec((uintptr)strap);
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
void setconfenv(void)
{
	print("NOT DOING setconfenv\n");
}
