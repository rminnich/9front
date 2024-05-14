#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mach.h>

/*
 * PowerPC-specific debugger interface,
 * including 64-bit modes
 *	forsyth@terzarima.net
 */

static	char	*powerexcep(Map*, Rgetter);
static	int	powerfoll(Map*, uvlong, Rgetter, uvlong*);
static	int	powerinst(Map*, uvlong, char, char*, int);
static	int	powerinstlen(Map*, uvlong);
static	int	powerdas(Map*, uvlong, char*, int);

/*
 *	Machine description
 */
Machdata powermach =
{
	{0x02, 0x8f, 0xff, 0xff},		/* break point */	/* BUG */
	4,			/* break point size */

	beswab,			/* short to local byte order */
	beswal,			/* long to local byte order */
	beswav,			/* vlong to local byte order */
	risctrace,		/* print C traceback */
	riscframe,		/* frame finder */
	powerexcep,		/* print exception */
	0,			/* breakpoint fixup */
	beieeesftos,		/* single precision float printer */
	beieeedftos,		/* double precisioin float printer */
	powerfoll,		/* following addresses */
	powerinst,		/* print instruction */
	powerdas,		/* dissembler */
	powerinstlen,		/* instruction size */
};

static char *excname[] =
{
	"reserved 0",
	"system reset",
	"machine check",
	"data access",
	"instruction access",
	"external interrupt",
	"alignment",
	"program exception",
	"floating-point unavailable",
	"decrementer",
	"i/o controller interface error",
	"reserved B",
	"system call",
	"trace trap",
	"floating point assist",
	"reserved",
	"ITLB miss",
	"DTLB load miss",
	"DTLB store miss",
	"instruction address breakpoint"
	"SMI interrupt"
	"reserved 15",
	"reserved 16",
	"reserved 17",
	"reserved 18",
	"reserved 19",
	"reserved 1A",
	/* the following are made up on a program exception */
	"floating point exception",		/* FPEXC */
	"illegal instruction",
	"privileged instruction",
	"trap",
	"illegal operation",
};

static char*
powerexcep(Map *map, Rgetter rget)
{
	long c;
	static char buf[32];

	c = (*rget)(map, "CAUSE") >> 8;
	if(c < nelem(excname))
		return excname[c];
	sprint(buf, "unknown trap #%lx", c);
	return buf;
}

/*
 * disassemble PowerPC opcodes
 */

#define	REGSP	1	/* should come from q.out.h, but there's a clash */
#define	REGSB	2

static	char FRAMENAME[] = ".frame";

static Map *mymap;

/*
 * ibm conventions for these: bit 0 is top bit
 *	from table 10-1
 */
typedef struct {
	uchar	aa;		/* bit 30 */
	uchar	crba;		/* bits 11-15 */
	uchar	crbb;		/* bits 16-20 */
	long	bd;		/* bits 16-29 */
	uchar	crfd;		/* bits 6-8 */
	uchar	crfs;		/* bits 11-13 */
	uchar	bi;		/* bits 11-15 */
	uchar	bo;		/* bits 6-10 */
	uchar	crbd;		/* bits 6-10 */
	union {
		short	d;	/* bits 16-31 */
		short	simm;
		ushort	uimm;
	};
	uchar	fm;		/* bits 7-14 */
	uchar	fra;		/* bits 11-15 */
	uchar	frb;		/* bits 16-20 */
	uchar	frc;		/* bits 21-25 */
	uchar	frs;		/* bits 6-10 */
	uchar	frd;		/* bits 6-10 */
	uchar	crm;		/* bits 12-19 */
	long	li;		/* bits 6-29 || b'00' */
	uchar	lk;		/* bit 31 */
	uchar	mb;		/* bits 21-25 */
	uchar	me;		/* bits 26-30 */
	uchar	xmbe;		/* bits 26,21-25: mb[5] || mb[0:4], also xme */
	uchar	xsh;		/* bits 30,16-20: sh[5] || sh[0:4] */
	uchar	nb;		/* bits 16-20 */
	uchar	op;		/* bits 0-5 */
	uchar	oe;		/* bit 21 */
	uchar	ra;		/* bits 11-15 */
	uchar	rb;		/* bits 16-20 */
	uchar	rc;		/* bit 31 */
	union {
		uchar	rs;	/* bits 6-10 */
		uchar	rd;
	};
	uchar	sh;		/* bits 16-20 */
	ushort	spr;		/* bits 11-20 */
	uchar	to;		/* bits 6-10 */
	uchar	imm;		/* bits 16-19 */
	ushort	xo;		/* bits 21-30, 22-30, 26-30, or 30 (beware) */
	uvlong	imm64;
	long	w[5];		/* full context of a combined pseudo instruction */
	uchar	pop;		/* op of second half of prefixed instruction */
	uvlong	addr;		/* pc of instruction */
	short	target;
	short	m64;		/* 64-bit mode */
	char	*curr;		/* current fill level in output buffer */
	char	*end;		/* end of buffer */
	int 	size;		/* number of longs in instr */
	char	*err;		/* errmsg */
} Instr;

#define	IBF(v,a,b) (((ulong)(v)>>(32-(b)-1)) & ~(~0L<<(((b)-(a)+1))))
#define	IB(v,b) IBF((v),(b),(b))

#pragma	varargck	argpos	bprint		2

static void
bprint(Instr *i, char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	i->curr = vseprint(i->curr, i->end, fmt, arg);
	va_end(arg);
}

static int
decode(uvlong pc, Instr *i)
{
	ulong w;

	if (get4(mymap, pc, &w) < 0) {
		werrstr("can't read instruction: %r");
		return -1;
	}
	i->m64 = asstype == APOWER64;
	i->aa = IB(w, 30);
	i->crba = IBF(w, 11, 15);
	i->crbb = IBF(w, 16, 20);
	i->bd = IBF(w, 16, 29)<<2;
	if(i->bd & 0x8000)
		i->bd |= ~0L<<16;
	i->crfd = IBF(w, 6, 8);
	i->crfs = IBF(w, 11, 13);
	i->bi = IBF(w, 11, 15);
	i->bo = IBF(w, 6, 10);
	i->crbd = IBF(w, 6, 10);
	i->uimm = IBF(w, 16, 31);	/* also d, simm */
	i->fm = IBF(w, 7, 14);
	i->fra = IBF(w, 11, 15);
	i->frb = IBF(w, 16, 20);
	i->frc = IBF(w, 21, 25);
	i->frs = IBF(w, 6, 10);
	i->frd = IBF(w, 6, 10);
	i->crm = IBF(w, 12, 19);
	i->li = IBF(w, 6, 29)<<2;
	if(IB(w, 6))
		i->li |= ~0<<25;
	i->lk = IB(w, 31);
	i->mb = IBF(w, 21, 25);
	i->me = IBF(w, 26, 30);
	i->xmbe = (IB(w,26)<<5) | i->mb;
	i->nb = IBF(w, 16, 20);
	i->op = IBF(w, 0, 5);
	i->oe = IB(w, 21);
	i->ra = IBF(w, 11, 15);
	i->rb = IBF(w, 16, 20);
	i->rc = IB(w, 31);
	i->rs = IBF(w, 6, 10);	/* also rd */
	i->sh = IBF(w, 16, 20);
	i->xsh = (IB(w, 30)<<5) | i->sh;
	i->spr = IBF(w, 11, 20);
	i->to = IBF(w, 6, 10);
	i->imm = IBF(w, 16, 19);
	i->xo = IBF(w, 21, 30);		/* bits 21-30, 22-30, 26-30, or 30 (beware) */
	if(i->op == 58){	/* class of 64-bit loads */
		i->xo = i->simm & 3;
		i->simm &= ~3;
	} else if(i->op == 4)
		i->xo = IBF(w, 21, 31);
	else if(i->op == 6)
		i->xo = IBF(w, 28, 31);
	else if(i->op == 57)
		i->xo = IBF(w, 30, 31);
	else if(i->op == 61)
		i->xo = IBF(w, 29, 31);
	else if(i->op == 17)
		i->xo = IBF(w, 30, 31);
	i->imm64 = i->simm;
	if(i->op == 15)
		i->imm64 <<= 16;
	else if(i->op == 25 || i->op == 27 || i->op == 29)
		i->imm64 = (uvlong)(i->uimm<<16);
	i->w[0] = w;
	i->target = -1;
	i->addr = pc;
	i->size = 1;
	return 1;
}

static int
mkinstr(uvlong pc, Instr *i)
{
	Instr x;
	Instr sf[3];
	ulong w;
	int j;

	if(decode(pc, i) < 0)
		return -1;
	/*
	 * Linker has to break out larger constants into multiple instructions.
	 * Combine them back together into one MOV.
	 * 15 is addis, 25 is oris, 24 is ori.
	 */
	if((i->op == 15 && i->ra == 0) || (i->op == 25 && i->rs == 0) || (i->op == 24 && i->rs == 0)) {
		if(decode(pc+4, &x) < 0)
			return 1;

		/* very specific worst case 64 bit load */
		if(x.rd == 31 && (x.op == 15 && x.ra == 0) || (x.op == 25 && x.rs == 0)){
			for(j = 0; j < nelem(sf); j++)
				if(decode(pc + 4*(j+2), sf+j) < 0)
					goto Next;
			if(sf[0].op == 24 && sf[0].rs == sf[0].ra && sf[0].ra == i->rd)
			if(sf[1].op == 24 && sf[1].rs == sf[1].ra && sf[1].ra == 31)
			if(sf[2].ra == (i->rs == 0 ? i->ra : i ->rs))
			if(sf[2].op == 30 && IBF(sf[2].w[0], 27, 30) == 7)
			if(sf[2].xsh == 32 && IBF(sf[2].w[0], 21, 26) == 0){
				i->size = 5;
				i->imm64 = (i->imm64&0xFFFF0000) | ((sf[0].imm64&0xFFFF));
				i->imm64 |= ((x.imm64&0xFFFF0000)<<32) | ((sf[1].imm64&0xFFFF)<<32);
			}
			return 1;
		}

Next:
		if(i->op != 24 && x.op == 24 && x.rs == x.ra && x.ra == i->rd) {
			i->imm64 |= (x.imm64 & 0xFFFF);
			if(i->op != 15)
				i->imm64 &= 0xFFFFFFFFUL;
			i->w[1] = x.w[0];
			i->target = x.rd;
			i->size++;
			if(decode(pc+8, &x) < 0)
				return 1;
		}

		/* 64 bit constant mov with lower 32 zero */
		if(x.ra == (i->rs == 0 ? i->ra : i ->rs))
		if(x.op == 30 && IBF(x.w[0], 27, 30) == 7)
		if(x.xsh == 32 && IBF(x.w[0], 21, 26) == 0){
			i->imm64 <<= 32;
			if(i->size == 2)
				i->w[2] = x.w[0];
			else
				i->w[1] = x.w[0];
			i->size++;
		}
		return 1;
	}

	/* ISA3.1+ prefixed instructions */
	if(i->op == 1){
		if(get4(mymap, pc+4, &w) < 0)
			return -1;
		i->w[1] = w;
		i->pop = IBF(i->w[1], 0, 5);
		i->size++;
	}
	return 1;
}

static int
plocal(Instr *i)
{
	long offset;
	Symbol s;

	if (!findsym(i->addr, CTEXT, &s) || !findlocal(&s, FRAMENAME, &s))
		return -1;
	offset = s.value - i->imm64;
	if (offset > 0) {
		if(getauto(&s, offset, CAUTO, &s)) {
			bprint(i, "%s+%lld(SP)", s.name, s.value);
			return 1;
		}
	} else {
		if (getauto(&s, -offset-4, CPARAM, &s)) {
			bprint(i, "%s+%ld(FP)", s.name, -offset);
			return 1;
		}
	}
	return -1;
}

static int
pglobal(Instr *i, uvlong off, int anyoff, char *reg)
{
	Symbol s, s2;
	uvlong off1;

	if(findsym(off, CANY, &s) &&
	   off-s.value < 4096 &&
	   (s.class == CDATA || s.class == CTEXT)) {
		if(off==s.value && s.name[0]=='$'){
			off1 = 0;
			geta(mymap, s.value, &off1);
			if(off1 && findsym(off1, CANY, &s2) && s2.value == off1){
				bprint(i, "$%s%s", s2.name, reg);
				return 1;
			}
		}
		bprint(i, "%s", s.name);
		if (s.value != off)
			bprint(i, "+%llux", off-s.value);
		bprint(i, reg);
		return 1;
	}
	if(!anyoff)
		return 0;
	bprint(i, "%llux%s", off, reg);
	return 1;
}

static void
address(Instr *i)
{
	if (i->ra == REGSP && plocal(i) >= 0)
		return;
	if (i->ra == REGSB && mach->sb && pglobal(i, mach->sb+i->imm64, 0, "(SB)"))
		return;
	if(i->simm < 0)
		bprint(i, "-%x(R%d)", -i->simm, i->ra);
	else
		bprint(i, "%llux(R%d)", i->imm64, i->ra);
}

static	char	*tcrbits[] = {"LT", "GT", "EQ", "VS"};
static	char	*fcrbits[] = {"GE", "LE", "NE", "VC"};

typedef struct Opcode Opcode;

struct Opcode {
	uchar	op;
	ushort	xo;
	ushort	xomask;
	char	*mnemonic;
	void	(*f)(Opcode *, Instr *);
	char	*ken;
	int	flags;
};

static void format(char *, Instr *, char *);

static void
branch(Opcode *o, Instr *i)
{
	char buf[8];
	int bo, bi;

	bo = i->bo & ~1;	/* ignore prediction bit */
	if(bo==4 || bo==12 || bo==20) {	/* simple forms */
		if(bo != 20) {
			bi = i->bi&3;
			sprint(buf, "B%s%%L", bo==12? tcrbits[bi]: fcrbits[bi]);
			format(buf, i, nil);
			bprint(i, "\t");
			if(i->bi > 4)
				bprint(i, "CR(%d),", i->bi/4);
		} else
			format("BR%L\t", i, nil);
		if(i->op == 16)
			format(0, i, "%J");
		else if(i->op == 19 && i->xo == 528)
			format(0, i, "(CTR)");
		else if(i->op == 19 && i->xo == 16)
			format(0, i, "(LR)");
	} else
		format(o->mnemonic, i, o->ken);
}

static void
addi(Opcode *o, Instr *i)
{
	if (i->op==14 && i->ra == 0)
		format("MOV%N", i, "%i,R%d");
	else if (i->ra == REGSB) {
		format("MOV%N\t$", i, nil);
		address(i);
		bprint(i, ",R%d", i->rd);
	} else if(i->op==14 && i->simm < 0) {
		bprint(i, "SUB\t$%d,R%d", -i->simm, i->ra);
		if(i->rd != i->ra)
			bprint(i, ",R%d", i->rd);
	} else if(i->ra == i->rd) {
		format(o->mnemonic, i, "%i");
		bprint(i, ",R%d", i->rd);
	} else
		format(o->mnemonic, i, o->ken);
}

static void
addis(Opcode *o, Instr *i)
{
	vlong v;

	v = i->imm64;
	if (i->op==15 && i->ra == 0){
		format("MOV%N\t", i, nil);
		bprint(i, "$%llux,R%d", v, i->rd);
	} else if (i->op==15 && i->ra == REGSB) {
		format("MOV%N\t$", i, nil);
		address(i);
		bprint(i, ",R%d", i->rd);
	} else if(i->op==15 && v < 0) {
		bprint(i, "SUB\t$%lld,R%d", -v, i->ra);
		if(i->rd != i->ra)
			bprint(i, ",R%d", i->rd);
	} else {
		format(o->mnemonic, i, nil);
		bprint(i, "\t$%lld,R%d", v, i->ra);
		if(i->rd != i->ra)
			bprint(i, ",R%d", i->rd);
	}
}

static void
andi(Opcode *o, Instr *i)
{
	if (i->ra == i->rs)
		format(o->mnemonic, i, "%I,R%d");
	else
		format(o->mnemonic, i, o->ken);
}

static void
gencc(Opcode *o, Instr *i)
{
	format(o->mnemonic, i, o->ken);
}

static void
gen(Opcode *o, Instr *i)
{
	format(o->mnemonic, i, o->ken);
	if (i->rc)
		bprint(i, " [illegal Rc]");
}

static void
ldx(Opcode *o, Instr *i)
{
	if(i->ra == 0)
		format(o->mnemonic, i, "(R%b),R%d");
	else
		format(o->mnemonic, i, "(R%b+R%a),R%d");
	if(i->rc)
		bprint(i, " [illegal Rc]");
}

static void
stx(Opcode *o, Instr *i)
{
	if(i->ra == 0)
		format(o->mnemonic, i, "R%d,(R%b)");
	else
		format(o->mnemonic, i, "R%d,(R%b+R%a)");
	if(i->rc && (i->xo != 150 && i->xo != 214))
		bprint(i, " [illegal Rc]");
}

static void
fldx(Opcode *o, Instr *i)
{
	if(i->ra == 0)
		format(o->mnemonic, i, "(R%b),F%d");
	else
		format(o->mnemonic, i, "(R%b+R%a),F%d");
	if(i->rc)
		bprint(i, " [illegal Rc]");
}

static void
fstx(Opcode *o, Instr *i)
{
	if(i->ra == 0)
		format(o->mnemonic, i, "F%d,(R%b)");
	else
		format(o->mnemonic, i, "F%d,(R%b+R%a)");
	if(i->rc)
		bprint(i, " [illegal Rc]");
}

static void
vldx(Opcode *o, Instr *i)
{
	if(i->ra == 0)
		format(o->mnemonic, i, "(R%b),V%d");
	else
		format(o->mnemonic, i, "(R%b+R%a),V%d");
	if(i->rc)
		bprint(i, " [illegal Rc]");
}

static void
vstx(Opcode *o, Instr *i)
{
	if(i->ra == 0)
		format(o->mnemonic, i, "V%d,(R%b)");
	else
		format(o->mnemonic, i, "V%d,(R%b+R%a)");
	if(i->rc)
		bprint(i, " [illegal Rc]");
}

static void
dcb(Opcode *o, Instr *i)
{
	if(i->ra == 0)
		format(o->mnemonic, i, "(R%b)");
	else
		format(o->mnemonic, i, "(R%b+R%a)");
	if(i->rd)
		bprint(i, " [illegal Rd]");
	if(i->rc)
		bprint(i, " [illegal Rc]");
}

static void
lw(Opcode *o, Instr *i, char r)
{
	format(o->mnemonic, i, nil);
	bprint(i, "\t");
	address(i);
	bprint(i, ",%c%d", r, i->rd);
}

static void
load(Opcode *o, Instr *i)
{
	lw(o, i, 'R');
}

static void
fload(Opcode *o, Instr *i)
{
	lw(o, i, 'F');
}

static void
sw(Opcode *o, Instr *i, char r)
{
	int offset;
	Symbol s;

	if (i->rs == REGSP) {
		if (findsym(i->addr, CTEXT, &s) && findlocal(&s, FRAMENAME, &s)) {
			offset = s.value-i->imm64;
			if (offset > 0 && getauto(&s, offset, CAUTO, &s)) {
				format(o->mnemonic, i, nil);
				bprint(i, "\t%c%d,%s-%d(SP)", r, i->rd, s.name, offset);
				return;
			}
		}
	}
	if (i->rs == REGSB && mach->sb) {
		format(o->mnemonic, i, nil);
		bprint(i, "\t%c%d,", r, i->rd);
		address(i);
		return;
	}
	if (r == 'F')
		format(o->mnemonic, i, "F%d,%l");
	else
		format(o->mnemonic, i, o->ken);
}

static void
store(Opcode *o, Instr *i)
{
	sw(o, i, 'R');
}

static void
fstore(Opcode *o, Instr *i)
{
	sw(o, i, 'F');
}

static void
shifti(Opcode *o, Instr *i)
{
	if (i->ra == i->rs)
		format(o->mnemonic, i, "$%k,R%a");
	else
		format(o->mnemonic, i, o->ken);
}

static void
shift(Opcode *o, Instr *i)
{
	if (i->ra == i->rs)
		format(o->mnemonic, i, "R%b,R%a");
	else
		format(o->mnemonic, i, o->ken);
}

static void
add(Opcode *o, Instr *i)
{
	if (i->rd == i->ra)
		format(o->mnemonic, i, "R%b,R%d");
	else if (i->rd == i->rb)
		format(o->mnemonic, i, "R%a,R%d");
	else
		format(o->mnemonic, i, o->ken);
}

static void
sub(Opcode *o, Instr *i)
{
	format(o->mnemonic, i, nil);
	bprint(i, "\t");
	if(i->op == 31) {
		bprint(i, "\tR%d,R%d", i->ra, i->rb);	/* subtract Ra from Rb */
		if(i->rd != i->rb)
			bprint(i, ",R%d", i->rd);
	} else
		bprint(i, "\tR%d,$%d,R%d", i->ra, i->simm, i->rd);
}

static void
qdiv(Opcode *o, Instr *i)
{
	format(o->mnemonic, i, nil);
	if(i->op == 31)
		bprint(i, "\tR%d,R%d", i->rb, i->ra);
	else
		bprint(i, "\t$%d,R%d", i->simm, i->ra);
	if(i->ra != i->rd)
		bprint(i, ",R%d", i->rd);
}

static void
and(Opcode *o, Instr *i)
{
	if (i->op == 31) {
		/* Rb,Rs,Ra */
		if (i->ra == i->rs)
			format(o->mnemonic, i, "R%b,R%a");
		else if (i->ra == i->rb)
			format(o->mnemonic, i, "R%s,R%a");
		else
			format(o->mnemonic, i, o->ken);
	} else {
		/* imm,Rs,Ra */
		if (i->ra == i->rs)
			format(o->mnemonic, i, "%I,R%a");
		else
			format(o->mnemonic, i, o->ken);
	}
}

static void
or(Opcode *o, Instr *i)
{
	if (i->op == 31) {
		/* Rb,Rs,Ra */
		if (i->rs == 0 && i->ra == 0 && i->rb == 0)
			format("NOP", i, nil);
		else if (i->rs == i->rb)
			format("MOV%N", i, "R%b,R%a");
		else
			and(o, i);
	} else if(i->op == 24 && i->rs == 0)
		format("MOV%N", i, "$%B,R%a");
	else
		and(o, i);
}

static void
shifted(Opcode *o, Instr *i)
{
	if (i->op == 25 && i->rs == 0){
		format("MOV%N\t", i, nil);
		bprint(i, "$%llux,R%d", i->imm64, i->ra);
		return;
	}
	format(o->mnemonic, i, nil);
	bprint(i, "\t$%llux,", i->imm64);
	if (i->rs == i->ra)
		bprint(i, "R%d", i->ra);
	else
		bprint(i, "R%d,R%d", i->rs, i->ra);
}

static void
neg(Opcode *o, Instr *i)
{
	if (i->rd == i->ra)
		format(o->mnemonic, i, "R%d");
	else
		format(o->mnemonic, i, o->ken);
}

static void
vra2s(Opcode *o, Instr *i)
{
	char **p;
	int n;

	static char* bcdc[] = {
		[0] "bcdctsq.",
		[2] "bcdcfsq.",
		[4] "bcdctz.",
		[5] "bcdctn.",
		[6] "bcdcfz.",
		[7] "bcdcfn.",
		[31] "bcdsetsgn.",
	};
	static char* vcz[] = {
		[0]  "vclzlsbb",
		[1] "vctzlsbb",
		[6] "vnegw",
		[7] "vnegd",
		[8] "vprtybw",
		[9] "vprtybd",
		[10] "vprtybq",
		[31] "vctzd",
		[16] "vextsb2w",
		[17] "vextsh2w",
		[24] "vextsb2d",
		[25] "vextsh2d",
		[26] "vextsw2d",
		[27] "vextsd2q",
		[28] "vctzb",
		[29] "vctzh",
		[30] "vctzw",
	};
	static char* exp[] = {
		[1] "vexpandhm",
		[2] "vexpandwm",
		[3] "vexpanddm",
		[4] "vexpandqm",
		[8] "vextractbm",
		[9] "vextracthm",
		[10] "vextractwm",
		[11] "vextractdm",
		[12] "vextractqm",
		[16] "mtvsrbm",
		[17] "mtvsrhm",
		[18] "mtvsrwm",
		[19] "mtvsrdm",
		[20] "mtvsrqm",
		[24] "vcntmbb",
		[26] "vcntmbd",
		[28] "vcntmbh",
		[30] "vcntmbw",
	};
	static char *vstr[] = {
		[1] "vstribr[.]",
		[2] "vstrihl[.]",
		[3] "vstrihr[.]",
	};
	static char *xscv[] = {
		[1] "xscvqpuwz",
		[2] "xscvudqp",
		[3] "xscvuqqp",
		[8] "xscvqpsqz",
		[9] "xscvqpswz",
		[10] "xscvsdqp",
		[11] "xscvsqqp",
		[17] "xscvqpudz",
		[20] "xscvqpdp[o]",
		[22] "xscvdpqp",
		[25] "xscvqpsdz",
	};
	static char *xsab[] = {
		[2] "xsxexpqp",
		[8] "xsnabsqp",
		[16] "xsnegqp",
		[18] "xsxsigqp",
		[27] "xssqrtqp[o]",
	};
	static char *xvx[] = {
		[1] "xvxsigdp",
		[2] "xvtlsbb",
		[7] "xxbrh",
		[8] "xvxexpsp",
		[9] "xvxsigsp",
		[15] "xxbrw",
		[16] "xvcvbf16sp",
		[17] "xvcvspbf16",
		[23] "xxbrd",
		[24] "xvcvhpsp",
		[25] "xvcvsphp",
		[31] "xxbrq",
	};
	static char *xsc[] = {
		[1] "xsxsigdp",
		[16] "xscvhpdp",
		[17] "xscvdphp",
	};
	static char *xxcc[] = {
		[1] "xxmtacc",
		[3] "xxsetaccz",
	};
	static char *dcff[] = {
		[1] "dctfixqq",
	};
	static char *lxvk[] = {
		[31] "lxvkq",
	};
	if(o->op == 4 && (o->xo & o->xomask) == 385){
		p = bcdc; n = nelem(bcdc);
	} else switch(o->xo){
	case 1538: p = vcz; n = nelem(vcz); break;
	case 1602:
		if((i->ra & (12<<1)) == (12<<1))
			i->ra &= ~1;
		if((i->ra & (13<<1)) == (13<<1))
			i->ra &= ~1;
		p = exp; n = nelem(exp); break;
	case 836: p = xscv; n = nelem(xscv); break;
	case 804: p = xsab; n = nelem(xsab); break;
	case 950: p = xvx; n = nelem(xvx); break;
	case 13: p = vstr; n = nelem(vstr); break;
	case 694: p = xsc; n = nelem(xsc); break;
	case 177: p = xxcc; n = nelem(xxcc); break;
	case 994: p = dcff; n = nelem(dcff); break;
	case 360: p = lxvk; n = nelem(lxvk); break;
	default: p = nil; n = 0; break;
	}
		
	if(p == nil || i->ra > n || p[i->ra] == nil)
		format(o->mnemonic, i, o->ken);
	else
		format(p[i->ra], i, o->ken);
}

static void
addpcis(Opcode *o, Instr *i)
{
	long l;
	char buf[16];

	l = ((IBF(i->w[0], 11, 15)<<11) | (IBF(i->w[0], 16, 25)<<1) | (i->rc))<<16;
	snprint(buf, sizeof buf, "$%ld,PC,R%%d", l);
	format(o->mnemonic, i, buf);
}

static void
vsldbi(Opcode *o, Instr *i)
{
	switch(IBF(i->w[0], 21, 22)){
	case 1:
		format("vsrdbi", i, o->ken);
		break;
	case 0:
		format("vsldbi", i, o->ken);
		break;
	default:
		format("unknown instruction", i, 0);
	}
}

static void
sync(Opcode *o, Instr *i)
{
	switch(IBF(i->w[0], 9, 10)){
	case 0:
		format(o->mnemonic, i, o->ken);
		break;
	case 1:
		format("LWSYNC", i, o->ken);
		break;
	case 2:
		format("PTESYNC", i, o->ken);
		break;
	default:
		format("reserved instruction", i, 0);
	}
}

static	char	ir2[] = "R%a,R%d";		/* reverse of IBM order */
static	char	ir3[] = "R%b,R%a,R%d";
static	char	ir3r[] = "R%a,R%b,R%d";
static	char	il3[] = "R%b,R%s,R%a";
static	char	il2u[] = "%I,R%d,R%a";
static	char	il3s[] = "$%k,R%s,R%a";
static	char	il2[] = "R%s,R%a";
static	char	icmp3[] = "R%a,R%b,%D";
static	char	cr3op[] = "%b,%a,%d";
static	char	ir2i[] = "%i,R%a,R%d";
static	char	fp2[] = "F%b,F%d";
static	char	fp3[] = "F%b,F%a,F%d";
static	char	fp3c[] = "F%c,F%a,F%d";
static	char	fp4[] = "F%a,F%c,F%b,F%d";
static	char	fpcmp[] = "F%a,F%b,%D";
static	char	ldop[] = "%l,R%d";
static	char	stop[] = "R%d,%l";
static	char	fldop[] = "%l,F%d";
static	char	fstop[] = "F%d,%l";
static	char	rldc[] = "R%b,R%s,$%E,R%a";
static	char	rlim[] = "R%b,R%s,$%z,R%a";
static	char	rlimi[] = "$%k,R%s,$%z,R%a";
static	char	rldi[] = "$%e,R%s,$%E,R%a";
static	char	vr2[] = "V%a,V%d";
static	char	vr3[] = "V%b,V%a,V%d";

#define	OEM	IBF(~0,22,30)
#define	FP4	IBF(~0,26,30)
#define VXM	IBF(~0,21,31)
#define VXM2	IBF(~0,23,31)
#define VCM	IBF(~0,22,31)
#define DQM	IBF(~0,28,31)
#define VAM	IBF(~0,26,31)
#define	ALL	(~0)
#define	RLDC	0xF
#define	RLDI	0xE
/*
notes:
	10-26: crfD = rD>>2; rD&3 mbz
		also, L bit (bit 10) mbz or selects 64-bit operands
*/

typedef struct Popcode Popcode;

struct Popcode {
	ulong xo1;
	ulong xomask1;

	ulong op2;
	ulong xo2;
	ulong xomask2;

	char *mnemonic;
};

/* mask of bits [lo, hi] */
#define IBM(lo,hi)	( ((((~0UL)>>(31-hi))<<(31-hi))<<(lo))>>(lo) )

#define PLM	IBM(6,8)
#define XXM1	IBM(6,11)
#define XXM2	IBM(11,13)
#define XXM3	IBM(26,27)
#define XXM4	IBM(6,8)
#define XXM5	IBM(21,28)

#define PMXOP	(((3<<4)|9)<<20)

/* this is where they hide the x86 */
static Popcode popcodes[] = {
	{0,	PLM,	61,	0,	0,	"pstd"},
	{0,	PLM,	62,	0,	0,	"pstxvp"},
	{2<<24,	PLM,	32,	0,	0,	"plwz"},
	{1<<24,	XXM1,	32,	3<<17,	IBM(11,14),	"xxspltiw"},
	{1<<24,	XXM1,	32,	2<<17,	IBM(11,14),	"xxspltidp"},
	{1<<24,	XXM1,	32,	0,	XXM2,	"xxsplti32dx"},
	{1<<24,	XXM1,	33,	0,	XXM3,	"xxblendvb"},
	{1<<24,	XXM1,	33,	1<<4,	XXM3,	"xxblendvh"},
	{1<<24,	XXM1,	33,	2<<4,	XXM3,	"xxblendvw"},
	{1<<24,	XXM1,	33,	3<<4,	XXM3,	"xxblendvd"},
	{2<<24,	XXM4,	34,	0,	0,	"plbz"},
	{1<<24, XXM1,	34,	0,	XXM3,	"xxpermx"},
	{1<<24,	XXM1,	34,	1<<4,	XXM3,	"xxeval"},
	{2<<24,	XXM4,	36,	0,	0,	"pstw"},
	{2<<24,	XXM4,	38,	0,	0,	"pstb"},
	{2<<24,	XXM4,	40,	0,	0,	"plhz"},
	{0,	XXM4,	41,	0,	0,	"plwa"},
	{0,	XXM4,	42,	0,	0,	"plxsd"},
	{2<<24,	XXM4,	42,	0,	0,	"plha"},
	{0,	XXM4,	43,	0,	0,	"plxssp"},
	{2<<24,	XXM4,	44,	0,	0,	"psth"},
	{0,	XXM4,	46,	0,	0,	"pstxsd"},
	{0,	XXM4,	47,	0,	0,	"pstxssp"},
	{2<<24,	XXM4,	48,	0,	0,	"plfs"},
	{2<<24,	XXM4,	50,	0,	0,	"plfd"},
	{2<<24,	XXM4,	52,	0,	0,	"pstfs"},
	{2<<24,	XXM4,	54,	0,	0,	"pstfd"},
	{0,	XXM4,	56,	0,	0,	"plq"},
	{0,	XXM4,	57,	0,	0,	"pld"},
	{0,	XXM4,	58,	0,	0,	"plxvp"},
	{PMXOP,	XXM1,	59,	2<<3,	XXM5,	"pmxvi8ger4pp"},
	{PMXOP,	XXM1,	59,	18<<3,	XXM5,	"pmxvf16ger2pp"},
	{PMXOP,	XXM1,	59,	26<<3,	XXM5,	"pmxvf32gerpp"},
	{PMXOP,	XXM1,	59,	34<<3,	XXM5,	"pmxvi4ger8pp"},
	{PMXOP,	XXM1,	59,	34<<3,	XXM5,	"pmxvi16ger2spp"},
	{PMXOP,	XXM1,	59,	42<<3,	XXM5,	"pmxvi16ger2spp"},
	{PMXOP,	XXM1,	59,	50<<3,	XXM5,	"pmxvbf16ger2pp"},
	{PMXOP,	XXM1,	59,	58<<3,	XXM5,	"pmxvf64gerpp"},
	{PMXOP,	XXM1,	59,	82<<3,	XXM5,	"pmxvf16ger2np"},
	{PMXOP,	XXM1,	59,	90<<3,	XXM5,	"pmxvf32gernp"},
	{PMXOP,	XXM1,	59,	114<<3,	XXM5,	"pmxvbf16ger2n"},
	{PMXOP,	XXM1,	59,	122<<3,	XXM5,	"pmxvf64gernp"},
	{PMXOP,	XXM1,	59,	146<<3,	XXM5,	"pmxvf16ger2pn"},
	{PMXOP,	XXM1,	59,	154<<3,	XXM5,	"pmxvf32gerpn"},
	{PMXOP,	XXM1,	59,	178<<3,	XXM5,	"pmxvbf16ger2pn"},
	{PMXOP,	XXM1,	59,	186<<3,	XXM5,	"pmxvf64gerpn"},
	{PMXOP,	XXM1,	59,	210<<3,	XXM5,	"pmxvf16ger2nn"},
	{PMXOP,	XXM1,	59,	218<<3,	XXM5,	"pmxvf32gernn"},
	{PMXOP,	XXM1,	59,	242<<3,	XXM5,	"pmxvbf16ger2nn"},
	{PMXOP,	XXM1,	59,	250<<3,	XXM5,	"pmxvf64gernn"},
	{PMXOP,	XXM1,	59,	3<<3,	XXM5,	"pmxvi8ger4"},
	{PMXOP,	XXM1,	59,	19<<3,	XXM5,	"pmxvf16ger2"},
	{PMXOP,	XXM1,	59,	27<<3,	XXM5,	"pmxvf32ger"},
	{PMXOP,	XXM1,	59,	35<<3,	XXM5,	"pmxvi4ger8"},
	{PMXOP,	XXM1,	59,	43<<3,	XXM5,	"pmxvi16ger2s"},
	{PMXOP,	XXM1,	59,	51<<3,	XXM5,	"pmxvbf16ger2"},
	{PMXOP,	XXM1,	59,	59<<3,	XXM5,	"pmxvf64ger"},
	{PMXOP,	XXM1,	59,	75<<3,	XXM5,	"pmxvi16ger2"},
	{PMXOP,	XXM1,	59,	99<<3,	XXM5,	"pmxvi8ger4spp"},
	{PMXOP,	XXM1,	59,	107<<3,	XXM5,	"pmxvi16ger2pp"},
	{0,	XXM4,	60,	0,	0,	"pstq"},
	{2<<24,	XXM4,	14,	0,	0,	"paddi"},

	{0},
};

static void
prefixed(Opcode*, Instr *i)
{
	Popcode *p;

	for(p = popcodes; p->mnemonic != nil; p++){
		if(i->pop != p->op2)
			continue;
		if((i->w[0] & p->xomask1) != p->xo1)
			continue;
		if((i->w[1] & p->xomask2) != p->xo2)
			continue;
		format(p->mnemonic, i, nil);
		return;
	}
	if((i->w[0] & XXM1) == 3<<24)
		format("NOP", i, nil);
	else if((i->w[0] & XXM4) == 0){
		if((i->pop & ~1) == 25<<1)
			format("plxv", i, nil);
		else if((i->pop & ~1) == 27<<1)
			format("pstxv", i, nil);
		else
			format("unknown instruction", i, nil);
	} else
		format("unknown instruction", i, nil);
}

static Opcode opcodes[] = {
	{31,	266,	OEM,	"ADD%V%C",	add,	ir3},
	{31,	 10,	OEM,	"ADDC%V%C",	add,	ir3},
	{31,	138,	OEM,	"ADDE%V%C",	add,	ir3},
	{14,	0,	0,	"ADD",		addi,	ir2i},
	{12,	0,	0,	"ADDC",		addi,	ir2i},
	{13,	0,	0,	"ADDCCC",	addi,	ir2i},
	{15,	0,	0,	"ADD",		addis,	0},
	{31,	234,	OEM,	"ADDME%V%C",	gencc,	ir2},
	{31,	202,	OEM,	"ADDZE%V%C",	gencc,	ir2},

	{31,	28,	ALL,	"AND%C",	and,	il3},
	{31,	60,	ALL,	"ANDN%C",	and,	il3},
	{28,	0,	0,	"ANDCC",	andi,	il2u},
	{29,	0,	0,	"ANDCC",	shifted, 0},

	{18,	0,	0,	"B%L",		gencc,	"%j"},
	{16,	0,	0,	"BC%L",		branch,	"%d,%a,%J"},
	{19,	528,	ALL,	"BC%L",		branch,	"%d,%a,(CTR)"},
	{19,	16,	ALL,	"BC%L",		branch,	"%d,%a,(LR)"},

	{31,	0,	ALL,	"CMP%W",	0,	icmp3},
	{11,	0,	0,	"CMP%W",	0,	"R%a,%i,%D"},
	{31,	32,	ALL,	"CMP%WU",	0,	icmp3},
	{10,	0,	0,	"CMP%WU",	0,	"R%a,%I,%D"},

	{31,	58,	ALL,	"CNTLZD%C",	gencc,	ir2},	/* 64 */
	{31,	26,	ALL,	"CNTLZ%W%C",	gencc,	ir2},

	{19,	257,	ALL,	"CRAND",	gen,	cr3op},
	{19,	129,	ALL,	"CRANDN",	gen,	cr3op},
	{19,	289,	ALL,	"CREQV",	gen,	cr3op},
	{19,	225,	ALL,	"CRNAND",	gen,	cr3op},
	{19,	33,	ALL,	"CRNOR",	gen,	cr3op},
	{19,	449,	ALL,	"CROR",		gen,	cr3op},
	{19,	417,	ALL,	"CRORN",	gen,	cr3op},
	{19,	193,	ALL,	"CRXOR",	gen,	cr3op},

	{31,	86,	ALL,	"DCBF",		dcb,	0},
	{31,	470,	ALL,	"DCBI",		dcb,	0},
	{31,	54,	ALL,	"DCBST",	dcb,	0},
	{31,	278,	ALL,	"DCBT",		dcb,	0},
	{31,	246,	ALL,	"DCBTST",	dcb,	0},
	{31,	1014,	ALL,	"DCBZ",		dcb,	0},
	{31,	454,	ALL,	"DCCCI",	dcb,	0},
	{31,	966,	ALL,	"ICCCI",	dcb,	0},

	{31,	489,	OEM,	"DIVD%V%C",	qdiv,	ir3},	/* 64 */
	{31,	457,	OEM,	"DIVDU%V%C",	qdiv,	ir3},	/* 64 */
	{31,	491,	OEM,	"DIVW%V%C",	qdiv,	ir3},
	{31,	459,	OEM,	"DIVWU%V%C",	qdiv,	ir3},

	{31,	310,	ALL,	"ECIWX",	ldx,	0},
	{31,	438,	ALL,	"ECOWX",	stx,	0},
	{31,	854,	ALL,	"EIEIO",	gen,	0},

	{31,	284,	ALL,	"EQV%C",	gencc,	il3},

	{31,	954,	ALL,	"EXTSB%C",	gencc,	il2},
	{31,	922,	ALL,	"EXTSH%C",	gencc,	il2},
	{31,	986,	ALL,	"EXTSW%C",	gencc,	il2},	/* 64 */

	{63,	264,	ALL,	"FABS%C",	gencc,	fp2},
	{63,	21,	ALL,	"FADD%C",	gencc,	fp3},
	{59,	21,	ALL,	"FADDS%C",	gencc,	fp3},
	{63,	32,	ALL,	"FCMPO",	gen,	fpcmp},
	{63,	0,	ALL,	"FCMPU",	gen,	fpcmp},
	{63,	846,	ALL,	"FCFID%C",	gencc,	fp2},	/* 64 */
	{63,	814,	ALL,	"FCTID%C",	gencc,	fp2},	/* 64 */
	{63,	815,	ALL,	"FCTIDZ%C",	gencc,	fp2},	/* 64 */
	{63,	14,	ALL,	"FCTIW%C",	gencc,	fp2},
	{63,	15,	ALL,	"FCTIWZ%C",	gencc,	fp2},
	{63,	18,	ALL,	"FDIV%C",	gencc,	fp3},
	{59,	18,	ALL,	"FDIVS%C",	gencc,	fp3},
	{63,	29,	FP4,	"FMADD%C",	gencc,	fp4},
	{59,	29,	FP4,	"FMADDS%C",	gencc,	fp4},
	{63,	72,	ALL,	"FMOVD%C",	gencc,	fp2},
	{63,	28,	FP4,	"FMSUB%C",	gencc,	fp4},
	{59,	28,	FP4,	"FMSUBS%C",	gencc,	fp4},
	{63,	25,	FP4,	"FMUL%C",	gencc,	fp3c},
	{59,	25,	FP4,	"FMULS%C",	gencc,	fp3c},
	{63,	136,	ALL,	"FNABS%C",	gencc,	fp2},
	{63,	40,	ALL,	"FNEG%C",	gencc,	fp2},
	{63,	31,	FP4,	"FNMADD%C",	gencc,	fp4},
	{59,	31,	FP4,	"FNMADDS%C",	gencc,	fp4},
	{63,	30,	FP4,	"FNMSUB%C",	gencc,	fp4},
	{59,	30,	FP4,	"FNMSUBS%C",	gencc,	fp4},
	{59,	24,	ALL,	"FRES%C",	gencc,	fp2},	/* optional */
	{63,	12,	ALL,	"FRSP%C",	gencc,	fp2},
	{63,	26,	ALL,	"FRSQRTE%C",	gencc,	fp2},	/* optional */
	{63,	23,	FP4,	"FSEL%CC",	gencc,	fp4},	/* optional */
	{63,	22,	ALL,	"FSQRT%C",	gencc,	fp2},	/* optional */
	{59,	22,	ALL,	"FSQRTS%C",	gencc,	fp2},	/* optional */
	{63,	20,	FP4,	"FSUB%C",	gencc,	fp3},
	{59,	20,	FP4,	"FSUBS%C",	gencc,	fp3},

	{31,	982,	ALL,	"ICBI",		dcb,	0},	/* optional */
	{19,	150,	ALL,	"ISYNC",	gen,	0},

	{34,	0,	0,	"MOVBZ",	load,	ldop},
	{35,	0,	0,	"MOVBZU",	load,	ldop},
	{31,	119,	ALL,	"MOVBZU",	ldx,	0},
	{31,	87,	ALL,	"MOVBZ",	ldx,	0},
	{50,	0,	0,	"FMOVD",	fload,	fldop},
	{51,	0,	0,	"FMOVDU",	fload,	fldop},
	{31,	631,	ALL,	"FMOVDU",	fldx,	0},
	{31,	599,	ALL,	"FMOVD",	fldx,	0},
	{48,	0,	0,	"FMOVS",	load,	fldop},
	{49,	0,	0,	"FMOVSU",	load,	fldop},
	{31,	567,	ALL,	"FMOVSU",	fldx,	0},
	{31,	535,	ALL,	"FMOVS",	fldx,	0},
	{42,	0,	0,	"MOVH",		load,	ldop},
	{43,	0,	0,	"MOVHU",	load,	ldop},
	{31,	375,	ALL,	"MOVHU",	ldx,	0},
	{31,	343,	ALL,	"MOVH",		ldx,	0},
	{31,	790,	ALL,	"MOVHBR",	ldx,	0},
	{40,	0,	0,	"MOVHZ",	load,	ldop},
	{41,	0,	0,	"MOVHZU",	load,	ldop},
	{31,	311,	ALL,	"MOVHZU",	ldx,	0},
	{31,	279,	ALL,	"MOVHZ",	ldx,	0},
	{46,	0,	0,	"MOVMW",	load,	ldop},
	{31,	597,	ALL,	"LSW",		gen,	"(R%a),$%n,R%d"},
	{31,	533,	ALL,	"LSW",		ldx,	0},
	{31,	52,	ALL,	"LBAR",		ldx,	0},
	{31,	116,	ALL,	"LHAR",		ldx,	0},
	{31,	20,	ALL,	"LWAR",		ldx,	0},
	{31,	84,	ALL,	"LDAR",		ldx,	0},	/* 64 */
	{31,	276,	ALL,	"LQAR",		ldx,	0},	/* 64 */

	{58,	0,	ALL,	"MOVD",		load,	ldop},	/* 64 */
	{58,	1,	ALL,	"MOVDU",	load,	ldop},	/* 64 */
	{31,	53,	ALL,	"MOVDU",	ldx,	0},	/* 64 */
	{31,	21,	ALL,	"MOVD",		ldx,	0},	/* 64 */

	{31,	534,	ALL,	"MOVWBR",	ldx,	0},

	{58,	2,	ALL,	"MOVW",		load,	ldop},	/* 64 (lwa) */
	{31,	373,	ALL,	"MOVWU",	ldx,	0},	/* 64 */
	{31,	341,	ALL,	"MOVW",		ldx,	0},	/* 64 */

	{32,	0,	0,	"MOVW%Z",	load,	ldop},
	{33,	0,	0,	"MOVW%ZU",	load,	ldop},
	{31,	55,	ALL,	"MOVW%ZU",	ldx,	0},
	{31,	23,	ALL,	"MOVW%Z",	ldx,	0},

	{19,	0,	ALL,	"MOVFL",	gen,	"%S,%D"},
	{63,	64,	ALL,	"MOVCRFS",	gen,	"%S,%D"},
	{31,	19,	ALL,	"MOV%N",	gen,	"CR,R%d"},

	{31,	512,	ALL,	"MOVW",		gen,	"XER,%D"}, 	/* deprecated */
	{31,	595,	ALL,	"MOVW",		gen,	"SEG(%a),R%d"},	/* deprecated */
	{31,	659,	ALL,	"MOVW",		gen,	"SEG(R%b),R%d"},/* deprecated */
	{31,	323,	ALL,	"MOVW",		gen,	"DCR(%Q),R%d"},	/* deprecated */
	{31,	451,	ALL,	"MOVW",		gen,	"R%s,DCR(%Q)"},	/* deprecated */
	{31,	259,	ALL,	"MOVW",		gen,	"DCR(R%a),R%d"},/* deprecated */
	{31,	387,	ALL,	"MOVW",		gen,	"R%s,DCR(R%a)"},/* deprecated */
	{31,	210,	ALL,	"MOVW",		gen,	"R%s,SEG(%a)"},	/* deprecated */
	{31,	242,	ALL,	"MOVW",		gen,	"R%s,SEG(R%b)"},/* deprecated */

	{63,	583,	ALL,	"MOV%N%C",	gen,	"FPSCR, F%d"},	/* mffs */
	{31,	83,	ALL,	"MOV%N",	gen,	"MSR,R%d"},
	{31,	339,	ALL,	"MOV%N",	gen,	"%P,R%d"},
	{31,	144,	ALL,	"MOVFL",	gen,	"R%s,%m,CR"},
	{63,	70,	ALL,	"MTFSB0%C",	gencc,	"%D"},
	{63,	38,	ALL,	"MTFSB1%C",	gencc,	"%D"},
	{63,	711,	ALL,	"MOVFL%C",	gencc,	"F%b,%M,FPSCR"},	/* mtfsf */
	{63,	134,	ALL,	"MOVFL%C",	gencc,	"%K,%D"},
	{31,	146,	ALL,	"MOVW",		gen,	"R%s,MSR"},
	{31,	178,	ALL,	"MOVD",		gen,	"R%s,MSR"},
	{31,	467,	ALL,	"MOV%N",	gen,	"R%s,%P"},

	{31,	7,	ALL,	"MOVBE",	vldx,	0},
	{31,	39,	ALL,	"MOVHE",	vldx,	0},
	{31,	71,	ALL,	"MOVWE",	vldx,	0},
	{31,	103,	ALL,	"MOV",		vldx,	0},
	{31,	359,	ALL,	"MOV",		vldx,	0},	/* lvxl */

	{4,	525,	VXM,	"MOVBZ",	0,	"$%a,V%b,V%d"},
	{4,	589,	VXM,	"MOVHZ",	0,	"$%a,V%b,V%d"},
	{4,	653,	VXM,	"MOVWZ",	0,	"$%a,V%b,V%d"},
	{4,	717,	VXM,	"MOVDZ",	0,	"$%a,V%b,V%d"},

	{4,	1549,	VXM,	"MOVBZ",	0,	"R%a,V%b,R%d"},
	{4,	1805,	VXM,	"MOVBZ",	0,	"15-R%a,V%b,R%d"},
	{4,	1613,	VXM,	"MOVHZ",	0,	"R%a,V%b,R%d"},
	{4,	1869,	VXM,	"MOVHZ",	0,	"15-R%a,V%b,R%d"},
	{4,	1677,	VXM,	"MOVDZ",	0,	"R%a,V%b,R%d"},
	{4,	1933,	VXM,	"MOVDZ",	0,	"15-R%a,V%b,R%d"},
	{4,	781,	VXM,	"MOVB",		0,	"$%a,V%b,V%d"},
	{4,	845,	VXM,	"MOVH",		0,	"$%a,V%b,V%d"},
	{4,	909,	VXM,	"MOVW",		0,	"$%a,V%b,V%d"},
	{4,	973,	VXM,	"MOVD",		0,	"$%a,V%b,V%d"},

	{31,	6,	ALL,	"MOVSIL",	vldx,	0},
	{31,	38,	ALL,	"MOVSIR",	vldx,	0},

	{4,	782,	VXM,	"PACKP",	0,	vr3},
	{4,	398,	VXM,	"PACKHS",	0,	vr3},
	{4,	270,	VXM,	"PACKHSU",	0,	vr3},
	{4,	462,	VXM,	"PACKWS",	0,	vr3},
	{4,	334,	VXM,	"PACKWSU",	0,	vr3},
	{4,	1486,	VXM,	"PACKDS",	0,	vr3},
	{4,	1358,	VXM,	"PACKDSU",	0,	vr3},
	{4,	14,	VXM,	"PACKHUMU",	0,	vr3},
	{4,	142,	VXM,	"PACKHUSU",	0,	vr3},
	{4,	78,	VXM,	"PACKWUMU",	0,	vr3},
	{4,	206,	VXM,	"PACKWUSU",	0,	vr3},
	{4,	1102,	VXM,	"PACKDUMU",	0,	vr3},
	{4,	1230,	VXM,	"PACKDUSU",	0,	vr3},
	{4,	526,	VXM,	"UPACKB",	0,	vr2},
	{4,	654,	VXM,	"UPACKBL",	0,	vr2},
	{4,	590,	VXM,	"UPACKH",	0,	vr2},
	{4,	718,	VXM,	"UPACKHL",	0,	vr2},
	{4,	1614,	VXM,	"UPACKW",	0,	vr2},
	{4,	1742,	VXM,	"UPACKWL",	0,	vr2},
	{4,	846,	VXM,	"UPACKP",	0,	vr2},
	{4,	974,	VXM,	"UPACKPL",	0,	vr2},

	{31,	73,	ALL,	"MULHD%C",	gencc,	ir3},
	{31,	9,	ALL,	"MULHDU%C",	gencc,	ir3},
	{31,	233,	OEM,	"MULLD%V%C",	gencc,	ir3},

	{31,	75,	ALL,	"MULHW%C",	gencc,	ir3},
	{31,	11,	ALL,	"MULHWU%C",	gencc,	ir3},
	{31,	235,	OEM,	"MULLW%V%C",	gencc,	ir3},

	{7,	0,	0,	"MULLW",	qdiv,	"%i,R%a,R%d"},

	{31,	476,	ALL,	"NAND%C",	gencc,	il3},
	{31,	104,	OEM,	"NEG%V%C",	neg,	ir2},
	{31,	124,	ALL,	"NOR%C",	gencc,	il3},
	{31,	444,	ALL,	"OR%C",		or,	il3},
	{31,	412,	ALL,	"ORN%C",	or,	il3},
	{24,	0,	0,	"OR",		or,	"%I,R%d,R%a"},
	{25,	0,	0,	"OR",		shifted, 0},

	{19,	50,	ALL,	"RFI",		gen,	0},
	{19,	51,	ALL,	"RFCI",		gen,	0},

	{30,	8,	RLDC,	"RLDCL%C",	gencc,	rldc},	/* 64 */
	{30,	9,	RLDC,	"RLDCR%C",	gencc,	rldc},	/* 64 */
	{30,	0,	RLDI,	"RLDCL%C",	gencc,	rldi},	/* 64 */
	{30,	1<<1, RLDI,	"RLDCR%C",	gencc,	rldi},	/* 64 */
	{30,	2<<1, RLDI,	"RLDC%C",	gencc,	rldi},	/* 64 */
	{30,	3<<1, RLDI,	"RLDMI%C",	gencc,	rldi},	/* 64 */

	{20,	0,	0,	"RLWMI%C",	gencc,	rlimi},
	{21,	0,	0,	"RLWNM%C",	gencc,	rlimi},
	{23,	0,	0,	"RLWNM%C",	gencc,	rlim},

	{17,	2,	ALL,	"SYSCALL",	gen,	0},

	{31,	27,	ALL,	"SLD%C",	shift,	il3},	/* 64 */
	{31,	24,	ALL,	"SLW%C",	shift,	il3},

	{31,	794,	ALL,	"SRAD%C",	shift,	il3},	/* 64 */
	{31,	(413<<1)|0,	ALL,	"SRAD%C",	shifti,	il3s},	/* 64 */
	{31,	(413<<1)|1,	ALL,	"SRAD%C",	shifti,	il3s},	/* 64 */
	{31,	792,	ALL,	"SRAW%C",	shift,	il3},
	{31,	824,	ALL,	"SRAW%C",	shifti,	il3s},

	{31,	539,	ALL,	"SRD%C",	shift,	il3},	/* 64 */
	{31,	536,	ALL,	"SRW%C",	shift,	il3},

	{38,	0,	0,	"MOVB",		store,	stop},
	{39,	0,	0,	"MOVBU",	store,	stop},
	{31,	247,	ALL,	"MOVBU",	stx,	0},
	{31,	215,	ALL,	"MOVB",		stx,	0},
	{54,	0,	0,	"FMOVD",	fstore,	fstop},
	{55,	0,	0,	"FMOVDU",	fstore,	fstop},
	{31,	759,	ALL,	"FMOVDU",	fstx,	0},
	{31,	727,	ALL,	"FMOVD",	fstx,	0},
	{52,	0,	0,	"FMOVS",	fstore,	fstop},
	{53,	0,	0,	"FMOVSU",	fstore,	fstop},
	{31,	695,	ALL,	"FMOVSU",	fstx,	0},
	{31,	663,	ALL,	"FMOVS",	fstx,	0},
	{44,	0,	0,	"MOVH",		store,	stop},
	{31,	918,	ALL,	"MOVHBR",	stx,	0},
	{45,	0,	0,	"MOVHU",	store,	stop},
	{31,	439,	ALL,	"MOVHU",	stx,	0},
	{31,	407,	ALL,	"MOVH",		stx,	0},
	{47,	0,	0,	"MOVMW",	store,	stop},
	{31,	725,	ALL,	"STSW",		gen,	"R%d,$%n,(R%a)"},
	{31,	661,	ALL,	"STSW",		stx,	0},
	{36,	0,	0,	"MOVW",		store,	stop},
	{31,	662,	ALL,	"MOVWBR",	stx,	0},
	{31,	694,	ALL,	"STBCCC",	stx,	0},
	{31,	726,	ALL,	"STHCCC",	stx,	0},
	{31,	150,	ALL,	"STWCCC",	stx,	0},
	{31,	214,	ALL,	"STDCCC",	stx,	0},	/* 64 */
	{31,	182,	ALL,	"STQCCC",	stx,	0},	/* 64 */
	{37,	0,	0,	"MOVWU",	store,	stop},
	{31,	183,	ALL,	"MOVWU",	stx,	0},
	{31,	151,	ALL,	"MOVW",		stx,	0},

	{62,	0,	0,	"MOVD%U",	store,	stop},	/* 64 */
	{31,	149,	ALL,	"MOVD",		stx,	0,},	/* 64 */
	{31,	181,	ALL,	"MOVDU",	stx,	0},	/* 64 */

	{31,	135,	ALL,	"MOVBE",	vstx,	0},
	{31,	167,	ALL,	"MOVHE",	vstx,	0},
	{31,	199,	ALL,	"MOVWE",	vstx,	0},
	{31,	231,	ALL,	"MOV",		vstx,	0},
	{31,	487,	ALL,	"MOV",		vstx,	0},	/* stvxl */

	{31,	498,	ALL,	"SLBIA",	gen,	0},	/* 64 */
	{31,	434,	ALL,	"SLBIE",	gen,	"R%b"},	/* 64 */
	{31,	466,	ALL,	"SLBIEX",	gen,	"R%b"},	/* 64 */
	{31,	915,	ALL,	"SLBMFEE",	gen,	"R%b,R%d"},	/* 64 */
	{31,	851,	ALL,	"SLBMFEV",	gen,	"R%b,R%d"},	/* 64 */
	{31,	402,	ALL,	"SLBMTE",	gen,	"R%s,R%b"},	/* 64 */

	{31,	40,	OEM,	"SUB%V%C",	sub,	ir3},
	{31,	8,	OEM,	"SUBC%V%C",	sub,	ir3},
	{31,	136,	OEM,	"SUBE%V%C",	sub,	ir3},
	{8,	0,	0,	"SUBC",		gen,	"R%a,%i,R%d"},
	{31,	232,	OEM,	"SUBME%V%C",	sub,	ir2},
	{31,	200,	OEM,	"SUBZE%V%C",	sub,	ir2},

	{31,	598,	ALL,	"SYNC",		sync,	0},
	{2,	0,	0,	"TD",		gen,	"%d,R%a,%i"},	/* 64 */
	{31,	370,	ALL,	"TLBIA",	gen,	0},	/* optional */
	{31,	306,	ALL,	"TLBIE",	gen,	"R%b"},	/* optional */
	{31,	274,	ALL,	"TLBIEL",	gen,	"R%b"},	/* optional */
	{31,	1010,	ALL,	"TLBLI",	gen,	"R%b"},	/* optional */
	{31,	978,	ALL,	"TLBLD",	gen,	"R%b"},	/* optional */
	{31,	566,	ALL,	"TLBSYNC",	gen,	0},	/* optional */
	{31,	68,	ALL,	"TD",		gen,	"%d,R%a,R%b"},	/* 64 */
	{31,	4,	ALL,	"TW",		gen,	"%d,R%a,R%b"},
	{3,	0,	0,	"TW",		gen,	"%d,R%a,%i"},

	{31,	316,	ALL,	"XOR",		and,	il3},
	{26,	0,	0,	"XOR",		and,	il2u},
	{27,	0,	0,	"XOR",		shifted, 0},

	/* unimplemented/lightly implemented from here on out */
	{19,	2,	FP4, "ADD",	addpcis,	0},
	{19,	560,	ALL,	"BC%L.T",	0,	"%d,%a,(TAR)"},
	{19,	370,	ALL,	"STOP",		0,	0},
	{17,	1,	ALL,	"SYSCALL.V",	0,	0},
	{19,	82,	ALL,	"RETURN.V",	0,	0},
	{19,	146,	ALL,	"RETURN.E",	0,	0},
	{19,	18,	ALL,	"RETURN.I",	0,	0},
	{19,	274,	ALL,	"HRETURN.I",	0,	0},
	{19,	306,	ALL,	"URETURN.I",	0,	0},

	{4,	0,	VXM,	"vaddubm",	0,	vr3},
	{4,	64,	VXM,	"vadduhm",	0,	vr3},
	{4,	128,	VXM,	"vadduwm",	0,	vr3},
	{4,	192,	VXM,	"vaddudm",	0,	vr3},
	{4,	256,	VXM,	"vadduqm",	0,	vr3},
	{4,	320,	VXM,	"vaddcuq",	0,	vr3},
	{4,	384,	VXM,	"vaddcuw",	0,	vr3},
	{4,	512,	VXM,	"vaddubs",	0,	vr3},
	{4,	576,	VXM,	"vadduhs",	0,	vr3},
	{4,	640,	VXM,	"vadduws",	0,	vr3},
	{4,	768,	VXM,	"vaddsbs",	0,	vr3},
	{4,	832,	VXM,	"vaddshs",	0,	vr3},
	{4,	896,	VXM,	"vaddsws",	0,	vr3},
	{4,	1024,	VXM,	"vsububm",	0,	vr3},
	{4,	1088,	VXM,	"vsubuhm",	0,	vr3},
	{4,	1152,	VXM,	"vsubuwm",	0,	vr3},
	{4,	1216,	VXM,	"vsubudm",	0,	vr3},
	{4,	1280,	VXM,	"vsubuqm",	0,	vr3},
	{4,	1344,	VXM,	"vsubcuq",	0,	vr3},
	{4,	1408,	VXM,	"vsubcuw",	0,	vr3},
	{4,	1536,	VXM,	"vsububs",	0,	vr3},
	{4,	1600,	VXM,	"vsubuhs",	0,	vr3},
	{4,	1664,	VXM,	"vsubuws",	0,	vr3},
	{4,	1792,	VXM,	"vsubsbs",	0,	vr3},
	{4,	1856,	VXM,	"vsubshs",	0,	vr3},
	{4,	1920,	VXM,	"vsubsws",	0,	vr3},
	{4,	1,	VXM,	"vmul10cuq",	0,	vr2},
	{4,	65,	VXM,	"vmul10ecuq",	0,	vr2},
	{4,	257,	VXM,	"vcmpuq",	0,	vr3},
	{4,	321,	VXM,	"vcmpsq",	0,	vr3},
	{4,	513,	VXM,	"vmul10uq",	0,	vr2},
	{4,	577,	VXM,	"vmul10euq",	0,	vr2},
	{4,	833,	VXM,	"bcdcpsgn.",	0,	vr3},
	{4,	1,	VXM2,	"bcdadd.",	0,	vr3},
	{4,	65,	VXM2,	"bcdsub.",	0,	vr3},
	{4,	129,	VXM2,	"bcdus.",	0,	vr3},
	{4,	193,	VXM2,	"bcds.",	0,	vr3},
	{4,	257,	VXM2,	"bcdtrunc.",	0,	vr3},
	{4,	1345,	VXM,	"bcdutrunc.",	0,	vr3},
	{4,	385,	VXM2,	"bcdctsq.",	vra2s,	vr2},
	{4,	385,	VXM2,	"bcdcfsq.",	vra2s,	vr2},
	{4,	385,	VXM2,	"bcdctz.",	vra2s,	vr2},
	{4,	385,	VXM2,	"bcdctn.",	vra2s,	vr2},
	{4,	385,	VXM2,	"bcdcfz.",	vra2s,	vr2},
	{4,	385,	VXM2,	"bcdcfn.",	vra2s,	vr2},
	{4,	385,	VXM2,	"bcdsetsgn.",	0,	vr3},
	{4,	449,	VXM2,	"bcdsr.",	0,	vr3},
	{4,	2,	VXM,	"vmaxub",	0,	vr3},
	{4,	66,	VXM,	"vmaxuh",	0,	vr3},
	{4,	130,	VXM,	"vmaxuw",	0,	vr3},
	{4,	194,	VXM,	"vmaxud",	0,	vr3},
	{4,	258,	VXM,	"vmaxsb",	0,	vr3},
	{4,	322,	VXM,	"vmaxsh",	0,	vr3},
	{4,	386,	VXM,	"vmaxsw",	0,	vr3},
	{4,	450,	VXM,	"vmaxsd",	0,	vr3},
	{4,	514,	VXM,	"vminub",	0,	vr3},
	{4,	578,	VXM,	"vminuh",	0,	vr3},
	{4,	642,	VXM,	"vminuw",	0,	vr3},
	{4,	706,	VXM,	"vminud",	0,	vr3},
	{4,	770,	VXM,	"vminsb",	0,	vr3},
	{4,	834,	VXM,	"vminsh",	0,	vr3},
	{4,	898,	VXM,	"vminsw",	0,	vr3},
	{4,	962,	VXM,	"vminsd",	0,	vr3},
	{4,	1026,	VXM,	"vavgub",	0,	vr3},
	{4,	1090,	VXM,	"vavguh",	0,	vr3},
	{4,	1154,	VXM,	"vavguw",	0,	vr3},
	{4,	1282,	VXM,	"vavgsb",	0,	vr3},
	{4,	1346,	VXM,	"vavgsh",	0,	vr3},
	{4,	1410,	VXM,	"vavgsw",	0,	vr3},
	{4,	1538,	VXM,	"vclzlsbb",	vra2s,	vr2},
	{4,	1538,	VXM,	"vctzlsbb",	vra2s,	vr2},
	{4,	1538,	VXM,	"vnegw",	vra2s,	vr2},
	{4,	1538,	VXM,	"vnegd",	vra2s,	vr2},
	{4,	1538,	VXM,	"vprtybw",	vra2s,	vr2},
	{4,	1538,	VXM,	"vprtybd",	vra2s,	vr2},
	{4,	1538,	VXM,	"vprtybq",	vra2s,	vr2},
	{4,	1538,	VXM,	"vextsb2w",	vra2s,	vr2},
	{4,	1538,	VXM,	"vextsh2w",	vra2s,	vr2},
	{4,	1538,	VXM,	"vextsb2d",	vra2s,	vr2},
	{4,	1538,	VXM,	"vextsh2d",	vra2s,	vr2},
	{4,	1538,	VXM,	"vextsw",	vra2s,	vr2},
	{4,	1538,	VXM,	"vextsd",	vra2s,	vr2},
	{4,	1538,	VXM,	"vctzb",	vra2s,	vr2},
	{4,	1538,	VXM,	"vctzh",	vra2s,	vr2},
	{4,	1538,	VXM,	"vctzw",	vra2s,	vr2},
	{4,	1538,	VXM,	"vctzd",	vra2s,	vr2},
	{4,	1602,	ALL,	"vexpandbm",	vra2s,	vr2},
	{4,	1602,	VXM,	"vexpandhm",	vra2s,	vr2},
	{4,	1602,	VXM,	"vexpandwm",	vra2s,	vr2},
	{4,	1602,	VXM,	"vexpanddm",	vra2s,	vr2},
	{4,	1602,	VXM,	"vexpandqm",	vra2s,	vr2},
	{4,	1602,	VXM,	"vextractbm",	vra2s,	vr2},
	{4,	1602,	VXM,	"vextracthm",	vra2s,	vr2},
	{4,	1602,	VXM,	"vextractwm",	vra2s,	vr2},
	{4,	1602,	VXM,	"vextractdm",	vra2s,	vr2},
	{4,	1602,	VXM,	"vextractqm",	vra2s,	vr2},
	{4,	1602,	VXM,	"mtvsrbm",	0,	vr3},
	{4,	1602,	VXM,	"mtvsrhm",	0,	vr3},
	{4,	1602,	VXM,	"mtvsrwm",	0,	vr3},
	{4,	1602,	VXM,	"mtvsrdm",	0,	vr3},
	{4,	1602,	VXM,	"mtvsrqm",	0,	vr3},
	{4,	1602,	VXM,	"vcntmbb",	0,	vr3},
	{4,	1602,	VXM,	"vcntmbd",	0,	vr3},
	{4,	1602,	VXM,	"vcntmbh",	0,	vr3},
	{4,	1602,	VXM,	"vcntmbw",	0,	vr3},
	{4,	1666,	VXM,	"vshasigmaw",	0,	vr3},
	{4,	1730,	VXM,	"vshasigmad",	0,	vr3},
	{4,	1794,	VXM,	"vclzb",	0,	vr3},
	{4,	1858,	VXM,	"vclzh",	0,	vr3},
	{4,	1922,	VXM,	"vclzw",	0,	vr3},
	{4,	1986,	VXM,	"vclzd",	0,	vr3},
	{4,	1027,	VXM,	"vabsdub",	0,	vr3},
	{4,	1091,	VXM,	"vabsduh",	0,	vr3},
	{4,	1155,	VXM,	"vabsduw",	0,	vr3},
	{4,	1795,	VXM,	"vpopcntb",	0,	vr3},
	{4,	1859,	VXM,	"vpopcnth",	0,	vr3},
	{4,	1923,	VXM,	"vpopcntw",	0,	vr3},
	{4,	1987,	VXM,	"vpopcntd",	0,	vr3},
	{4,	4,	VXM,	"vrlb",	0,	vr3},
	{4,	68,	VXM,	"vrlh",	0,	vr3},
	{4,	132,	VXM,	"vrlw",	0,	vr3},
	{4,	196,	VXM,	"vrld",	0,	vr3},
	{4,	260,	VXM,	"vslb",	0,	vr3},
	{4,	324,	VXM,	"vslh",	0,	vr3},
	{4,	388,	VXM,	"vslw",	0,	vr3},
	{4,	452,	VXM,	"vsl",	0,	vr3},
	{4,	516,	VXM,	"vsrb",	0,	vr3},
	{4,	580,	VXM,	"vsrh",	0,	vr3},
	{4,	644,	VXM,	"vsrw",	0,	vr3},
	{4,	708,	VXM,	"vsr",	0,	vr3},
	{4,	772,	VXM,	"vsrab",	0,	vr3},
	{4,	836,	VXM,	"vsrah",	0,	vr3},
	{4,	900,	VXM,	"vsraw",	0,	vr3},
	{4,	964,	VXM,	"vsrad",	0,	vr3},
	{4,	1028,	VXM,	"vand",	0,	vr3},
	{4,	1092,	VXM,	"vandc",	0,	vr3},
	{4,	1156,	VXM,	"vor",	0,	vr3},
	{4,	1220,	VXM,	"vxor",	0,	vr3},
	{4,	1284,	VXM,	"vnor",	0,	vr3},
	{4,	1348,	VXM,	"vorc",	0,	vr3},
	{4,	1412,	VXM,	"vnand",	0,	vr3},
	{4,	1476,	VXM,	"vsld",	0,	vr3},
	{4,	1540,	VXM,	"mfvscr",	0,	vr3},
	{4,	1604,	VXM,	"mtvscr",	0,	vr3},
	{4,	1668,	VXM,	"veqv",	0,	vr3},
	{4,	1732,	VXM,	"vsrd",	0,	vr3},
	{4,	1796,	VXM,	"vsrv",	0,	vr3},
	{4,	1860,	VXM,	"vslv",	0,	vr3},
	{4,	1924,	VXM,	"vclzdm",	0,	vr3},
	{4,	1988,	VXM,	"vctzdm",	0,	vr3},
	{4,	5,	VXM,	"vrlq",	0,	vr3},
	{4,	69,	VXM,	"vrlqmi",	0,	vr3},
	{4,	133,	VXM,	"vrlwmi",	0,	vr3},
	{4,	197,	VXM,	"vrldmi",	0,	vr3},
	{4,	261,	VXM,	"vslq",	0,	vr3},
	{4,	325,	VXM,	"vrlqnm",	0,	vr3},
	{4,	389,	VXM,	"vrlwnm",	0,	vr3},
	{4,	453,	VXM,	"vrldnm",	0,	vr3},
	{4,	517,	VXM,	"vsrq",	0,	vr3},
	{4,	773,	VXM,	"vsraq",	0,	vr3},
	{4,	6,	VCM,	"vcmpequb[.]",	0,	vr3},
	{4,	70,	VCM,	"vcmpequh[.]",	0,	vr3},
	{4,	134,	VCM,	"vcmpequw[.]",	0,	vr3},
	{4,	198,	VCM,	"vcmpeqfp[.]",	0,	vr3},
	{4,	454,	VCM,	"vcmpgefp[.]",	0,	vr3},
	{4,	518,	VCM,	"vcmpgtub[.]",	0,	vr3},
	{4,	582,	VCM,	"vcmpgtuh[.]",	0,	vr3},
	{4,	646,	VCM,	"vcmpgtuw[.]",	0,	vr3},
	{4,	710,	VCM,	"vcmpgtfp[.]",	0,	vr3},
	{4,	774,	VCM,	"vcmpgtsb[.]",	0,	vr3},
	{4,	838,	VCM,	"vcmpgtsh[.]",	0,	vr3},
	{4,	902,	VCM,	"vcmpgtsw[.]",	0,	vr3},
	{4,	966,	VCM,	"vcmpbfp[.]",	0,	vr3},
	{4,	7,	VCM,	"vcmpneb[.]",	0,	vr3},
	{4,	71,	VCM,	"vcmpneh[.]",	0,	vr3},
	{4,	135,	VCM,	"vcmpnew[.]",	0,	vr3},
	{4,	199,	VCM,	"vcmpequd[.]",	0,	vr3},
	{4,	263,	VCM,	"vcmpnezb[.]",	0,	vr3},
	{4,	327,	VCM,	"vcmpnezh[.]",	0,	vr3},
	{4,	391,	VCM,	"vcmpnezw[.]",	0,	vr3},
	{4,	455,	VCM,	"vcmpequq[.]",	0,	vr3},
	{4,	647,	VCM,	"vcmpgtuq[.]",	0,	vr3},
	{4,	711,	VCM,	"vcmpgtud[.]",	0,	vr3},
	{4,	903,	VCM,	"vcmpgtsq[.]",	0,	vr3},
	{4,	967,	VCM,	"vcmpgtsd[.]",	0,	vr3},
	{4,	8,	VXM,	"vmuloub",	0,	vr3},
	{4,	72,	VXM,	"vmulouh",	0,	vr3},
	{4,	136,	VXM,	"vmulouw",	0,	vr3},
	{4,	200,	VXM,	"vmuloud",	0,	vr3},
	{4,	264,	VXM,	"vmulosb",	0,	vr3},
	{4,	328,	VXM,	"vmulosh",	0,	vr3},
	{4,	392,	VXM,	"vmulosw",	0,	vr3},
	{4,	456,	VXM,	"vmulosd",	0,	vr3},
	{4,	520,	VXM,	"vmuleub",	0,	vr3},
	{4,	584,	VXM,	"vmuleuh",	0,	vr3},
	{4,	648,	VXM,	"vmuleuw",	0,	vr3},
	{4,	712,	VXM,	"vmuleud",	0,	vr3},
	{4,	776,	VXM,	"vmulesb",	0,	vr3},
	{4,	840,	VXM,	"vmulesh",	0,	vr3},
	{4,	904,	VXM,	"vmulesw",	0,	vr3},
	{4,	968,	VXM,	"vmulesd",	0,	vr3},
	{4,	1032,	VXM,	"vpmsumb",	0,	vr3},
	{4,	1096,	VXM,	"vpmsumh",	0,	vr3},
	{4,	1160,	VXM,	"vpmsumw",	0,	vr3},
	{4,	1224,	VXM,	"vpmsumd",	0,	vr3},
	{4,	1288,	VXM,	"vcipher",	0,	vr3},
	{4,	1352,	VXM,	"vncipher",	0,	vr3},
	{4,	1480,	VXM,	"vsbox",	0,	vr3},
	{4,	1544,	VXM,	"vsum4ubs",	0,	vr3},
	{4,	1608,	VXM,	"vsum4shs",	0,	vr3},
	{4,	1672,	VXM,	"vsum2sws",	0,	vr3},
	{4,	1800,	VXM,	"vsum4sbs",	0,	vr3},
	{4,	1928,	VXM,	"vsumsws",	0,	vr3},
	{4,	137,	VXM,	"vmuluwm",	0,	vr3},
	{4,	457,	VXM,	"vmulld",	0,	vr3},
	{4,	649,	VXM,	"vmulhuw",	0,	vr3},
	{4,	713,	VXM,	"vmulhud",	0,	vr3},
	{4,	905,	VXM,	"vmulhsw",	0,	vr3},
	{4,	969,	VXM,	"vmulhsd",	0,	vr3},
	{4,	1289,	VXM,	"vcipherlast",	0,	vr3},
	{4,	1353,	VXM,	"vncipherlast",	0,	vr3},
	{4,	10,	VXM,	"vaddfp",	0,	vr3},
	{4,	74,	VXM,	"vsubfp",	0,	vr3},
	{4,	266,	VXM,	"vrefp",	0,	vr3},
	{4,	330,	VXM,	"vrsqrtefp",	0,	vr3},
	{4,	394,	VXM,	"vexptefp",	0,	vr3},
	{4,	458,	VXM,	"vlogefp",	0,	vr3},
	{4,	522,	VXM,	"vrfin",	0,	vr3},
	{4,	586,	VXM,	"vrfiz",	0,	vr3},
	{4,	650,	VXM,	"vrfip",	0,	vr3},
	{4,	714,	VXM,	"vrfim",	0,	vr3},
	{4,	778,	VXM,	"vcfux",	0,	vr3},
	{4,	842,	VXM,	"vcfsx",	0,	vr3},
	{4,	906,	VXM,	"vctuxs",	0,	vr3},
	{4,	970,	VXM,	"vctsxs",	0,	vr3},
	{4,	1034,	VXM,	"vmaxfp",	0,	vr3},
	{4,	1098,	VXM,	"vminfp",	0,	vr3},
	{4,	11,	VXM,	"vdivuq",	0,	vr3},
	{4,	139,	VXM,	"vdivuw",	0,	vr3},
	{4,	203,	VXM,	"vdivud",	0,	vr3},
	{4,	267,	VXM,	"vdivsq",	0,	vr3},
	{4,	395,	VXM,	"vdivsw",	0,	vr3},
	{4,	459,	VXM,	"vdivsd",	0,	vr3},
	{4,	523,	VXM,	"vdiveuq",	0,	vr3},
	{4,	651,	VXM,	"vdiveuw",	0,	vr3},
	{4,	715,	VXM,	"vdiveud",	0,	vr3},
	{4,	779,	VXM,	"vdivesq",	0,	vr3},
	{4,	907,	VXM,	"vdivesw",	0,	vr3},
	{4,	971,	VXM,	"vdivesd",	0,	vr3},
	{4,	1547,	VXM,	"vmoduq",	0,	vr3},
	{4,	1675,	VXM,	"vmoduw",	0,	vr3},
	{4,	1739,	VXM,	"vmodud",	0,	vr3},
	{4,	1803,	VXM,	"vmodsq",	0,	vr3},
	{4,	1931,	VXM,	"vmodsw",	0,	vr3},
	{4,	1995,	VXM,	"vmodsd",	0,	vr3},
	{4,	12,	VXM,	"vmrghb",	0,	vr3},
	{4,	76,	VXM,	"vmrghh",	0,	vr3},
	{4,	140,	VXM,	"vmrghw",	0,	vr3},
	{4,	268,	VXM,	"vmrglb",	0,	vr3},
	{4,	332,	VXM,	"vmrglh",	0,	vr3},
	{4,	396,	VXM,	"vmrglw",	0,	vr3},
	{4,	524,	VXM,	"vspltb",	0,	vr3},
	{4,	588,	VXM,	"vsplth",	0,	vr3},
	{4,	652,	VXM,	"vspltw",	0,	vr3},
	{4,	780,	VXM,	"vspltisb",	0,	vr3},
	{4,	844,	VXM,	"vspltish",	0,	vr3},
	{4,	908,	VXM,	"vspltisw",	0,	vr3},
	{4,	1036,	VXM,	"vslo",	0,	vr3},
	{4,	1100,	VXM,	"vsro",	0,	vr3},
	{4,	1228,	VXM,	"vgnb",	0,	vr3},
	{4,	1292,	VXM,	"vgbbd",	0,	vr3},
	{4,	1356,	VXM,	"vbpermq",	0,	vr3},
	{4,	1484,	VXM,	"vbpermd",	0,	vr3},
	{4,	1676,	VXM,	"vmrgow",	0,	vr3},
	{4,	1932,	VXM,	"vmrgew",	0,	vr3},
	{4,	13,	VCM,	"vstribl[.]",	vra2s,	vr2},
	{4,	13,	VCM,	"vstribr[.]",	vra2s,	vr2},
	{4,	13,	VCM,	"vstrihl[.]",	vra2s,	vr2},
	{4,	13,	VCM,	"vstrihr[.]",	vra2s,	vr2},
	{4,	397,	VXM,	"vclrlb",	0,	vr3},
	{4,	461,	VXM,	"vclrrb",	0,	vr3},
	{4,	1357,	VXM,	"vcfuged",	0,	vr3},
	{4,	1421,	VXM,	"vpextd",	0,	vr3},
	{4,	1485,	VXM,	"vpdepd",	0,	vr3},

	{4,	15,	VXM,	"vinsbvlx",	0,	vr3},
	{4,	79,	VXM,	"vinshvlx",	0,	vr3},
	{4,	143,	VXM,	"vinswvlx",	0,	vr3},
	{4,	207,	VXM,	"vinsw",	0,	vr3},
	{4,	271,	VXM,	"vinsbvrx",	0,	vr3},
	{4,	335,	VXM,	"vinshvrx",	0,	vr3},
	{4,	399,	VXM,	"vinswvrx",	0,	vr3},
	{4,	463,	VXM,	"vinsd",	0,	vr3},
	{4,	527,	VXM,	"vinsblx",	0,	vr3},
	{4,	591,	VXM,	"vinshlx",	0,	vr3},
	{4,	655,	VXM,	"vinswlx",	0,	vr3},
	{4,	719,	VXM,	"vinsdlx",	0,	vr3},
	{4,	783,	VXM,	"vinsbrx",	0,	vr3},
	{4,	847,	VXM,	"vinshrx",	0,	vr3},
	{4,	911,	VXM,	"vinswrx",	0,	vr3},
	{4,	975,	VXM,	"vinsdrx",	0,	vr3},
	{4,	20,	0b111110,	"mtvsrbmi",	0,	vr3},
	{4,	22,	VAM,	"vsldbi",	vsldbi,	vr3},
	{4,	22,	VAM,	"vsrdbi",	vsldbi,	vr3},
	{4,	23,	VAM,	"vmsumcud",	0,	vr3},
	{4,	24,	VAM,	"vextdubvlx",	0,	vr3},
	{4,	25,	VAM,	"vextdubvrx",	0,	vr3},
	{4,	26,	VAM,	"vextduhvlx",	0,	vr3},
	{4,	27,	VAM,	"vextduhvrx",	0,	vr3},
	{4,	28,	VAM,	"vextduwvlx",	0,	vr3},
	{4,	29,	VAM,	"vextduwvrx",	0,	vr3},
	{4,	30,	VAM,	"vextddvlx",	0,	vr3},
	{4,	31,	VAM,	"vextddvrx",	0,	vr3},
	{4,	32,	VAM,	"vmhaddshs",	0,	vr3},
	{4,	33,	VAM,	"vmhraddshs",	0,	vr3},
	{4,	34,	VAM,	"vmladduhm",	0,	vr3},
	{4,	35,	VAM,	"vmsumudm",	0,	vr3},
	{4,	36,	VAM,	"vmsumubm",	0,	vr3},
	{4,	37,	VAM,	"vmsummbm",	0,	vr3},
	{4,	38,	VAM,	"vmsumuhm",	0,	vr3},
	{4,	39,	VAM,	"vmsumuhs",	0,	vr3},
	{4,	40,	VAM,	"vmsumshm",	0,	vr3},
	{4,	41,	VAM,	"vmsumshs",	0,	vr3},
	{4,	42,	VAM,	"vsel",	0,	vr3},
	{4,	43,	VAM,	"vperm",	0,	vr3},
	{4,	44,	VAM,	"vsldoi",	0,	vr3},
	{4,	45,	VAM,	"vpermxor",	0,	vr3},
	{4,	46,	VAM,	"vmaddfp",	0,	vr3},
	{4,	47,	VAM,	"vnmsubfp",	0,	vr3},
	{4,	48,	VAM,	"maddhd",	0,	vr3},
	{4,	49,	VAM,	"maddhdu",	0,	vr3},
	{4,	51,	VAM,	"maddld",	0,	vr3},
	{4,	59,	VAM,	"vpermr",	0,	vr3},
	{4,	60,	VAM,	"vaddeuqm",	0,	vr3},
	{4,	61,	VAM,	"vaddecuq",	0,	vr3},
	{4,	62,	VAM,	"vsubeuqm",	0,	vr3},
	{4,	63,	VAM,	"vsubecuq",	0,	vr3},
	{6,	0,	ALL,	"lxvp",	0,	0},
	{6,	1,	ALL,	"stxvp",	0,	0},
	{31,	128,	ALL,	"setb",	0,	0},
	{31,	192,	ALL,	"cmprb",	0,	0},
	{31,	224,	ALL,	"cmpeqb",	0,	0},
	{31,	384,	ALL,	"setbc",	0,	0},
	{31,	416,	ALL,	"setbcr",	0,	0},
	{31,	448,	ALL,	"setnbc",	0,	0},
	{31,	480,	ALL,	"setnbcr",	0,	0},
	{31,	576,	ALL,	"mcrxrx",	0,	0},
	{31,	582,	ALL,	"lwat",	0,	0},
	{31,	614,	ALL,	"ldat",	0,	0},
	{31,	710,	ALL,	"stwat",	0,	0},
	{31,	742,	ALL,	"stdat",	0,	0},
	{31,	774,	ALL,	"copy",	0,	0},
	{31,	838,	ALL,	"cpabort",	0,	0},
	{31,	902,	ALL,	"paste[.]",	0,	0},
	{31,	265,	ALL,	"modud",	0,	0},
	{31,	393,	ALL,	"divdeu[.]",	0,	0},
	{31,	425,	ALL,	"divde[.]",	0,	0},
	{31,	777,	ALL,	"modsd",	0,	0},
	{31,	905,	ALL,	"divdeuo[.]",	0,	0},
	{31,	937,	ALL,	"divdeo[.]",	0,	0},
	{31,	170,	0b11111111,	"addex",	0,	0},
	{31,	74,	ALL,	"addg",	0,	0},
	{31,	267,	ALL,	"moduw",	0,	0},
	{31,	395,	ALL,	"divweu[.]",	0,	0},
	{31,	427,	ALL,	"divwe[.]",	0,	0},
	{31,	779,	ALL,	"modsw",	0,	0},
	{31,	907,	ALL,	"divweuo[.]",	0,	0},
	{31,	939,	ALL,	"divweo[.]",	0,	0},
	{31,	12,	ALL,	"lxsiwzx",	0,	0},
	{31,	76,	ALL,	"lxsiwax",	0,	0},
	{31,	140,	ALL,	"stxsiwx",	0,	0},
	{31,	268,	ALL,	"lxvx",	0,	0},
	{31,	332,	ALL,	"lxvdsx",	0,	0},
	{31,	364,	ALL,	"lxvwsx",	0,	0},
	{31,	396,	ALL,	"stxvx",	0,	0},
	{31,	524,	ALL,	"lxsspx",	0,	0},
	{31,	588,	ALL,	"lxsdx",	0,	0},
	{31,	652,	ALL,	"stxsspx",	0,	0},
	{31,	716,	ALL,	"stxsdx",	0,	0},
	{31,	780,	ALL,	"lxvw4x",	0,	0},
	{31,	812,	ALL,	"lxvh8x",	0,	0},
	{31,	844,	ALL,	"lxvd2x",	0,	0},
	{31,	876,	ALL,	"lxvb16x",	0,	0},
	{31,	908,	ALL,	"stxvw4x",	0,	0},
	{31,	940,	ALL,	"stxvh8x",	0,	0},
	{31,	972,	ALL,	"stxvd2x",	0,	0},
	{31,	1004,	ALL,	"stxvb16x",	0,	0},
	{31,	13,	ALL,	"lxvrbx",	0,	0},
	{31,	45,	ALL,	"lxvrhx",	0,	0},
	{31,	77,	ALL,	"lxvrwx",	0,	0},
	{31,	109,	ALL,	"lxvrdx",	0,	0},
	{31,	141,	ALL,	"stxvrbx",	0,	0},
	{31,	173,	ALL,	"stxvrhx",	0,	0},
	{31,	205,	ALL,	"stxvrwx",	0,	0},
	{31,	237,	ALL,	"stxvrdx",	0,	0},
	{31,	269,	ALL,	"lxvl",	0,	0},
	{31,	301,	ALL,	"lxvll",	0,	0},
	{31,	333,	ALL,	"lxvpx",	0,	0},
	{31,	397,	ALL,	"stxvl",	0,	0},
	{31,	429,	ALL,	"stxvll",	0,	0},
	{31,	461,	ALL,	"stxvpx",	0,	0},
	{31,	781,	ALL,	"lxsibzx",	0,	0},
	{31,	813,	ALL,	"lxsihzx",	0,	0},
	{31,	909,	ALL,	"stxsibx",	0,	0},
	{31,	941,	ALL,	"stxsihx",	0,	0},
	{31,	78,	ALL,	"msgsndu",	0,	0},
	{31,	110,	ALL,	"msgclru",	0,	0},
	{31,	142,	ALL,	"msgsndp",	0,	0},
	{31,	174,	ALL,	"msgclrp",	0,	0},
	{31,	206,	ALL,	"msgsnd",	0,	0},
	{31,	238,	ALL,	"msgclr",	0,	0},
	{31,	302,	ALL,	"mfbhrbe",	0,	0},
	{31,	430,	ALL,	"clrbhrb",	0,	0},
	{31,	15,	0b11111,	"isel",	0,	0},
	{31,	177,	ALL,	"xxmfacc",	vra2s,	vr2},
	{31,	177,	ALL,	"xxmtacc",	vra2s,	vr2},
	{31,	177,	ALL,	"xxsetaccz",	vra2s,	vr2},
	{31,	338,	ALL,	"slbsync",	0,	0},
	{31,	850,	ALL,	"slbiag",	0,	0},
	{31,	51,	ALL,	"mfvsrd",	0,	0},
	{31,	115,	ALL,	"mfvsrwz",	0,	0},
	{31,	179,	ALL,	"mtvsrd",	0,	0},
	{31,	211,	ALL,	"mtvsrwa",	0,	0},
	{31,	243,	ALL,	"mtvsrwz",	0,	0},
	{31,	307,	ALL,	"mfvsrld",	0,	0},
	{31,	371,	ALL,	"mftb",	0,	0},
	{31,	403,	ALL,	"mtvsrws",	0,	0},
	{31,	435,	ALL,	"mtvsrdd",	0,	0},
	{31,	755,	ALL,	"darn",	0,	0},
	{31,	979,	ALL,	"slbfee.",	0,	0},
	{31,	532,	ALL,	"ldbrx",	0,	0},
	{31,	660,	ALL,	"stdbrx",	0,	0},
	{31,	789,	ALL,	"lwzcix",	0,	0},
	{31,	821,	ALL,	"lhzcix",	0,	0},
	{31,	853,	ALL,	"lbzcix",	0,	0},
	{31,	885,	ALL,	"ldcix",	0,	0},
	{31,	917,	ALL,	"stwcix",	0,	0},
	{31,	949,	ALL,	"sthcix",	0,	0},
	{31,	981,	ALL,	"stbcix",	0,	0},
	{31,	1013,	ALL,	"stdcix",	0,	0},
	{31,	22,	ALL,	"icbt",	0,	0},
	{31,	886,	ALL,	"msgsync",	0,	0},
	{31,	503,	ALL,	"spom",	0,	0},
	{31,	791,	ALL,	"lfdpx",	0,	0},
	{31,	855,	ALL,	"lfiwax",	0,	0},
	{31,	887,	ALL,	"lfiwzx",	0,	0},
	{31,	919,	ALL,	"stfdpx",	0,	0},
	{31,	983,	ALL,	"stfiwx",	0,	0},
	{31,	1015,	ALL,	"lqm",	0,	0},
	{31,	445<<1,	0b1111111110,	"extswsli[.]",	0,	0},
	{31,	122,	ALL,	"popcntb",	0,	0},
	{31,	154,	ALL,	"prtyw",	0,	0},
	{31,	186,	ALL,	"prtyd",	0,	0},
	{31,	282,	ALL,	"cdtbcd",	0,	0},
	{31,	314,	ALL,	"cbcdtd",	0,	0},
	{31,	378,	ALL,	"popcntw",	0,	0},
	{31,	506,	ALL,	"popcntd",	0,	0},
	{31,	538,	ALL,	"cnttzw[.]",	0,	0},
	{31,	570,	ALL,	"cnttzd[.]",	0,	0},
	{31,	59,	ALL,	"cntlzdm",	0,	0},
	{31,	155,	ALL,	"brw",	0,	0},
	{31,	187,	ALL,	"brd",	0,	0},
	{31,	219,	ALL,	"brh",	0,	0},
	{31,	571,	ALL,	"cnttzdm",	0,	0},
	{31,	156,	ALL,	"pdepd",	0,	0},
	{31,	188,	ALL,	"pextd",	0,	0},
	{31,	220,	ALL,	"cfuged",	0,	0},
	{31,	252,	ALL,	"bpermd",	0,	0},
	{31,	508,	ALL,	"cmpb",	0,	0},
	{31,	30,	ALL,	"wait",	0,	0},
	{56,	0,	0b111,	"lq",	0,	0},
	{57,	0,	ALL,	"lfdp",	0,	0},
	{57,	2,	ALL,	"lxsd",	0,	0},
	{57,	3,	ALL,	"lxssp",	0,	0},
	{59,	66,	0b1111111111,	"dscli[.]",	0,	0},
	{59,	98,	ALL,	"dscri[.]",	0,	0},
	{59,	194,	ALL,	"dtstdc",	0,	0},
	{59,	226,	ALL,	"dtstdg",	0,	0},
	{59,	2,	ALL,	"dadd[.]",	0,	0},
	{59,	34,	ALL,	"dmul[.]",	0,	0},
	{59,	130,	ALL,	"dcmpo",	0,	0},
	{59,	162,	ALL,	"dtstex",	0,	0},
	{59,	258,	ALL,	"dctdp[.]",	0,	0},
	{59,	290,	ALL,	"dctfix[.]",	0,	0},
	{59,	322,	ALL,	"ddedpd[.]",	0,	0},
	{59,	354,	ALL,	"dxex[.]",	0,	0},
	{59,	514,	ALL,	"dsub[.]",	0,	0},
	{59,	546,	ALL,	"ddiv[.]",	0,	0},
	{59,	642,	ALL,	"dcmpu",	0,	0},
	{59,	674,	ALL,	"dtstsf",	0,	0},
	{59,	770,	ALL,	"drsp[.]",	0,	0},
	{59,	802,	ALL,	"dcffix[.]",	0,	0},
	{59,	834,	ALL,	"denbcd[.]",	0,	0},
	{59,	866,	ALL,	"diex[.]",	0,	0},
	{59,	3,	0b11111111,	"dqua[.]",	0,	0},
	{59,	35,	0b11111111,	"drrnd[.]",	0,	0},
	{59,	67,	0b11111111,	"dquai[.]",	0,	0},
	{59,	99,	0b11111111,	"drintx[.]",	0,	0},
	{59,	227,	0b11111111,	"drintn[.]",	0,	0},
	{59,	675,	ALL,	"dtstsfi",	0,	0},
	{59,	2<<2,	0b1111111100,	"xvi8ger4pp",	0,	0},
	{59,	18<<2,	0b1111111100,	"xvf16ger2pp",	0,	0},
	{59,	26<<2,	0b1111111100,	"xvf32gerpp",	0,	0},
	{59,	34<<2,	0b1111111100,	"xvi4ger8pp",	0,	0},
	{59,	210<<2,	0b1111111100,	"xvf16ger2nn",	0,	0},
	{59,	218<<2,	0b1111111100,	"xvf32gernn",	0,	0},
	{59,	242<<2,	0b1111111100,	"xvbf16ger2nn",	0,	0},
	{59,	250<<2,	0b1111111100,	"xvf64gernn",	0,	0},
	{59,	3<<2,	0b1111111100,	"xvi8ger4",	0,	0},
	{59,	19<<2,	0b1111111100,	"xvf16ger2",	0,	0},
	{59,	27<<2,	0b1111111100,	"xvf32ger",	0,	0},
	{59,	35<<2,	0b1111111100,	"xvi4ger8",	0,	0},
	{59,	43<<2,	0b1111111100,	"xvi16ger2s",	0,	0},
	{59,	59<<2,	0b1111111100,	"xvf64ger",	0,	0},
	{59,	75<<2,	0b1111111100,	"xvi16ger2",	0,	0},
	{59,	99<<2,	0b1111111100,	"xvi8ger4spp",	0,	0},
	{59,	107<<2,	0b1111111100,	"xvi16ger2pp",	0,	0},
	{59,	51<<2,	0b1111111100,	"xvbf16ger2",	0,	0},
	{59,	99<<2,	0b1111111100,	"xvi8ger4spp",	0,	0},
	{59,	846,	ALL,	"fcfids[.]",	0,	0},
	{59,	974,	ALL,	"fcfidus[.]",	0,	0},
	{59,	26,	ALL,	"frsqrtes[.]",	0,	0},
	{60,	0,	~3,	"xsaddsp",	0,	0},
	{60,	32,	~3,	"xssubsp",	0,	0},
	{60,	64,	~3,	"xsmulsp",	0,	0},
	{60,	96,	~3,	"xsdivsp",	0,	0},
	{60,	128,	~3,	"xsadddp",	0,	0},
	{60,	160,	~3,	"xssubdp",	0,	0},
	{60,	192,	~3,	"xsmuldp",	0,	0},
	{60,	224,	~3,	"xsdivdp",	0,	0},
	{60,	256,	~3,	"xvaddsp",	0,	0},
	{60,	288,	~3,	"xvsubsp",	0,	0},
	{60,	320,	~3,	"xvmulsp",	0,	0},
	{60,	352,	~3,	"xvdivsp",	0,	0},
	{60,	384,	~3,	"xvadddp",	0,	0},
	{60,	416,	~3,	"xvsubdp",	0,	0},
	{60,	448,	~3,	"xvmuldp",	0,	0},
	{60,	480,	~3,	"xvdivdp",	0,	0},
	{60,	512,	~3,	"xsmaxcdp",	0,	0},
	{60,	544,	~3,	"xsmincdp",	0,	0},
	{60,	576,	~3,	"xsmaxjdp",	0,	0},
	{60,	608,	~3,	"xsminjdp",	0,	0},
	{60,	640,	~3,	"xsmaxdp",	0,	0},
	{60,	672,	~3,	"xsmindp",	0,	0},
	{60,	704,	~3,	"xscpsgndp",	0,	0},
	{60,	768,	~3,	"xvmaxsp",	0,	0},
	{60,	800,	~3,	"xvminsp",	0,	0},
	{60,	832,	~3,	"xvcpsgnsp",	0,	0},
	{60,	864,	~3,	"xviexpsp",	0,	0},
	{60,	896,	~3,	"xvmaxdp",	0,	0},
	{60,	928,	~3,	"xvmindp",	0,	0},
	{60,	960,	~3,	"xvcpsgndp",	0,	0},
	{60,	992,	~3,	"xviexpdp",	0,	0},
	{60,	4,	~3,	"xsmaddasp",	0,	0},
	{60,	36,	~3,	"xsmaddmsp",	0,	0},
	{60,	68,	~3,	"xsmsubasp",	0,	0},
	{60,	100,	~3,	"xsmsubmsp",	0,	0},
	{60,	132,	~3,	"xsmaddadp",	0,	0},
	{60,	164,	~3,	"xsmaddmdp",	0,	0},
	{60,	196,	~3,	"xsmsubadp",	0,	0},
	{60,	228,	~3,	"xsmsubmdp",	0,	0},
	{60,	260,	~3,	"xvmaddasp",	0,	0},
	{60,	292,	~3,	"xvmaddmsp",	0,	0},
	{60,	324,	~3,	"xvmsubasp",	0,	0},
	{60,	356,	~3,	"xvmsubmsp",	0,	0},
	{60,	388,	~3,	"xvmaddadp",	0,	0},
	{60,	420,	~3,	"xvmaddmdp",	0,	0},
	{60,	452,	~3,	"xvmsubadp",	0,	0},
	{60,	484,	~3,	"xvmsubmdp",	0,	0},
	{60,	516,	~3,	"xsnmaddasp",	0,	0},
	{60,	548,	~3,	"xsnmaddmsp",	0,	0},
	{60,	580,	~3,	"xsnmsubasp",	0,	0},
	{60,	612,	~3,	"xsnmsubmsp",	0,	0},
	{60,	644,	~3,	"xsnmaddadp",	0,	0},
	{60,	676,	~3,	"xsnmaddmdp",	0,	0},
	{60,	708,	~3,	"xsnmsubadp",	0,	0},
	{60,	740,	~3,	"xsnmsubmdp",	0,	0},
	{60,	772,	~3,	"xvnmaddasp",	0,	0},
	{60,	804,	~3,	"xvnmaddmsp",	0,	0},
	{60,	836,	~3,	"xvnmsubasp",	0,	0},
	{60,	868,	~3,	"xvnmsubmsp",	0,	0},
	{60,	900,	~3,	"xvnmaddadp",	0,	0},
	{60,	932,	~3,	"xvnmaddmdp",	0,	0},
	{60,	964,	~3,	"xvnmsubadp",	0,	0},
	{60,	996,	~3,	"xvnmsubmdp",	0,	0},
	{60,	2<<2,	0b1001111100,	"xxsldwi",	0,	0},
	{60,	10<<2,	0b1001111100,	"xxpermdi",	0,	0},
	{60,	72,	~3,	"xxmrghw",	0,	0},
	{60,	104,	~3,	"xxperm",	0,	0},
	{60,	200,	~3,	"xxmrglw",	0,	0},
	{60,	232,	~3,	"xxpermr",	0,	0},
	{60,	130<<2,	~3,	"xxland",	0,	0},
	{60,	552,	~3,	"xxlandc",	0,	0},
	{60,	584,	~3,	"xxlor",	0,	0},
	{60,	616,	~3,	"xxlxor",	0,	0},
	{60,	648,	~3,	"xxlnor",	0,	0},
	{60,	680,	~3,	"xxlorc",	0,	0},
	{60,	712,	~3,	"xxlnand",	0,	0},
	{60,	744,	~3,	"xxleqv",	0,	0},
	{60,	164<<1,	~1,	"xxspltw",	0,	0},
	{60,	360,	ALL,	"xxspltib",	vra2s,	vr2},
	{60,	360,	ALL,	"lxvkq",	vra2s,	vr2},
	{60,	165<<1,	~1,	"xxextractuw",	0,	0},
	{60,	181<<1,	~1,	"xxinsertw",	0,	0},
	{60,	268,	0b111111100,	"xvcmpeqsp[.]",	0,	0},
	{60,	300,	0b111111100,	"xvcmpgtsp[.]",	0,	0},
	{60,	332,	0b111111100,	"xvcmpgesp[.]",	0,	0},
	{60,	396,	0b111111100,	"xvcmpeqdp[.]",	0,	0},
	{60,	428,	0b111111100,	"xvcmpgtdp[.]",	0,	0},
	{60,	460,	0b111111100,	"xvcmpgedp[.]",	0,	0},
	{60,	12,	~3,	"xscmpeqdp",	0,	0},
	{60,	44,	~3,	"xscmpgtdp",	0,	0},
	{60,	76,	~3,	"xscmpgedp",	0,	0},
	{60,	140,	~3,	"xscmpudp",	0,	0},
	{60,	172,	~3,	"xscmpodp",	0,	0},
	{60,	236,	~3,	"xscmpexpdp",	0,	0},
	{60,	72<<1,	~1,	"xscvdpuxws",	0,	0},
	{60,	88<<1,	~1,	"xscvdpsxws",	0,	0},
	{60,	136<<1,	~1,	"xvcvspuxws",	0,	0},
	{60,	304,	~1,	"xvcvspsxws",	0,	0},
	{60,	336,	~1,	"xvcvuxwsp",	0,	0},
	{60,	368,	~1,	"xvcvsxwsp",	0,	0},
	{60,	400,	~1,	"xvcvdpuxws",	0,	0},
	{60,	432,	~1,	"xvcvdpsxws",	0,	0},
	{60,	464,	~1,	"xvcvuxwdp",	0,	0},
	{60,	496,	~1,	"xvcvsxwdp",	0,	0},
	{60,	592,	~1,	"xscvuxdsp",	0,	0},
	{60,	624,	~1,	"xscvsxdsp",	0,	0},
	{60,	656,	~1,	"xscvdpuxds",	0,	0},
	{60,	688,	~1,	"xscvdpsxds",	0,	0},
	{60,	720,	~1,	"xscvuxddp",	0,	0},
	{60,	752,	~1,	"xscvsxddp",	0,	0},
	{60,	784,	~1,	"xvcvspuxds",	0,	0},
	{60,	816,	~1,	"xvcvspsxds",	0,	0},
	{60,	848,	~1,	"xvcvuxdsp",	0,	0},
	{60,	880,	~1,	"xvcvsxdsp",	0,	0},
	{60,	912,	~1,	"xvcvdpuxds",	0,	0},
	{60,	944,	~1,	"xvcvdpsxds",	0,	0},
	{60,	976,	~1,	"xvcvuxddp",	0,	0},
	{60,	1008,	~1,	"xvcvsxddp",	0,	0},
	{60,	73<<1,	~1,	"xsrdpi",	0,	0},
	{60,	89<<1,	~1,	"xsrdpiz",	0,	0},
	{60,	210,	~1,	"xsrdpip",	0,	0},
	{60,	242,	~1,	"xsrdpim",	0,	0},
	{60,	137<<1,	~1,	"xvrspi",	0,	0},
	{60,	153<<1,	~1,	"xvrspiz",	0,	0},
	{60,	169<<1,	~1,	"xvrspip",	0,	0},
	{60,	185<<1,	~1,	"xvrspim",	0,	0},
	{60,	201<<1,	~1,	"xvrdpi",	0,	0},
	{60,	217<<1,	~1,	"xvrdpiz",	0,	0},
	{60,	466,	~1,	"xvrdpip",	0,	0},
	{60,	498,	~1,	"xvrdpim",	0,	0},
	{60,	530,	~1,	"xscvdpsp",	0,	0},
	{60,	562,	~1,	"xsrsp",	0,	0},
	{60,	329<<1,	~1,	"xscvspdp",	0,	0},
	{60,	690,	~1,	"xsabsdp",	0,	0},
	{60,	722,	~1,	"xsnabsdp",	0,	0},
	{60,	754,	~1,	"xsnegdp",	0,	0},
	{60,	786,	~1,	"xvcvdpsp",	0,	0},
	{60,	818,	~1,	"xvabssp",	0,	0},
	{60,	850,	~1,	"xvnabssp",	0,	0},
	{60,	882,	~1,	"xvnegsp",	0,	0},
	{60,	457<<1,	~1,	"xvcvspdp",	0,	0},
	{60,	473<<1,	~1,	"xvabsdp",	0,	0},
	{60,	489<<1,	~1,	"xvnabsdp",	0,	0},
	{60,	505<<1,	~1,	"xvnegdp",	0,	0},
	{60,	61<<2,	~3,	"xstdivdp",	0,	0},
	{60,	93<<2,	~3,	"xvtdivsp",	0,	0},
	{60,	125<<2,	~3,	"xvtdivdp",	0,	0},
	{60,	(13<<6)|(5<<2),	0b1111011100,	"xvtstdcsp",	0,	0},
	{60,	(15<<6)|(5<<2),	0b1111011100,	"xvtstdcdp",	0,	0},
	{60,	10<<1,	~1,	"xsrsqrtesp",	0,	0},
	{60,	26<<1,	~1,	"xsresp",	0,	0},
	{60,	74<<1,	~1,	"xsrsqrtedp",	0,	0},
	{60,	90<<1,	~1,	"xsredp",	0,	0},
	{60,	106<<1,	~1,	"xstsqrtdp",	0,	0},
	{60,	138<<1,	~1,	"xvrsqrtesp",	0,	0},
	{60,	154<<1,	~1,	"xvresp",	0,	0},
	{60,	170<<1,	~1,	"xvtsqrtsp",	0,	0},
	{60,	202<<1,	~1,	"xvrsqrtedp",	0,	0},
	{60,	218<<1,	~1,	"xvredp",	0,	0},
	{60,	234<<1,	~1,	"xvtsqrtdp",	0,	0},
	{60,	298<<1,	~1,	"xststdcsp",	0,	0},
	{60,	362<<1,	~1,	"xststdcdp",	0,	0},
	{60,	916,	ALL,	"xxgenpcvbm",	0,	0},
	{60,	948,	ALL,	"xxgenpcvwm",	0,	0},
	{60,	917,	ALL,	"xxgenpcvhm",	0,	0},
	{60,	949,	ALL,	"xxgenpcvdm",	0,	0},
	{60,	11<<1,	~1,	"xssqrtsp",	0,	0},
	{60,	75<<1,	~1,	"xssqrtdp",	0,	0},
	{60,	107<<1,	~1,	"xsrdpic",	0,	0},
	{60,	139<<1,	~1,	"xvsqrtsp",	0,	0},
	{60,	171<<1,	~1,	"xvrspic",	0,	0},
	{60,	203<<1,	~1,	"xvsqrtdp",	0,	0},
	{60,	235<<1,	~1,	"xvrdpic",	0,	0},
	{60,	267<<1,	~1,	"xscvdpspn",	0,	0},
	{60,	331<<1,	~1,	"xscvspdpn",	0,	0},
	{60,	347<<1,	~1,	"xsxexpdp",	vra2s,	vr2},
	{60,	347<<1,	~1,	"xsxsigdp",	vra2s,	vr2},
	{60,	347<<1,	~1,	"xscvhpdp",	vra2s,	vr2},
	{60,	347<<1,	~1,	"xscvdphp",	vra2s,	vr2},
	{60,	475<<1,	~1,	"xvxexpdp",	vra2s,	vr2},
	{60,	475<<1,	~1,	"xvxsigdp",	vra2s,	vr2},
	{60,	475<<1,	~1,	"xvtlsbb",	vra2s,	vr2},
	{60,	475<<1,	~1,	"xxbrh",	vra2s,	vr2},
	{60,	475<<1,	~1,	"xvxexpsp",	vra2s,	vr2},
	{60,	475<<1,	~1,	"xvxsigsp",	vra2s,	vr2},
	{60,	475<<1,	~1,	"xxbrw",	vra2s,	vr2},
	{60,	475<<1,	~1,	"xvcvbf16sp",	vra2s,	vr2},
	{60,	475<<1,	~1,	"xvcvspbf16",	vra2s,	vr2},
	{60,	475<<1,	~1,	"xxbrd",	vra2s,	vr2},
	{60,	475<<1,	~1,	"xvcvhpsp",	vra2s,	vr2},
	{60,	475<<1,	~1,	"xvcvsphp",	vra2s,	vr2},
	{60,	475<<1,	~1,	"xxbrq",	vra2s,	vr2},
	{60,	918,	ALL,	"xsiexpdp",	0,	0},
	{60,	3<<3,	0b11000,	"xxsel",	0,	0},
	{61,	0,	0b11,	"stfdp",	0,	0},
	{61,	2,	0b11,	"stxsd",	0,	0},
	{61,	3,	0b11,	"stxssp",	0,	0},
	{61,	1,	ALL,	"lxv",	0,	0},
	{61,	5,	ALL,	"stxv",	0,	0},
	{63,	128,	ALL,	"ftdiv",	0,	0},
	{63,	160,	ALL,	"ftsqrt",	0,	0},
	{63,	66,	ALL,	"dscliq[.]",	0,	0},
	{63,	98,	ALL,	"dscriq[.]",	0,	0},
	{63,	194,	ALL,	"dtstdcq",	0,	0},
	{63,	226,	ALL,	"dtstdgq",	0,	0},
	{63,	2,	ALL,	"daddq[.]",	0,	0},
	{63,	34,	ALL,	"dmulq[.]",	0,	0},
	{63,	130,	ALL,	"dcmpoq",	0,	0},
	{63,	162,	ALL,	"dtstexq",	0,	0},
	{63,	258,	ALL,	"dctqpq[.]",	0,	0},
	{63,	290,	ALL,	"dctfixq[.]",	0,	0},
	{63,	322,	ALL,	"ddedpdq[.]",	0,	0},
	{63,	354,	ALL,	"dxexq[.]",	0,	0},
	{63,	514,	ALL,	"dsubq[.]",	0,	0},
	{63,	546,	ALL,	"ddivq[.]",	0,	0},
	{63,	642,	ALL,	"dcmpuq",	0,	0},
	{63,	674,	ALL,	"dtstsfq",	0,	0},
	{63,	770,	ALL,	"drdpq[.]",	0,	0},
	{63,	802,	ALL,	"dcffixq[.]",	0,	0},
	{63,	834,	ALL,	"denbcdq[.]",	0,	0},
	{63,	866,	ALL,	"diexq[.]",	0,	0},
	{63,	994,	ALL,	"dcffixqq",	vra2s,	vr2},
	{63,	994,	ALL,	"dctfixqq",	vra2s,	vr2},
	{63,	3,	0b11111111,	"dquaq[.]",	0,	0},
	{63,	35,	0b11111111,	"drrndq[.]",	0,	0},
	{63,	67,	0b11111111,	"dquaiq[.]",	0,	0},
	{63,	99,	0b11111111,	"drintxq[.]",	0,	0},
	{63,	227,	0b11111111,	"drintnq[.]",	0,	0},
	{63,	675,	ALL,	"dtstsfiq",	0,	0},
	{63,	4,	ALL,	"xsaddqp[o]",	0,	0},
	{63,	36,	ALL,	"xsmulqp[o]",	0,	0},
	{63,	68,	ALL,	"xscmpeqqp",	0,	0},
	{63,	100,	ALL,	"xscpsgnqp",	0,	0},
	{63,	132,	ALL,	"xscmpoqp",	0,	0},
	{63,	164,	ALL,	"xscmpexpqp",	0,	0},
	{63,	196,	ALL,	"xscmpgeqp",	0,	0},
	{63,	228,	ALL,	"xscmpgtqp",	0,	0},
	{63,	388,	ALL,	"xsmaddqp[o]",	0,	0},
	{63,	420,	ALL,	"xsmsubqp[o]",	0,	0},
	{63,	452,	ALL,	"xsnmaddqp[o]",	0,	0},
	{63,	484,	ALL,	"xsnmsubqp[o]",	0,	0},
	{63,	516,	ALL,	"xssubqp[o]",	0,	0},
	{63,	548,	ALL,	"xsdivqp[o]",	0,	0},
	{63,	644,	ALL,	"xscmpuqp",	0,	0},
	{63,	676,	ALL,	"xsmaxcqp",	0,	0},
	{63,	708,	ALL,	"xststdcqp",	0,	0},
	{63,	740,	ALL,	"xsmincqp",	0,	0},
	{63,	804,	ALL,	"xsabsqp",	vra2s,	vr2},
	{63,	804,	ALL,	"xsxexpqp",	vra2s,	vr2},
	{63,	804,	ALL,	"xsnabsqp",	vra2s,	vr2},
	{63,	804,	ALL,	"xsnegqp",	vra2s,	vr2},
	{63,	804,	ALL,	"xsxsigqp",	vra2s,	vr2},
	{63,	804,	ALL,	"xssqrtqp[o]",	vra2s,	vr2},
	{63,	836,	ALL,	"xscvqpuqz",	vra2s,	vr2},
	{63,	836,	ALL,	"xscvqpuwz",	vra2s,	vr2},
	{63,	836,	ALL,	"xscvudqp",	vra2s,	vr2},
	{63,	836,	ALL,	"xscvuqqp",	vra2s,	vr2},
	{63,	836,	ALL,	"xscvqpsqz",	vra2s,	vr2},
	{63,	836,	ALL,	"xscvqpswz",	vra2s,	vr2},
	{63,	836,	ALL,	"xscvsdqp",	vra2s,	vr2},
	{63,	836,	ALL,	"xscvsqqp",	vra2s,	vr2},
	{63,	836,	ALL,	"xscvqpudz",	vra2s,	vr2},
	{63,	868,	ALL,	"xsiexpqp",	0,	0},
	{63,	5,	0b11111111,	"xsrqpi[x]",	0,	0},
	{63,	37,	0b11111111,	"xsrqpxp",	0,	0},
	{63,	838,	ALL,	"fmrgow",	0,	0},
	{63,	966,	ALL,	"fmrgew",	0,	0},
	{63,	8,	ALL,	"fcpsgn[.]",	0,	0},
	{63,	392,	ALL,	"frin[.]",	0,	0},
	{63,	424,	ALL,	"friz[.]",	0,	0},
	{63,	456,	ALL,	"frip[.]",	0,	0},
	{63,	488,	ALL,	"frim[.]",	0,	0},
	{63,	142,	ALL,	"fctiwu[.]",	0,	0},
	{63,	942,	ALL,	"fctidu[.]",	0,	0},
	{63,	974,	ALL,	"fcfidu[.]",	0,	0},
	{63,	143,	ALL,	"fctiwuz[.]",	0,	0},
	{63,	943,	ALL,	"fctiduz[.]",	0,	0},
	{63,	24,	ALL,	"fre[.]",	0,	0},
	{59,	42<<2,	~3,	"xvi16ger2spp",	0,	0},
	{59,	50<<2,	~3,	"xvbf16ger2pp",	0,	0},
	{59,	58<<2,	~3,	"xvf64gerpp",	0,	0},
	{59,	82<<2,	~3,	"xvf16ger2np",	0,	0},
	{59,	90<<2,	~3,	"xvf32gernp",	0,	0},
	{59,	114<<2,	~3,	"xvbf16ger2np",	0,	0},
	{59,	122<<2,	~3,	"xvf64gernp",	0,	0},
	{59,	146<<2,	~3,	"xvf16ger2pn",	0,	0},
	{59,	154<<2,	~3,	"xvf32gerpn",	0,	0},
	{59,	178<<2,	~3,	"xvbf16ger2pn",	0,	0},
	{59,	186<<2,	~3,	"xvf64gerpn",	0,	0},

	{1,	0,	0,	"",	prefixed, 0},
	{0},
};

typedef struct Spr Spr;
struct Spr {
	int	n;
	char	*name;
};

static	Spr	sprname[] = {
	{0, "MQ"},
	{1, "XER"},
	{268, "TBL"},
	{269, "TBU"},
	{8, "LR"},
	{9, "CTR"},
	{528, "IBAT0U"},
	{529, "IBAT0L"},
	{530, "IBAT1U"},
	{531, "IBAT1L"},
	{532, "IBAT2U"},
	{533, "IBAT2L"},
	{534, "IBAT3U"},
	{535, "IBAT3L"},
	{536, "DBAT0U"},
	{537, "DBAT0L"},
	{538, "DBAT1U"},
	{539, "DBAT1L"},
	{540, "DBAT2U"},
	{541, "DBAT2L"},
	{542, "DBAT3U"},
	{543, "DBAT3L"},
	{25, "SDR1"},
	{19, "DAR"},
	{272, "SPRG0"},
	{273, "SPRG1"},
	{274, "SPRG2"},
	{275, "SPRG3"},
	{18, "DSISR"},
	{26, "SRR0"},
	{27, "SRR1"},
	{284, "TBLW"},
	{285, "TBUW"},	
	{22, "DEC"},
	{282, "EAR"},
	{1008, "HID0"},
	{1009, "HID1"},
	{976, "DMISS"},
	{977, "DCMP"},
	{978, "HASH1"},
	{979, "HASH2"},
	{980, "IMISS"},
	{981, "ICMP"},
	{982, "RPA"},
	{1010, "IABR"},
	{1013, "DABR"},
	{0,0},
};

static int
shmask(uvlong *m)
{
	int i;

	for(i=0; i<63; i++)
		if(*m & ((uvlong)1<<i))
			break;
	if(i > 63)
		return 0;
	if(*m & ~((uvlong)1<<i)){	/* more than one bit: do multiples of bytes */
		i = (i/8)*8;
		if(i == 0)
			return 0;
	}
	*m >>= i;
	return i;
}

static void
format(char *mnemonic, Instr *i, char *f)
{
	int n, s;
	ulong mask;
	uvlong vmask;

	if (mnemonic)
		format(0, i, mnemonic);
	if (f == 0)
		return;
	if (mnemonic)
		bprint(i, "\t");
	for ( ; *f; f++) {
		if (*f != '%') {
			bprint(i, "%c", *f);
			continue;
		}
		switch (*++f) {
		case 'a':
			bprint(i, "%d", i->ra);
			break;

		case 'b':
			bprint(i, "%d", i->rb);
			break;

		case 'c':
			bprint(i, "%d", i->frc);
			break;

		case 'B':
			bprint(i, "%llx", i->imm64);
			break;

		case 'd':
		case 's':
			bprint(i, "%d", i->rd);
			break;

		case 'C':
			if(i->rc)
				bprint(i, "CC");
			break;

		case 'D':
			if(i->rd & 3)
				bprint(i, "CR(INVAL:%d)", i->rd);
			else if(i->op == 63)
				bprint(i, "FPSCR(%d)", i->crfd);
			else
				bprint(i, "CR(%d)", i->crfd);
			break;

		case 'e':
			bprint(i, "%d", i->xsh);
			break;

		case 'E':
			switch(IBF(i->w[0],27,30)){	/* low bit is top bit of shift in rldiX cases */
			case 8:	i->mb = i->xmbe; i->me = 63; break;	/* rldcl */
			case 9:	i->mb = 0; i->me = i->xmbe; break;	/* rldcr */
			case 4: case 5:
					i->mb = i->xmbe; i->me = 63-i->xsh; break;	/* rldic */
			case 0: case 1:
					i->mb = i->xmbe; i->me = 63; break;	/* rldicl */
			case 2: case 3:
					i->mb = 0; i->me = i->xmbe; break;	/* rldicr */
			case 6: case 7:
					i->mb = i->xmbe; i->me = 63-i->xsh; break;	/* rldimi */
			}
			vmask = (~(uvlong)0>>i->mb) & (~(uvlong)0<<(63-i->me));
			s = shmask(&vmask);
			if(s)
				bprint(i, "(%llux<<%d)", vmask, s);
			else
				bprint(i, "%llux", vmask);
			break;

		case 'i':
			bprint(i, "$%d", i->simm);
			break;

		case 'I':
			bprint(i, "$%ux", i->uimm);
			break;

		case 'j':
			if(i->aa)
				pglobal(i, i->li, 1, "(SB)");
			else
				pglobal(i, i->addr+i->li, 1, "");
			break;

		case 'J':
			if(i->aa)
				pglobal(i, i->bd, 1, "(SB)");
			else
				pglobal(i, i->addr+i->bd, 1, "");
			break;

		case 'k':
			bprint(i, "%d", i->sh);
			break;

		case 'K':
			bprint(i, "$%x", i->imm);
			break;

		case 'L':
			if(i->lk)
				bprint(i, "L");
			break;

		case 'l':
			if(i->simm < 0)
				bprint(i, "-%x(R%d)", -i->simm, i->ra);
			else
				bprint(i, "%x(R%d)", i->simm, i->ra);
			break;

		case 'm':
			bprint(i, "%ux", i->crm);
			break;

		case 'M':
			bprint(i, "%ux", i->fm);
			break;

		case 'n':
			bprint(i, "%d", i->nb==0? 32: i->nb);	/* eg, pg 10-103 */
			break;

		case 'N':
			bprint(i, "%c", asstype==APOWER64 ? 'D' : 'W');
			break;

		case 'P':
			n = ((i->spr&0x1f)<<5)|((i->spr>>5)&0x1f);
			for(s=0; sprname[s].name; s++)
				if(sprname[s].n == n)
					break;
			if(sprname[s].name) {
				if(s < 10)
					bprint(i, sprname[s].name);
				else
					bprint(i, "SPR(%s)", sprname[s].name);
			} else
				bprint(i, "SPR(%d)", n);
			break;

		case 'Q':
			n = ((i->spr&0x1f)<<5)|((i->spr>>5)&0x1f);
			bprint(i, "%d", n);
			break;

		case 'S':
			if(i->ra & 3)
				bprint(i, "CR(INVAL:%d)", i->ra);
			else if(i->op == 63)
				bprint(i, "FPSCR(%d)", i->crfs);
			else
				bprint(i, "CR(%d)", i->crfs);
			break;

		case 'U':
			if(i->rc)
				bprint(i, "U");
			break;

		case 'V':
			if(i->oe)
				bprint(i, "V");
			break;

		case 'w':
			bprint(i, "[%lux]", i->w[0]);
			break;

		case 'W':
			if(!i->m64)
				break;
			/* sloppy */
			if(i->xo == 26 || IB(i->w[0], 10) == 0)
				bprint(i, "W");
			break;

		case 'Z':
			if(i->m64)
				bprint(i, "Z");
			break;

		case 'z':
			if(i->mb <= i->me)
				mask = ((ulong)~0L>>i->mb) & (~0L<<(31-i->me));
			else
				mask = ~(((ulong)~0L>>(i->me+1)) & (~0L<<(31-(i->mb-1))));
			bprint(i, "%lux", mask);
			break;

		case '\0':
			bprint(i, "%%");
			return;

		default:
			bprint(i, "%%%c", *f);
			break;
		}
	}
}

static int
printins(Map *map, uvlong pc, char *buf, int n)
{
	Instr i;
	Opcode *o;

	mymap = map;
	memset(&i, 0, sizeof(i));
	i.curr = buf;
	i.end = buf+n-1;
	if(mkinstr(pc, &i) < 0)
		return -1;
	for(o = opcodes; o->mnemonic != 0; o++)
		if(i.op == o->op && (i.xo & o->xomask) == o->xo) {
			if (o->f)
				(*o->f)(o, &i);
			else
				format(o->mnemonic, &i, o->ken);
			return i.size*4;
		}
	bprint(&i, "unknown %lux", i.w[0]);
	return i.size*4;
}

static int
powerinst(Map *map, uvlong pc, char modifier, char *buf, int n)
{
	USED(modifier);
	return printins(map, pc, buf, n);
}

static int
powerdas(Map *map, uvlong pc, char *buf, int n)
{
	Instr instr;
	int i;

	mymap = map;
	memset(&instr, 0, sizeof(instr));
	instr.curr = buf;
	instr.end = buf+n-1;
	if (mkinstr(pc, &instr) < 0)
		return -1;
	for(i = 0; instr.end-instr.curr > 8+1 && i < instr.size; i++){
		if(i != 0)
			*instr.curr++ = ' ';
		instr.curr = _hexify(instr.curr, instr.w[i], 7);
	}
	*instr.curr = 0;
	return instr.size*4;
}

static int
powerinstlen(Map *map, uvlong pc)
{
	Instr i;

	mymap = map;
	if (mkinstr(pc, &i) < 0)
		return -1;
	return i.size*4;
}

static int
powerfoll(Map *map, uvlong pc, Rgetter rget, uvlong *foll)
{
	char *reg;
	Instr i;

	mymap = map;
	if (mkinstr(pc, &i) < 0)
		return -1;
	foll[0] = pc+4;
	foll[1] = pc+4;
	switch(i.op) {
	default:
		return 1;

	case 18:	/* branch */
		foll[0] = i.li;
		if(!i.aa)
			foll[0] += pc;
		break;
			
	case 16:	/* conditional branch */
		foll[0] = i.bd;
		if(!i.aa)
			foll[0] += pc;
		break;

	case 19:	/* conditional branch to register */
		if(i.xo == 528)
			reg = "CTR";
		else if(i.xo == 16)
			reg = "LR";
		else
			return 1;	/* not a branch */
		foll[0] = (*rget)(map, reg);
		break;
	}
	if(i.lk)
		return 2;
	return 1;
}
