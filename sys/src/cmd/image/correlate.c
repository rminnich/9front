#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <memdraw.h>
#include "fns.h"

char kerndir[] = "/lib/image/filter";

void
reversek(double *k, int len)
{
	double *e, t;

	e = k + len;
	while(k < e){
		t = *k;
		*k++ = *--e;
		*e = t;
	}
}

#define isspace(c)	((c) == ' ' || (c) == '\t')
Memimage *
readimagekernel(int fd, double denom, int reverse)
{
	Memimage *ik;
	Biobuf *bin;
	double *k, d;
	int nk, dx0, dx, dy;
	char *line;

	k = nil;
	nk = 0;
	dx0 = dx = dy = 0;

	bin = Bfdopen(fd, OREAD);
	if(bin == nil)
		sysfatal("Bfdopen: %r");

	while((line = Brdstr(bin, '\n', 1)) != nil){
		while(*line){
			d = strtod(line, &line);
			if(*line && !isspace(*line)){
				free(k);
				Bterm(bin);
				werrstr("bad image kernel file");
				return nil;
			}
			k = erealloc(k, (nk + 1)*sizeof(double));
			k[nk++] = d;
			dx++;
		}
		if(dy > 0 && dx != dx0){
			free(k);
			Bterm(bin);
			werrstr("image kernel file has inconsistent dx");
			return nil;
		}
		if(dy++ == 0)
			dx0 = dx;
		dx = 0;
	}
	Bterm(bin);

	if(reverse)
		reversek(k, dx0*dy);

	ik = allocmemimagekernel(k, dx0, dy, denom);
	free(k);
	if(ik == nil){
		werrstr("allocmemimagekernel: %r");
		return nil;
	}
	return ik;
}

void
usage(void)
{
	fprint(2, "usage: %s [-cRp] kernel [denom]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Memimage *dst, *src;
	Memimage *ikern;
	Rectangle dr, *wr;
	int convolve, dorepl, parallel, fd, nproc, i;
	char *nprocs, *p, kernf[128];
	double denom;

	denom = 0;
	convolve = 0;
	dorepl = 0;
	parallel = 0;
	ARGBEGIN{
	case 'c':
		convolve++;
		break;
	case 'R':
		dorepl++;
		break;
	case 'p':
		parallel++;
		break;
	default:
		usage();
	}ARGEND;
	switch(argc){
	case 2:
		denom = strtod(argv[1], nil);
		/* fallthrough */
	case 1:
		break;
	default:
		usage();
	}

	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");

	fd = open(argv[0], OREAD);
	if(fd < 0){
		p = strrchr(argv[0], '/');
		p = p? p+1: argv[0];
		snprint(kernf, sizeof kernf, "%s/%s", kerndir, p);
		fd = open(kernf, OREAD);
		if(fd < 0)
			sysfatal("could not find filter: %r");
	}
	ikern = readimagekernel(fd, denom, convolve);
	if(ikern == nil)
		sysfatal("readimagekernel: %r");
	close(fd);

	src = ereadmemimage(0);
	if(dorepl)
		src->flags |= Frepl;

	dr = src->r;
	dst = eallocmemimage(dr, src->chan);
	memfillcolor(dst, DTransparent);

	if(parallel){
		nprocs = getenv("NPROC");
		if(nprocs == nil || (nproc = strtoul(nprocs, nil, 10)) < 2)
			nproc = 1;
		free(nprocs);

		wr = emalloc(nproc*sizeof(Rectangle));
		initworkrects(wr, nproc, &dr);

		for(i = 0; i < nproc; i++){
			switch(rfork(RFPROC|RFMEM)){
			case -1:
				sysfatal("rfork: %r");
			case 0:
				if(memimagecorrelate(dst, wr[i], src, src->r.min, ikern) < 0)
					fprint(2, "[%d] memimagecorrelate: %r\n", getpid());
				exits(nil);
			}
		}
		while(waitpid() != -1)
			;

		free(wr);
	}else
		if(memimagecorrelate(dst, dr, src, src->r.min, ikern) < 0)
			sysfatal("memimagecorrelate: %r");

	ewritememimage(1, dst);

	exits(nil);
}
