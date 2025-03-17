#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mach.h>

typedef struct Data	Data;
typedef struct Pc	Pc;
typedef struct Acc	Acc;

struct Data
{
	ushort	down;
	ushort	right;
	ulong	pc;
	ulong	count;
	uvlong	time;
};
enum { Datasz = 2+2+4+4+8 };

struct Pc
{
	Pc	*next;
	ulong	pc;
};

struct Acc
{
	char	*name;
	ulong	pc;
	uvlong	ticks;
	uvlong	calls;
};

Data*	data;
Acc*	acc;
uvlong	ticks;
long	nsym;
long	ndata;
int	dflag;
int	rflag;
Biobuf	bout;
int	tabstop = 4;
int	verbose;
uvlong	cyclefreq;

void	syms(char*);
void	datas(char*);
void	graph(int, ulong, Pc*);
void	plot(void);
char*	name(ulong);
void	indent(int);
char*	defaout(void);

void
main(int argc, char *argv[])
{
	char *s;

	s = getenv("tabstop");
	if(s!=nil && strtol(s,0,0)>0)
		tabstop = strtol(s,0,0);
	ARGBEGIN{
	case 'v':
		verbose = 1;
		break;
	case 'd':
		dflag = 1;
		break;
	case 'r':
		rflag = 1;
		break;
	default:
	usage:
		fprint(2, "usage: prof [-dr] [8.out] [prof.out]\n");
		exits("usage");
	}ARGEND
	Binit(&bout, 1, OWRITE);
	if(argc < 2)
		goto usage;
	syms(argv[0]);
	datas(argv[1]);
	if(ndata){
		if(dflag)
			graph(0, data[0].down, 0);
		else
			plot();
	}
	exits(0);
}

int
acmp(void *va, void *vb)
{
	Acc *a, *b;
	uvlong ua, ub;

	a = va;
	b = vb;
	ua = a->ticks;
	ub = b->ticks;

	if(ua > ub)
		return 1;
	if(ua < ub)
		return -1;
	return 0;
}

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

void
graph(int ind, ulong i, Pc *pc)
{
	long count, prgm;
	vlong time;
	Pc lpc;

	if(i >= ndata){
		fprint(2, "prof: index out of range %ld [max %ld]\n", i, ndata);
		return;
	}
	count = data[i].count;
	time = data[i].time;
	prgm = data[i].pc;
	if(time < 0)
		time += data[0].time;
	if(data[i].right != 0xFFFF)
		graph(ind, data[i].right, pc);
	indent(ind);
	if(count == 1)
		Bprint(&bout, "%s:%.9f\n", name(prgm), (double)time/cyclefreq);
	else
		Bprint(&bout, "%s:%.9f/%lud\n", name(prgm), (double)time/cyclefreq, count);
	if(data[i].down == 0xFFFF)
		return;
	lpc.next = pc;
	lpc.pc = prgm;
	if(!rflag){
		while(pc){
			if(pc->pc == prgm){
				indent(ind+1);
				Bprint(&bout, "...\n");
				return;
			}
			pc = pc->next;
		}
	}
	graph(ind+1, data[i].down, &lpc);
}
/*
 *	assume acc is ordered by increasing text address.
 */
long
symind(ulong pc)
{
	int top, bot, mid;

	bot = 0;
	top = nsym;
	for (mid = (bot+top)/2; mid < top; mid = (bot+top)/2) {
		if (pc < acc[mid].pc)
			top = mid;
		else
		if (mid != nsym-1 && pc >= acc[mid+1].pc)
			bot = mid;
		else
			return mid;
	}
	return -1;
}

double
sum(ulong i)
{
	long j;
	vlong dtime, time;
	int k;
	static indent;

	if(i >= ndata){
		fprint(2, "prof: index out of range %ld [max %ld]\n", i, ndata);
		return 0;
	}
	j = symind(data[i].pc);
	time = data[i].time;
	if(time < 0)
		time += data[0].time;
	if (verbose){
		for(k = 0; k < indent; k++)
			print("	");
		print("%lud: %llud/%.9f/%lud", i, data[i].time, (double)data[i].time/cyclefreq, data[i].count);
		if (j >= 0)
			print("	%s\n", acc[j].name);
		else
			print("	0x%lux\n", data[i].pc);
	}
	dtime = 0;
	if(data[i].down != 0xFFFF){
		indent++;
		dtime = sum(data[i].down);
		indent--;
	}
	j = symind(data[i].pc);
	if (j >= 0) {
		acc[j].ticks += time - dtime;
		ticks += time - dtime;
		acc[j].calls += data[i].count;
	}
	if(data[i].right == 0xFFFF)
		return time;
	return time + sum(data[i].right);
}

void
plot(void)
{
	Symbol s;

	for (nsym = 0; textsym(&s, nsym); nsym++) {
		acc = realloc(acc, (nsym+1)*sizeof(Acc));
		if(acc == 0){
			fprint(2, "prof: malloc fail\n");
			exits("acc malloc");
		}
		acc[nsym].name = s.name;
		acc[nsym].pc = s.value;
		acc[nsym].calls = 0;
		acc[nsym].ticks = 0;
	}
	sum(data[0].down);
	qsort(acc, nsym, sizeof(Acc), acmp);
	Bprint(&bout, "  %%       Time        Calls  Name\n");
	if(ticks == 0)
		ticks = 1;
	while (--nsym >= 0) {
		if(acc[nsym].calls)
			Bprint(&bout, "%4.1f   %.9f %8llud\t%s\n",
				(100.0*acc[nsym].ticks)/ticks,
				(double)acc[nsym].ticks/cyclefreq,
				acc[nsym].calls,
				acc[nsym].name);
	}
}

void
indent(int ind)
{
	int j;

	j = 2*ind;
	while(j >= tabstop){
		Bwrite(&bout, ".\t", 2);
		j -= tabstop;
	}
	if(j)
		Bwrite(&bout, ".                            ", j);
}
