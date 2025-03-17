#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <bio.h>
#include <mach.h>
#include <plumb.h>

typedef struct Data	Data;

struct Data
{
	ushort	down;
	ushort	right;
	ulong	pc;
	ulong	count;
	uvlong	time;
	uint 	rec;
};
enum { Datasz = 2+2+4+4+8 };

Data*	data;
long	ndata;
uvlong	cyclefreq;

void
syms(char *cout)
{
	Fhdr f;
	int fd;

	if((fd = open(cout, 0)) < 0)
		sysfatal("%r");
	if(!crackhdr(fd, &f))
		sysfatal("can't read text file header: %r");
	if(f.type == FNONE)
		sysfatal("text file not an a.out");
	if(syminit(fd, &f) < 0)
		sysfatal("syminit: %r");
	close(fd);
}

#define GET2(p) (u16int)(p)[1] | (u16int)(p)[0]<<8
#define GET4(p) (u32int)(p)[3] | (u32int)(p)[2]<<8 | (u32int)(p)[1]<<16 | (u32int)(p)[0]<<24
#define GET8(p) (u64int)(p)[7] | (u64int)(p)[6]<<8 | (u64int)(p)[5]<<16 | (u64int)(p)[4]<<24 | \
		(u64int)(p)[3]<<32 | (u64int)(p)[2]<<40 | (u64int)(p)[1]<<48 | (u64int)(p)[0]<<56

void
datas(char *dout)
{
	int fd;
	Dir *d;
	int i;
	uchar hdr[3+1+8], *buf, *p;

	if((fd = open(dout, 0)) < 0){
		perror(dout);
		exits("open");
	}
	d = dirfstat(fd);
	if(d == nil){
		perror(dout);
		exits("stat");
	}
	d->length -= sizeof hdr;
	ndata = d->length/Datasz;
	data = malloc(ndata*sizeof(Data));
	buf = malloc(d->length);
	if(buf == 0 || data == 0)
		sysfatal("malloc");
	if(read(fd, hdr, sizeof hdr) != sizeof hdr)
		sysfatal("read data header: %r");
	if(memcmp(hdr, "pr\x0f", 3) != 0)
		sysfatal("bad magic");
	cyclefreq = GET8(hdr+4);
	if(readn(fd, buf, d->length) != d->length)
		sysfatal("data file read: %r");
	for(p = buf, i = 0; i < ndata; i++){
		data[i].down = GET2(p); p += 2;
		data[i].right = GET2(p); p += 2;
		data[i].pc = GET4(p); p += 4;
		data[i].count = GET4(p); p += 4;
		data[i].time = GET8(p); p += 8;
	}
	free(buf);
	free(d);
	close(fd);
}

char*
name(ulong pc)
{
	Symbol s;
	static char buf[16];

	if (findsym(pc, CTEXT, &s))
		return(s.name);
	snprint(buf, sizeof(buf), "#%lux", pc);
	return buf;
}

static void
plumbpc(ulong pc)
{
	Symbol s;
	char buf[256], wd[256];
	int fd;

	if(!findsym(pc, CTEXT, &s))
		return;
	
	fileline(buf, sizeof buf, pc);
	fd = plumbopen("send", OWRITE);
	if(fd < 0)
		return;
	getwd(wd, sizeof wd);
	plumbsendtext(fd, "flambe", "edit", wd, buf);
	close(fd);
}

int rowh;
Image **cols;
int ncols;
uvlong total;
Rectangle *clicks;

void
gencols(void)
{
	int i, h;
	ulong col, step;

	h = Dy(screen->r) / rowh + 1;
	if(ncols == h)
		return;
	for(i = 0; i < ncols; i++)
		freeimage(cols[i]);
	free(cols);

	ncols = h;
	cols = malloc(sizeof(Image*)*ncols);
	col = DRed;
	step = (0xFF/ncols)<<16;
	for(i = 0; i < ncols; i++){
		cols[i] = allocimagemix(display, DWhite, col);
		col += step;
	}
}

Image*
colfor(int h)
{
	if(h % 2 == 0)
		h += (ncols/3);
	return cols[h % ncols];
}

void
onhover(int i)
{
	Rectangle r;
	char buf[128];

	r = screen->r;
	r.max.y = r.min.y + (font->height+2)*2;
	draw(screen, r, display->white, nil, ZP);
	string(screen, r.min, display->black, ZP, font, name(data[i].pc));

	r.min.y += font->height + 1;
	snprint(buf, sizeof buf, "Time: %.8f(s) %.2f%%, Calls: %lud", (double)data[i].time/cyclefreq, (double)data[i].time/total * 100, data[i].count);
	string(screen, r.min, display->black, ZP, font, buf);
	flushimage(display, 1);
}

void
graph(int i, int x, int h)
{
	Rectangle r, r2;

	if(i >= ndata)
		sysfatal("corrupted profile data");
	r.min = (Point){x, screen->r.max.y - rowh*h};
	r.max = (Point){
		x + ((double)data[i].time * Dx(screen->r)) / total,
		r.min.y + rowh
	};
	clicks[i] = r;
	if(Dx(r) > 6){
		draw(screen, r, colfor(h), nil, ZP);
		r2 = r;
		r2.min.x  = r2.max.x - 2;
		draw(screen, r2, display->black, nil, ZP);
		screen->clipr = r;
		r2.min = r.min;
		r2.min.x += 4;
		string(screen, r2.min, display->black, ZP, font, name(data[i].pc));
		screen->clipr = screen->r;
	}
	if(data[i].right != 0xFFFF)
		graph(data[i].right, r.max.x, h);
	if(data[i].down != 0xFFFF)
		graph(data[i].down, x, h + 1);
}

void
redraw(int i)
{
	total = data[i].time;
	memset(clicks, 0, ndata * sizeof(Rectangle));
	gencols();
	draw(screen, screen->r, display->white, nil, ZP);
	graph(i, screen->r.min.x, 1);
	flushimage(display, 1);
}

enum
{
	Ckey,
	Cmouse,
	Cresize,
	Numchan,
};

void
usage(void)
{
	fprint(2, "%s: $O.out prof\n", argv0);
	exits("usage");
}

/* syminit has a Biobuf on the stack */
mainstacksize = 64*1024;

void
threadmain(int argc, char **argv)
{
	Mousectl *mctl;
	Keyboardctl *kctl;
	Mouse m;
	Rune r;
	vlong t;
	int i;
	Alt a[Numchan+1] = {
		[Ckey] = {nil, &r, CHANRCV},
		[Cmouse] = {nil, &m, CHANRCV },
		[Cresize] = {nil, nil, CHANRCV},
		{nil, nil, CHANEND},
	};

	ARGBEGIN{
	default:
		usage();
		break;
	}ARGEND;
	if(argc != 2)
		usage();
	syms(argv[0]);
	datas(argv[1]);
	for(int i = 1; i < ndata; i++){
		t = data[i].time;
		if(t < 0)
			data[i].time =  t + data[0].time;
	}

	if(initdraw(nil, nil, "flambe") < 0)
		sysfatal("initdraw: %r");
	if((kctl = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");
	a[Ckey].c = kctl->c;
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	a[Cmouse].c = mctl->c;
	a[Cresize].c = mctl->resizec;

	rowh = font->height + 2;
	clicks = malloc(sizeof(Rectangle)*ndata);

	redraw(1);
	for(;;)
	switch(alt(a)){
		case Ckey:
			switch(r){
				case Kesc:
					redraw(1);
					break;
				case Kdel:
				case 'q':
					threadexitsall(nil);
				default:
					break;
			}
			break;
		case Cmouse:
			if(m.buttons == 16){
				redraw(1);
				break;
			}
			for(i = 0; i < ndata; i++){
				if(!ptinrect(m.xy, clicks[i]))
					continue;
				switch(m.buttons){
				case 0:
					onhover(i);
					break;
				case 1: case 8:
					redraw(i);
					break;
				case 4:
					plumbpc(data[i].pc);
					break;
				}
				break;
			}
			break;
		case Cresize:
			getwindow(display, Refnone);
			redraw(1);
			break;
	}
}
