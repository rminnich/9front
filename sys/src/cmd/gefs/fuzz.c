#include <u.h>
#include <libc.h>
#include <avl.h>
#include <fcall.h>
#include <bio.h>

#include "dat.h"
#include "fns.h"

typedef struct Fuzz	Fuzz;
typedef struct Shadow	Shadow;

struct Shadow {
	Avl;
	Kvp;
	char buf[Kvmax];
};

struct Fuzz {
	Mount *mnt;
	int nops;
};

Fuzz	*fz;
Avltree	*shadow;
Lock	shadowlk;
int	nshadow;

static uvlong
vrand(void)
{
	uvlong v;
	v = nrand(1<<4);
	v = (v<<30) | nrand(1<<30);
	v = (v<<30) | nrand(1<<30);
	return v;
}

static void
fail(char *fmt, ...)
{
	Biobuf *bfd;
	va_list ap;

	va_start(ap, fmt);
	vfprint(2, fmt, ap);
	va_end(ap);
	fprint(2, "seed: %lux\n", fuzzseed);
	fprint(2, "trace: /tmp/fuzz.trace");
	bfd = Bopen("/tmp/fuzz.trace", OWRITE);
	writetrace(bfd, -1);
	Bterm(bfd);
	postnote(PNGROUP, getpid(), "kill");
	abort();
}

static void
genname(char *name, int *nname)
{
	int n, i;
	char c;

	n = 1 + nrand(244);
	for(i = 0; i < n; i++){
		do {
			c = nrand(256);
		} while(c == '/' || c == '\0');
		name[i] = c;
	}
	*nname = n;
}

static void
genent(char *k, int *nk)
{
	char name[256], *p;
	vlong pqid;
	int nname;

	pqid = (vlong)nrand(1<<30) << 32 | nrand(1<<30);
	genname(name, &nname);
	name[nname] = '\0';
	p = packdkey(k, Keymax, pqid, name);
	*nk = p - k;
}

static void
gendir(char *v, short *nv)
{
	char *p;
	Xdir d;

	memset(&d, 0, sizeof(d));
	d.flag = 0;
	d.qid.path = vrand();
	d.qid.vers = nrand(1<<20);
	d.qid.type = nrand(2) ? QTFILE : QTDIR;
	d.mode = nrand(2) ? 0644 : (DMDIR|0755);
	d.atime = vrand();
	d.mtime = vrand();
	d.length = nrand(1<<20);
	d.uid = nrand(100);
	d.gid = nrand(100);
	d.muid = nrand(100);

	p = packdval(v, Inlmax, &d);
	*nv = p - v;
}

static void
genins(Msg *m, char *kbuf, char *vbuf)
{
	m->op = Oinsert;
	m->k = kbuf;
	genent(kbuf, &m->nk);
	m->v = vbuf;
	gendir(vbuf, &m->nv);
}

static void
gendel(Msg *m, Key *k, char *kbuf)
{
	memcpy(kbuf, k->k, k->nk);
	m->op = Odelete;
	m->k = kbuf;
	m->nk = k->nk;
	m->v = nil;
	m->nv = 0;
}

static void
genwstat(Msg *m, Key *k, char *kbuf, char *vbuf)
{
	char *p, flags;
	vlong size, mtime, atime;
	ulong mode;
	int uid, gid, muid;

	memcpy(kbuf, k->k, k->nk);
	m->op = Owstat;
	m->k = kbuf;
	m->nk = k->nk;
	m->v = vbuf;

	p = vbuf;
	do {
		flags = nrand(128);
	} while(flags == 0);
	*p++ = flags;
	if(flags & Owsize){
		size = nrand(1<<20);
		PACK64(p, size);
		p += 8;
	}
	if(flags & Owmode){
		mode = nrand(2) ? 0644 : (DMDIR|0755);
		PACK32(p, mode);
		p += 4;
	}
	if(flags & Owmtime){
		mtime = vrand();
		PACK64(p, mtime);
		p += 8;
	}
	if(flags & Owatime){
		atime = vrand();
		PACK64(p, atime);
		p += 8;
	}
	if(flags & Owuid){
		uid = nrand(100);
		PACK32(p, uid);
		p += 4;
	}
	if(flags & Owgid){
		gid = nrand(100);
		PACK32(p, gid);
		p += 4;
	}
	if(flags & Owmuid){
		muid = nrand(100);
		PACK32(p, muid);
		p += 4;
	}

	m->nv = p - vbuf;
}

static int
shadowcmp(Avl *a, Avl *b)
{
	Shadow *sa, *sb;

	sa = (Shadow*)a;
	sb = (Shadow*)b;
	return keycmp(sa, sb);
}

static Shadow*
pickrand(void)
{
	Avl *a, *c;
	int n, i;

	if(shadow->root == nil)
		return nil;
	n = nrand(nshadow);
	a = shadow->root;
	i = nshadow;
	while(i > 1){
		i /= 2;
		c = (n < i) ? a->c[0] : a->c[1];
		if(c == nil)
			break;
		a = c;
	}
	return (Shadow*)a;
}

static int
shadowins(Avltree *t, Msg *m)
{
	Shadow probe, *s, *old;

	probe.k = probe.buf;
	probe.v = probe.buf;
	cpkey(&probe, m, probe.buf, sizeof(probe.buf));
	old = (Shadow*)avllookup(t, &probe, 0);
	if(old != nil){
		memmove(old->v, m->v, m->nv);
		old->nv = m->nv;
		return 0;
	}
	s = malloc(sizeof(Shadow));
	s->k = s->buf;
	s->v = s->buf;
	cpkvp(s, m, s->buf, sizeof(s->buf));
	avlinsert(t, s);
	return 1;
}

void
shadowdel(Avltree *t, Msg *m)
{
	Shadow probe, *s;

	probe.k = probe.buf;
	probe.v = probe.buf;
	cpkey(&probe, m, probe.buf, sizeof(probe.buf));
	s = (Shadow*)avllookup(t, &probe, 0);
	if(s != nil){
		avldelete(t, s);
		free(s);
	}
}

void
shadowwstat(Avltree *t, Msg *m)
{
	Shadow probe, *s;
	char *p, *v, flags;
	vlong qidpath, qidvers, length, atime, mtime;
	ulong mode, qidtype;
	int uid, gid, muid;

	probe.k = probe.buf;
	probe.v = probe.buf;
	cpkey(&probe, m, probe.buf, sizeof(probe.buf));
	s = (Shadow*)avllookup(t, &probe, 0);
	v = s->v;
	v += 8;
	qidpath = UNPACK64(v);  v += 8;
	qidvers = UNPACK32(v);  v += 4;
	qidtype = UNPACK8(v);   v += 1;
	mode = UNPACK32(v);     v += 4;
	atime = UNPACK64(v);    v += 8;
	mtime = UNPACK64(v);    v += 8;
	length = UNPACK64(v);   v += 8;
	uid = UNPACK32(v);      v += 4;
	gid = UNPACK32(v);      v += 4;
	muid = UNPACK32(v);     v += 4;
	assert(v == s->v + s->nv);

	p = m->v;
	flags = *p++;
	if(flags & Owsize){
		length = UNPACK64(p);
		p += 8;
	}
	if(flags & Owmode){
		mode = UNPACK32(p);
		qidtype = mode>>24;
		p += 4;
	}
	if(flags & Owmtime){
		mtime = UNPACK64(p);
		p += 8;
	}
	if(flags & Owatime){
		atime = UNPACK64(p);
		p += 8;
	}
	if(flags & Owuid){
		uid = UNPACK32(p);
		p += 4;
	}
	if(flags & Owgid){
		gid = UNPACK32(p);
		p += 4;
	}
	if(flags & Owmuid){
		muid = UNPACK32(p);
		p += 4;
	}
	assert(p == m->v + m->nv);

	qidvers++;

	v = s->v;
	PACK64(v, 0);           v += 8;
	PACK64(v, qidpath);     v += 8;
	PACK32(v, qidvers);     v += 4;
	PACK8(v, qidtype);      v += 1;
	PACK32(v, mode);        v += 4;
	PACK64(v, atime);       v += 8;
	PACK64(v, mtime);       v += 8;
	PACK64(v, length);      v += 8;
	PACK32(v, uid);         v += 4;
	PACK32(v, gid);         v += 4;
	PACK32(v, muid);        v += 4;

	s->nv = v - s->v;
}

static void
shadowapply(Msg *m)
{
	switch(m->op){
	case Oinsert:
		if(shadowins(shadow, m))
			nshadow++;
		break;
	case Odelete:
		shadowdel(shadow, m);
		nshadow--;
		break;
	case Owstat:
		shadowwstat(shadow, m);
		break;
	}
}

static int
deleted(Msg *m, int nm, Key *k)
{
	int i, del;

	if(k == nil)
		return 1;
	del = 0;
	for(i = 0; i < nm; i++){
		if(keycmp(&m[i], k) != 0)
			continue;
		if(m[i].op == Odelete)
			del = 1;
		if(m[i].op == Oinsert)
			del = 0;
	}
	return del;
}

static void
genrand(Msg *m, char *kbuf, char *vbuf, Msg *batch, int nbatch)
{
	Shadow *s;
	int op, d;

	s = pickrand();
	op = nrand(100);
	/* If deleted, we need to do an insert or a replacement */
	d = deleted(batch, nbatch, s);
	if(d) d += nrand(2);
	/* 40% chance of new insertion */
	if(s == nil || op < 40 || d == 1){
		genins(m, kbuf, vbuf);
		return;
	}
	/* 20% chance of clobber */
	if(op < 60 || d == 2){
		memmove(kbuf, s->k, s->nk);
		m->op = Oinsert;
		m->k = kbuf;
		m->nk = s->nk;
		m->v = vbuf;
		gendir(vbuf, &m->nv);
		return;
	}
	/* 20% of Owstat */
	if(op < 80){
		genwstat(m, s, kbuf, vbuf);
		return;
	}
	/* 20% chance of delete */
	if(op < 100){
		gendel(m, s, kbuf);
		return;
	}
	abort();
}

void
fzupsert(int tid)
{
	char kbuf[8][Keymax], vbuf[8][Inlmax];
	Msg m[8];
	Tree *r, t;
	int i, nm;

	/*
	 * A bit of a hack: to prevent tearing between the shadow
	 * tree and the real tree, we need to apply the messages
	 * while holding the shadow lock.
	 *
	 * However, because we want to scan concurrently with
	 * tree mutations, we can't hold the shadow lock across
	 * the upsert, so we need to keep the tree somewhere
	 * temporary as we do the update.
	 */
	lock(&shadowlk);
	memset(&t, 0, sizeof(Tree));
	r = agetp(&fz->mnt->root);
	lock(&r->lk);
	t.bp = r->bp;
	t.ht = r->ht;
	unlock(&r->lk);
	nm = 1 + nrand(nelem(m)-1);
	for(i = 0; i < nm; i++)
		genrand(&m[i], kbuf[i], vbuf[i], m, i);
	unlock(&shadowlk);

	if(waserror()){
		fprint(2, "btupsert failed, nm=%d: %s\n", nm, errmsg());
		fprint(2, "nops: %d, nshadow %d\n", fz->nops, nshadow);
		fprint(2, "current batch:\n");
		for(i = 0; i < nm; i++)
			fprint(2, "  msg[%d]: %M\n", i, &m[i]);
		fail("btupsert failure\n");
	}
	qlock(&fs->mutlk);
	epochstart(tid);
	btupsert(&t, m, nm);
	epochend(tid);
	qunlock(&fs->mutlk);
	poperror();

	lock(&shadowlk);
	for(i = 0; i < nm; i++)
		shadowapply(&m[i]);
	r = agetp(&fz->mnt->root);
	lock(&r->lk);
	r->bp = t.bp;
	r->ht = t.ht;
	unlock(&r->lk);
	fz->nops += nm;
	unlock(&shadowlk);
	epochclean();
}

void
fzwrite(int tid, void *)
{
	char bksp[512];
	int w;

	w = 0;
	memset(bksp, '\b', sizeof(bksp));
	bksp[sizeof(bksp)-1] = 0;
	while(1){
		fzupsert(tid);
		if(fz->nops % 1000 == 0){
			fprint(2, "%.*s", (w>0)?w:0, bksp);
			w = fprint(2, "[%d ops] %d entries\n", fz->nops, nshadow);
		}
	}
}

static void
look1(int nsamp, int tid)
{
	Shadow *s;
	char buf[Kvmax];
	Tree *t;
	Kvp kv;
	int i, ok;

	lock(&shadowlk);
	epochstart(tid);
	t = agetp(&fz->mnt->root);
	for(i = 0; i < nsamp; i++){
		if(nshadow == 0)
			break;
		s = pickrand();
		ok = btlookup(t, s, &kv, buf, sizeof(buf));
		if(!ok)
			fail("key %K not found in tree, nops: %d\n", &s->Key, fz->nops);
		if(kv.nv != s->nv || memcmp(kv.v, s->v, s->nv) != 0)
			fail("value mismatch for key %P <> %P \n", &kv, &s->Kvp);
	}
	epochend(tid);
	unlock(&shadowlk);
}

static void
scan1(int tid)
{
	Shadow *s;
	Tree *t;
	Scan sc;
	Kvp kv;
	int i;

	lock(&shadowlk);
	epochstart(tid);
	t = agetp(&fz->mnt->root);
	i = 0;
	btnewscan(&sc, nil, 0);
	btenter(t, &sc);
	for(s = (Shadow*)avlmin(shadow); s != nil; s = (Shadow*)avlnext(s)){
		if(!btnext(&sc, &kv))
			fail("shadow key %K not found in tree at i=%d\n", &s->Key, i);
		if(kv.nk != s->nk
		|| kv.nv != s->nv
		|| memcmp(kv.k, s->k, s->nk) != 0
		|| memcmp(kv.v, s->v, s->nv)){
			fprint(2, "kvp mismatch at i=%d, nops=%d, t=%B\n", i, fz->nops, t->bp);
			fprint(2, "shadow: %P\n", &s->Kvp);
			fprint(2, "tree:   %P\n", &kv);
			fail("kvp mismatch\n");
		}
		i++;
	}
	btexit(&sc);
	epochend(tid);
	unlock(&shadowlk);
}

void
fzscan(int tid, void *)
{
	while(1){
		/*
		 * Because we hold the lock for a long time while
		 * doing scans as the tree grows, give the writer
		 * proc more time to do its thing.
		 */
		sleep(1000);
		look1(100, tid);
		scan1(tid);
	}
}

void
fzinit(void)
{
	char buf[Kvmax];
	Shadow *s;
	Tree *empty, *t;
	Scan sc;
	Kvp kv;

	if(opensnap("fuzz", nil) != nil)
		sysfatal("fuzz snapshot already exists");

	if((empty = opensnap("empty", nil)) == nil)
		sysfatal("opensnap empty: %r");
	qlock(&fs->mutlk);
	tagsnap(empty, "fuzz", Lmut);
	qunlock(&fs->mutlk);
	closesnap(empty);

	shadow = avlcreate(shadowcmp);
	fz = emalloc(sizeof(Fuzz), 1);
	if((fz->mnt = getmount("fuzz")) == nil)
		sysfatal("getmount fuzz: %r");
	fz->nops = 0;

	srand(fuzzseed);
	buf[0] = Kent;
	btnewscan(&sc, buf, 1);
	t = agetp(&fz->mnt->root);
	btenter(t, &sc);
	while(btnext(&sc, &kv)){
		if(kv.k[0] != Kent)
			break;
		s = emalloc(sizeof(Shadow), 1);
		s->k = s->buf;
		s->v = s->buf;
		cpkvp(s, &kv, s->buf, sizeof(s->buf));
		avlinsert(shadow, s);
		nshadow++;
	}
	btexit(&sc);
}
