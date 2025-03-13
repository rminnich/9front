#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"edf.h"

#include	<a.out.h>

static int
shargs(char *s, int n, char **ap, int nap)
{
	char *p;
	int i;

	if(n <= 2 || s[0] != '#' || s[1] != '!')
		return -1;
	s += 2;
	n -= 2;		/* skip #! */
	if((p = memchr(s, '\n', n)) == nil)
		return 0;
	*p = 0;
	i = tokenize(s, ap, nap-1);
	ap[i] = nil;
	return i;
}

/* TODO: in nix, we had common code for both of these. We're not ready yet. */
uintptr
sysexecac(va_list list)
{
	union {
		struct {
			Exec;
			uvlong	hdr[1];
		} ehdr;
		char buf[256];
	} u;
	char line[256];
	char *progarg[32+1];
	volatile char *args, *elem, *file0;
	char **argv, **argp, **argp0;
	char *a, *e, *charp, *file;
	int i, n, indir;
	ulong magic, ssize, nargs, nbytes;
	uintptr t, d, b, entry, text, data, bss, bssend, tstk, align;
	Segment *s, *ts;
	Image *img;
	Tos *tos;
	Chan *tc;
	Fgrp *f;
	uintptr ar0;
	/* NIX */
	uint flags;

	args = elem = nil;
	flags = va_arg(list, unsigned int);
	switch(flags){
	case EXTC:
	case EXXC:
		break;
	case EXAC:
		up->ac = getac(up, -1);
		break;
	default:
		error("unknown execac flag");
	}
	file0 = va_arg(list, char*);
	validaddr((uintptr)file0, 1, 0);
	argp0 = va_arg(list, char**);
	evenaddr((uintptr)argp0);
	validaddr((uintptr)argp0, 2*BY2WD, 0);
	if(*argp0 == nil)
		error(Ebadarg);
	file0 = validnamedup(file0, 1);
	if(waserror()){
		if(flags == EXAC && up->ac != nil)
			up->ac->proc = nil;
		up->ac = nil;
		free(file0);
		free(elem);
		free(args);
		/* Disaster after commit */
		if(up->seg[SSEG] == nil)
			pexit(up->errstr, 1);
		nexterror();
	}
	align = BY2PG-1;
	indir = 0;
	file = file0;
	for(;;){
		tc = namec(file, Aopen, OEXEC, 0);
		if(waserror()){
			cclose(tc);
			nexterror();
		}
		if(!indir)
			kstrdup(&elem, up->genbuf);

		n = devtab[tc->type]->read(tc, u.buf, sizeof(u.buf), 0);
		if(n >= sizeof(Exec)) {
			magic = beswal(u.ehdr.magic);
			if(magic == AOUT_MAGIC) {
				if(magic & HDR_MAGIC) {
					if(n < sizeof(u.ehdr))
						error(Ebadexec);
					entry = beswav(u.ehdr.hdr[0]);
					text = UTZERO+sizeof(u.ehdr);
				} else {
					entry = beswal(u.ehdr.entry);
					text = UTZERO+sizeof(Exec);
				}
				if(entry < text)
					error(Ebadexec);
				text += beswal(u.ehdr.text);
				if(text <= entry || text >= (USTKTOP-USTKSIZE))
					error(Ebadexec);

				switch(magic){
				case S_MAGIC:	/* 2MB segment alignment for amd64 */
					align = 0x1fffff;
					break;
				case P_MAGIC:	/* 16K segment alignment for spim */
				case V_MAGIC:	/* 16K segment alignment for mips */
					align = 0x3fff;
					break;
				case R_MAGIC:	/* 64K segment alignment for arm64 */
					align = 0xffff;
					break;
				}
				break; /* for binary */
			}
		}

		if(indir++)
			error(Ebadexec);

		/*
		 * Process #! /bin/sh args ...
		 */
		memmove(line, u.buf, n);
		n = shargs(line, n, progarg, nelem(progarg));
		if(n < 1)
			error(Ebadexec);
		/*
		 * First arg becomes complete file name
		 */
		progarg[n++] = file;
		progarg[n] = nil;
		argp0++;
		file = progarg[0];
		progarg[0] = elem;
		poperror();
		cclose(tc);
	}

	t = (text+align) & ~align;
	text -= UTZERO;
	data = beswal(u.ehdr.data);
	bss = beswal(u.ehdr.bss);
	align = BY2PG-1;
	d = (t + data + align) & ~align;
	bssend = t + data + bss;
	b = (bssend + align) & ~align;
	if(t >= (USTKTOP-USTKSIZE) || d >= (USTKTOP-USTKSIZE) || b >= (USTKTOP-USTKSIZE))
		error(Ebadexec);

	/*
	 * Args: pass 1: count
	 */
	nbytes = sizeof(Tos);		/* hole for profiling clock at top of stack (and more) */
	nargs = 0;
	if(indir){
		argp = progarg;
		while(*argp != nil){
			a = *argp++;
			nbytes += strlen(a) + 1;
			nargs++;
		}
	}
	argp = argp0;
	while(*argp != nil){
		a = *argp++;
		if(((uintptr)argp&(BY2PG-1)) < BY2WD)
			validaddr((uintptr)argp, BY2WD, 0);
		validaddr((uintptr)a, 1, 0);
		e = vmemchr(a, 0, USTKSIZE);
		if(e == nil)
			error(Ebadarg);
		nbytes += (e - a) + 1;
		if(nbytes >= USTKSIZE)
			error(Enovmem);
		nargs++;
	}
	ssize = BY2WD*(nargs+1) + ((nbytes+(BY2WD-1)) & ~(BY2WD-1));

	/*
	 * 8-byte align SP for those (e.g. sparc) that need it.
	 * execregs() will subtract another 4 bytes for argc.
	 */
	if(BY2WD == 4 && (ssize+4) & 7)
		ssize += 4;

	if(PGROUND(ssize) >= USTKSIZE)
		error(Enovmem);

	/*
	 * Build the stack segment, putting it in kernel virtual for the moment
	 */
	qlock(&up->seglock);
	if(waserror()){
		s = up->seg[ESEG];
		if(s != nil){
			up->seg[ESEG] = nil;
			putseg(s);
		}
		qunlock(&up->seglock);
		nexterror();
	}

	s = up->seg[SSEG];
	do {
		tstk = s->base;
		if(tstk <= USTKSIZE)
			error(Enovmem);
	} while((s = isoverlap(tstk-USTKSIZE, USTKSIZE)) != nil);
	up->seg[ESEG] = newseg(SG_STACK | SG_NOEXEC, tstk-USTKSIZE, USTKSIZE/BY2PG);

	/*
	 * Args: pass 2: assemble; the pages will be faulted in
	 */
	tos = (Tos*)(tstk - sizeof(Tos));
	tos->cyclefreq = m->cyclefreq;
	tos->kcycles = 0;
	tos->pcycles = 0;
	tos->clock = 0;

	argv = (char**)(tstk - ssize);
	charp = (char*)(tstk - nbytes);
	if(indir)
		argp = progarg;
	else
		argp = argp0;

	for(i=0; i<nargs; i++){
		if(indir && *argp==nil) {
			indir = 0;
			argp = argp0;
		}
		*argv++ = charp + (USTKTOP-tstk);
		a = *argp++;
		if(indir)
			e = strchr(a, 0);
		else {
			if(charp >= (char*)tos)
				error(Ebadarg);
			validaddr((uintptr)a, 1, 0);
			e = vmemchr(a, 0, (char*)tos - charp);
			if(e == nil)
				error(Ebadarg);
		}
		n = (e - a) + 1;
		memmove(charp, a, n);
		charp += n;
	}

	/* copy args; easiest from new process's stack */
	a = (char*)(tstk - nbytes);
	n = charp - a;
	if(n > 128)	/* don't waste too much space on huge arg lists */
		n = 128;
	args = smalloc(n);
	memmove(args, a, n);
	if(n>0 && args[n-1]!='\0'){
		/* make sure last arg is NUL-terminated */
		/* put NUL at UTF-8 character boundary */
		for(i=n-1; i>0; --i)
			if(fullrune(args+i, n-i))
				break;
		args[i] = 0;
		n = i+1;
	}

	/*
	 * Committed.
	 * Free old memory.
	 * Special segments are maintained across exec
	 */
	for(i = SSEG; i <= BSEG; i++) {
		s = up->seg[i];
		if(s != nil) {
			/* prevent a second free if we have an error */
			up->seg[i] = nil;
			putseg(s);
		}
	}
	for(i = ESEG+1; i < NSEG; i++) {
		s = up->seg[i];
		if(s != nil && (s->type&SG_CEXEC) != 0) {
			up->seg[i] = nil;
			putseg(s);
		}
	}

	/* Text.  Shared. Attaches to cache image if possible */
	/* attachimage returns a locked cache image */
	img = attachimage(SG_TEXT | SG_RONLY, tc, UTZERO, (t-UTZERO)>>PGSHIFT);
	ts = img->s;
	up->seg[TSEG] = ts;
	ts->flushme = 1;
	ts->fstart = 0;
	ts->flen = text;
	unlock(img);

	/* Data. Shared. */
	s = newseg(SG_DATA, t, (d-t)>>PGSHIFT);
	up->seg[DSEG] = s;

	/* Attached by hand */
	incref(img);
	s->image = img;
	s->fstart = ts->fstart+ts->flen;
	s->flen = data;

	/* BSS. Zero fill on demand */
	up->seg[BSEG] = newseg(SG_BSS, d, (b-d)>>PGSHIFT);

	/*
	 * Move the stack
	 */
	s = up->seg[ESEG];
	up->seg[ESEG] = nil;
	s->base = USTKTOP-USTKSIZE;
	s->top = USTKTOP;
	relocateseg(s, USTKTOP-tstk);
	up->seg[SSEG] = s;
	qunlock(&up->seglock);
	poperror();	/* seglock */

	/*
	 * Close on exec
	 */
	if((f = up->fgrp) != nil) {
		for(i=0; i<=f->maxfd; i++)
			fdclose(i, CCEXEC);
	}

	poperror();	/* tc */
	cclose(tc);
	poperror();	/* file0 */
	free(file0);

	qlock(&up->debug);
	free(up->text);
	up->text = elem;
	free(up->args);
	up->args = args;
	up->nargs = n;
	up->setargs = 0;

	freenotes(up);
	freenote(up->lastnote);
	up->lastnote = nil;
	up->notify = nil;
	up->notified = 0;
	up->ureg = nil;
	up->privatemem = 0;
	up->noswap = 0;
	up->pcycles = -up->kentry;
	procsetup(up);
	qunlock(&up->debug);

	up->errbuf0[0] = '\0';
	up->errbuf1[0] = '\0';

	/*
	 *  At this point, the mmu contains info about the old address
	 *  space and needs to be flushed
	 */
	flushmmu();
	if(up->prepagemem || flags == EXAC)
		nixprepage(-1);

	if(up->hang)
		up->procctl = Proc_stopme;
	ar0 = execregs(entry, ssize, nargs);
	if(flags == EXAC){
		up->procctl = Proc_toac;
		up->prepagemem = 1;
	}

	/*DBG("execac up %#p done\n"
		"textsz %lx datasz %lx bsssz %lx hdrsz %lx\n"
		"textlim %ullx datalim %ullx bsslim %ullx\n", up,
		textsz, datasz, bsssz, hdrsz, textlim, datalim, bsslim);*/
	return ar0;

}

