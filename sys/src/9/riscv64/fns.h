#include "../port/portfns.h"

/* l.s */
extern void sev(void);
extern int tas(void *);
extern int cmpswap(long*, long, long);
extern void coherence(void);
extern void idlehands(void);
extern uvlong vcycles(void);
#define cycles(ip) *(ip) = vcycles()
extern int splfhi(void);
extern void splflo(void);
extern void touser(uintptr sp);
extern void sysrforkret(void);
extern void noteret(void);
extern void returnto(void*);
extern void fpon(void);
extern void fpoff(void);
extern void fpsaveregs(void*);
extern void fploadregs(void*);
extern void hvccall(Ureg*);

extern void setttbr(uintptr pa);
extern uintptr getfar(void);

extern void flushasidva(uintptr asidva);
extern void tlbivae1is(uintptr asidva);

extern void flushasidvall(uintptr asidva);
extern void tlbivale1is(uintptr asidva);

extern void flushasid(uintptr asid);
extern void tlbiaside1is(uintptr asid);

extern void flushtlb(void);
extern void tlbivmalle1(void);

extern void flushlocaltlb(void);
extern void tlbivmalle1(void);

extern void sbiputc(uvlong);
extern int sbigetc();

/* cache */
extern ulong cachesize(int level);

extern void cacheiinvse(void*, int);
extern void cacheuwbinv(void);
extern void cacheiinv(void);

extern void cachedwbse(void*, int);
extern void cacheduwbse(void*, int);
extern void cachedinvse(void*, int);
extern void cachedwbinvse(void*, int);

extern void cachedwb(void);
extern void cachedinv(void);
extern void cachedwbinv(void);

extern void l2cacheuwb(void);
extern void l2cacheuinv(void);
extern void l2cacheuwbinv(void);

/* mmu */
#define	getpgcolor(a)	0
extern uintptr paddr(void*);
#define PADDR(a) paddr((void*)(a))
extern uintptr cankaddr(uintptr);
extern void* kaddr(uintptr);
#define KADDR(a) kaddr(a)
extern void kmapinval(void);
#define	VA(k)	((uintptr)(k))
extern KMap *kmap(Page*);
extern void kunmap(KMap*);
extern uintptr mmukmap(uintptr, uintptr, usize);
extern void kmapram(uintptr, uintptr);
extern void* vmap(uvlong, vlong);
extern void vunmap(void*, vlong);
extern void mmu1init(void);
extern void putasid(Proc*);

/* mem */
extern void mmuidmap(uintptr*);
extern void mmu0init(uintptr*);
extern void meminit(void);

/* clock */
extern void clockinit(void);
extern void synccycles(void);
extern void armtimerset(int);
extern void clockshutdown(void);

/* fpu */
extern void fpuinit(void);
extern void fpuprocsetup(Proc*);
extern void fpuprocfork(Proc*);
extern void fpuprocsave(Proc*);
extern void fpuprocrestore(Proc*);
extern FPsave* fpukenter(Ureg*);
extern void fpukexit(Ureg*, FPsave*);
extern void mathtrap(Ureg*);

/* trap */
extern void trapinit(void);
extern int userureg(Ureg*);
extern void evenaddr(uintptr);
extern void setkernur(Ureg*, Proc*);
extern void procfork(Proc*);
extern void procsetup(Proc*);
extern void procsave(Proc*);
extern void procrestore(Proc *);
extern void trap(Ureg*);
extern void syscall(Ureg*);
extern void faultarm64(Ureg*);
extern void dumpstack(void);
extern void dumpregs(Ureg*);

/* irq */
extern void intrinit(void);
extern void intrcpushutdown(void);
extern void intrsoff(void);
extern void intrenable(int, void (*)(Ureg*, void*), void*, int, char*);
extern void intrdisable(int, void (*)(Ureg*, void*), void*, int, char*);
extern int irq(Ureg*);
extern void fiq(Ureg*);

/* sysreg */
extern uvlong	sysrd(ulong);
extern void	syswr(ulong, uvlong);

/* uartimx */
extern void uartconsinit(void);

/* dma */
extern void dmaflush(int, void*, ulong);

/* main */
extern char *getconf(char *name);
extern void setconfenv(void);
extern void writeconf(void);

extern int isaconfig(char*, int, ISAConf*);
extern void links(void);

/* ccm */
extern void setclkgate(char *name, int on);
extern void setclkrate(char *name, char *source, int freq);
extern int getclkrate(char *name);

/* gpc */
extern void powerup(char *dom);

/* lcd */
extern void lcdinit(void);

/* iomux */
extern void iomuxpad(char *pads, char *sel, char *cfg);
extern uint iomuxgpr(int gpr, uint set, uint mask);

/* gpio */
#define GPIO_PIN(n, m)	((n)<<5 | (m))
extern void gpioout(uint pin, int set);
extern int gpioin(uint pin);
void gpiointrenable(uint pin, int mode, void (*f)(uint pin, void *a), void *a);
void gpiointrdisable(uint pin);

/* pciqemu */
extern int pcicfgrw8(int tbdf, int rno, int data, int read);
extern int pcicfgrw16(int tbdf, int rno, int data, int read);
extern int pcicfgrw32(int tbdf, int rno, int data, int read);
extern void pciintrenable(int tbdf, void (*f)(Ureg*, void*), void *a);
extern void pciintrdisable(int tbdf, void (*f)(Ureg*, void*), void *a);

/* bootargs */
extern void bootargsinit(void);

// new
void wsatp(u64int);
void wstvec(u64int);
u64int rsatp(void);
void *usertokernel(void *v);
u64int *userpte(void *v);
void flushalltlb(void);
void flushvatlb(uintptr);

// not all implemented
void	clearipi(void);
void	clockenable(void);
void	clockintr(Ureg* ureg, void *);
void	clockoff(void);
int	clocksanity(void);
void	clrreserv(void);
uintptr	clrsie(uintptr);
ulong	clrsipbit(ulong);
void	clrstie(void);
void	cboinval(void *);
void	cboflush(void *);

// more good 9k stuff: m mode bits. 
// Let's keep them until we're sure we don't want them.
ulong	getfcsr(void);
uintptr	getmie(void);
uintptr	getmip(void);
uintptr	getmsts(void);
void*	getmtvec(void);
void	putmie(uintptr);
void	putmip(uintptr);
void	putmsts(uintptr);
void*	putmtvec(void *);
uvlong	rdtime(void);
uvlong	rdtsc(void);
void	sbisettimer(uvlong);

/* SBI */
vlong	sbicall(uvlong, uvlong, uvlong, Sbiret *, uvlong *);
vlong	sbiclearipi(void);
vlong	sbiecall(uvlong, uvlong, uvlong, Sbiret *, uvlong *);
vlong	sbigetimplid(void);
vlong	sbigetimplvers(void);
vlong	sbigetmarchid(void);
vlong	sbigetmvendorid(void);
vlong	sbihartstart(uvlong hartid, uvlong phys_start, uvlong private);
vlong	sbihartstatus(uvlong hartid);
vlong	sbihartsuspend(void);
vlong	sbiprobeext(uvlong);
void	sbireset(ulong type, ulong reason);
vlong	sbisendipi(uvlong map[]);
void	sbisettimer(uvlong);
void	sbishutdown(void);

uintptr	getsie(void);
uintptr	getsip(void);
uintptr	getsp(void);
uintptr	getsts(void);
void*	getstvec(void);
void	putsie(uintptr);
void	putsip(uintptr);
void	putsscratch(uintptr);
void	putsts(uintptr);
void*	putstvec(void *);

/* archrv */
void calibrate(void);
uint mach2context(Mach *);

/* rva23 */
void wrstimecmp(u64int);
u64int rdstimecmp(void);

void soft(void);
