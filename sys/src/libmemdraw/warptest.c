#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>

uvlong t0;

void
profbegin(void)
{
	char buf[128];
	int fd;

	snprint(buf, sizeof(buf), "/proc/%d/ctl", getpid());
	fd = open(buf, OWRITE);
	if(fd < 0)
		sysfatal("open: %r");
	fprint(fd, "profile\n");
	close(fd);
	t0 = nsec();
}

void
profend(void)
{
	char buf[128];
	double dt;

	dt = (nsec() - t0)/1e6;
	fprint(2, "dt: %fms\n", dt);

	snprint(buf, sizeof(buf), "%d", getpid());
	switch(fork()){
	case -1:
		abort();
	case 0:
		dup(2, 1);
		print("tprof %s\n", buf);
		execl("/bin/tprof", "tprof", buf, nil);
		abort();
	default:
		free(wait());
		break;
	}
}

#define min(a,b)	((a)<(b)?(a):(b))
#define max(a,b)	((a)>(b)?(a):(b))
Rectangle
getbbox(Rectangle *sr, Matrix m)
{
	Point2 p0, p1, p2, p3;

	p0 = Pt2(sr->min.x, sr->min.y, 1);
	p0 = xform(p0, m);
	p1 = Pt2(sr->max.x, sr->min.y, 1);
	p1 = xform(p1, m);
	p2 = Pt2(sr->min.x, sr->max.y, 1);
	p2 = xform(p2, m);
	p3 = Pt2(sr->max.x, sr->max.y, 1);
	p3 = xform(p3, m);

	p0.x = min(min(min(p0.x, p1.x), p2.x), p3.x);
	p0.y = min(min(min(p0.y, p1.y), p2.y), p3.y);
	p1.x = max(max(max(p1.x, p1.x), p2.x), p3.x);
	p1.y = max(max(max(p1.y, p1.y), p2.y), p3.y);
	return Rect(p0.x, p0.y, p1.x, p1.y);
}

void
usage(void)
{
	fprint(2, "usage: %s [-s sx sy] [-t tx ty] [-r θ]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Memimage *dst, *src;
	Warp w;
	Rectangle dr;
	double sx, sy, tx, ty, θ, c, s;

	sx = sy = 1;
	tx = ty = 0;
	θ = 0;
	ARGBEGIN{
	case 's':
		sx = strtod(EARGF(usage()), nil);
		sy = strtod(EARGF(usage()), nil);
		break;
	case 't':
		tx = strtod(EARGF(usage()), nil);
		ty = strtod(EARGF(usage()), nil);
		break;
	case 'r':
		θ = strtod(EARGF(usage()), nil)*DEG;
		break;
	default:
		usage();
	}ARGEND;
	if(argc != 0)
		usage();

	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");

	src = readmemimage(0);
	c = cos(θ);
	s = sin(θ);
	Matrix S = {
		sx, 0, 0,
		0, sy, 0,
		0, 0, 1,
	}, T = {
		1, 0, tx,
		0, 1, ty,
		0, 0, 1,
	}, R = {
		c, -s, 0,
		s, c, 0,
		0, 0, 1,
	};

	mulm(S, R);
	mulm(T, S);

//	dr = getbbox(&src->r, T);
	dr = src->r;
	dst = allocmemimage(dr, src->chan);

	mkwarp(w, T);

	profbegin();
	if(memaffinewarp(dst, dst->r, src, src->r.min, w) < 0)
		sysfatal("memaffinewarp: %r");
	profend();
	writememimage(1, dst);

	exits(nil);
}
