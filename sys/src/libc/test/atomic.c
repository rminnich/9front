#include <u.h>
#include <libc.h>

Along nextid;

Along	counter;
Along	done0;
Along	done1;
long	val0;
long	val1;

int
spawn(void (*f)(void*), void *p)
{
	int pid;

	pid = rfork(RFMEM|RFPROC);
	switch(pid){
	case -1:
		sysfatal("rfork");
	case 0:
		f(p);
		exits("spawn");
	default:
		return pid;
	}
}

void
inc(void *p)
{
	int i;

	for(i = 0; i < 10000; i++)
		aincl(p, 1);
}

void
dec(void *p)
{
	int i;

	for(i = 0; i < 10000; i++)
		aincl(p, -1);
}

void
casinc(void *p)
{
	int i;
	long v;

	for(i = 0; i < 10000; i++) do{
		v = agetl(p);
	}while(!acasl(p, v, v+1));
}

void
casdec(void *p)
{
	int i;
	long v;

	for(i = 0; i < 10000; i++) do{
		v = agetl(p);
	}while(!acasl(p, v, v-1));
}

void
toggle0(void*)
{
	val0 = 1;
	aswapl(&done0, 1);
	while(agetl(&done1) != 1)
		/* wait */;
	assert(val1 == 1);
}

void
toggle1(void*)
{
	val1 = 1;
	aswapl(&done1, 1);
	while(agetl(&done0) != 1)
		/* wait */;
	assert(val0 == 1);
}

void
main(void)
{
	Along l;
	Avlong v;
	Aptr p;
	int i;

	/* smoke test: does it work at all */
	aswapl(&l, 1);
	assert(agetl(&l) == 1);
	assert(aincl(&l, 1) == 2);
	assert(aincl(&l, -1) == 1);
	assert(acasl(&l, 42, 123) == 0);
	assert(agetl(&l) == 1);
	assert(aswapl(&l, 77) == 1);
	assert(acasl(&l, 77, 42) == 1);
	assert(agetl(&l) == 42);

	aswapv(&v, 1);
	assert(agetv(&v) == 1);
	assert(aincv(&v, 1) == 2);
	assert(aincv(&v, -1) == 1);
	assert(acasv(&v, 42, 123) == 0);
	assert(agetv(&v) == 1);
	assert(aswapv(&v, 77) == 1);
	assert(acasv(&v, 77, 42) == 1);
	assert(agetv(&v) == 42);

	aswapp(&p, &v);
	assert(agetp(&p) == &v);
	assert(acasp(&p, &l, &i) == 0);
	assert(agetp(&p) == &v);
	assert(acasp(&p, &v, &i) == 1);
	assert(agetp(&p) == &i);

	/* do our counters look atomic */
	for(i = 0; i < 10; i++){
		spawn(inc, &counter);
		spawn(dec, &counter);
	}
	for(i = 0; i < 10; i++){
		free(wait());
		free(wait());
	}
	assert(agetl(&counter) == 0);

	/* how about when cas'ing */
	for(i = 0; i < 1000; i++){
		spawn(casinc, &counter);
		spawn(casdec, &counter);
	}
	for(i = 0; i < 1000; i++){
		free(wait());
		free(wait());
	}
	assert(agetl(&counter) == 0);

	/* do the atomics act as barriers? */
	for(i = 0; i < 10000; i++){
		spawn(toggle0, &counter);
		spawn(toggle1, &counter);
		free(wait());
		free(wait());
	}
	assert(agetl(&counter) == 0);
	exits(nil);
}
