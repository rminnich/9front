#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

static char*
slurp(Tree *t, vlong path, vlong len)
{
	char *ret, buf[Offksz], kvbuf[Offksz + Ptrsz];
	vlong o;
	Blk *b;
	Bptr bp;
	Key k;
	Kvp kv;

	if((ret = malloc(len + 1)) == nil)
		error(Enomem);
	k.k = buf;
	k.nk = Offksz;
	for(o = 0; o < len; o += Blksz){
		k.k[0] = Kdat;
		PACK64(k.k+1, path);
		PACK64(k.k+9, o);
		if(!btlookup(t, &k, &kv, kvbuf, sizeof(kvbuf)))
			error(Esrch);
		bp = unpackbp(kv.v, kv.nv);
		b = getblk(bp, GBraw);
		if(len - o >= Blksz)
			memcpy(ret + o, b->buf, Blksz);
		else
			memcpy(ret + o, b->buf, len - o);
	}
	ret[len] = 0;
	return ret;
}

static char*
readline(char **p, char *buf, int nbuf)
{
	char *e;
	int n;

	if((e = strchr(*p, '\n')) == nil)
		return nil;
	n = (e - *p) + 1;
	if(n >= nbuf)
		n = nbuf - 1;
	strecpy(buf, buf + n, *p);
	*p = e+1;
	return buf;
}

static char*
getfield(char **p, char delim)
{
	char *r;

	if(*p == nil)
		return nil;
	r = *p;
	*p = strchr(*p, delim);
	if(*p != nil){
		**p = '\0';
		*p += 1;
	}
	return r;
}

User*
name2user(char *name)
{
	int i;

	for(i = 0; i < fs->nusers; i++)
		if(strcmp(fs->users[i].name, name) == 0)
			return &fs->users[i];
	return nil;
}

User*
uid2user(int id)
{
	int i;

	for(i = 0; i < fs->nusers; i++)
		if(fs->users[i].id == id)
			return &fs->users[i];
	return nil;
}

static char*
parseusers(int fd, char *udata)
{
	char *pu, *p, *f, *m, *err, buf[8192];
	int i, j, lnum, ngrp, nusers, usersz;
	User *u, *n, *users;
	int *g, *grp;

	i = 0;
	err = nil;
	nusers = 0;
	usersz = 8;
	if((users = calloc(usersz, sizeof(User))) == nil)
		return Enomem;
	pu = udata;
	lnum = 0;
	while((p = readline(&pu, buf, sizeof(buf))) != nil){
		lnum++;
		if(p[0] == '#' || p[0] == 0)
			continue;
		if(i == usersz){
			usersz *= 2;
			n = realloc(users, usersz*sizeof(User));
			if(n == nil){
				free(users);
				return Enomem;
			}
			users = n;
		}
		if((f = getfield(&p, ':')) == nil){
			fprint(fd, "/adm/users:%d: missing ':' after id\n", lnum);
			err = Esyntax;
			goto Error;
		}
		u = &users[i];
		u->id = atol(f);
		if((f = getfield(&p, ':')) == nil){
			fprint(fd, "/adm/users:%d: missing ':' after name\n", lnum);
			err = Esyntax;
			goto Error;
		}
		snprint(u->name, sizeof(u->name), "%s", f);
		u->lead = noneid;
		u->memb = nil;
		u->nmemb = 0;
		i++;
	}
	nusers = i;


	i = 0;
	pu = udata;
	lnum = 0;
	while((p = readline(&pu, buf, sizeof(buf))) != nil){
		lnum++;
		if(buf[0] == '#' || buf[0] == 0)
			continue;
		getfield(&p, ':');	/* skip id */
		getfield(&p, ':');	/* skip name */
		if((f = getfield(&p, ':')) == nil){
			fprint(fd, "/adm/users:%d: missing ':' after name\n", lnum);
			err = Esyntax;
			goto Error;
		}
		if(f[0] != '\0'){
			u = nil;
			for(j = 0; j < nusers; j++)
				if(strcmp(users[j].name, f) == 0)
					u = &users[j];
			if(u == nil){
				fprint(fd, "/adm/users:%d: leader %s does not exist\n", lnum, f);
				err = Enouser;
				goto Error;
			}
			if(u->id == noneid){
				fprint(fd, "/adm/users:%d: group leader may not be none\n", lnum);
				err = Esyntax;
				goto Error;
			}
			users[i].lead = u->id;
		}
		if((f = getfield(&p, ':')) == nil){
			err = Esyntax;
			goto Error;
		}
		grp = nil;
		ngrp = 0;
		while((m = getfield(&f, ',')) != nil){
			if(m[0] == '\0')
				continue;
			u = nil;
			for(j = 0; j < nusers; j++)
				if(strcmp(users[j].name, m) == 0)
					u = &users[j];
			if(u == nil){
				fprint(fd, "/adm/users:%d: user %s does not exist\n", lnum, m);
				free(grp);
				err = Enouser;
				goto Error;
			}
			if((g = realloc(grp, (ngrp+1)*sizeof(int))) == nil){
				free(grp);
				err = Enomem;
				goto Error;
			}
			grp = g;
			grp[ngrp++] = u->id;
		}
		users[i].memb = grp;
		users[i].nmemb = ngrp;
		i++;
	}

	wlock(&fs->userlk);
	n = fs->users;
	i = fs->nusers;
	fs->users = users;
	fs->nusers = nusers;
	wunlock(&fs->userlk);
	users = n;
	nusers = i;

Error:
	if(users != nil)
		for(i = 0; i < nusers; i++)
			free(users[i].memb);
	free(users);
		
	return err;
		
}

void
loadusers(int fd, Tree *t)
{
	char *s, *e;
	vlong len;
	Qid q;
	User *u;

	if(walk1(t, -1, "", &q, &len) == -1)
		error(Efs);
	if(walk1(t, q.path, "users", &q, &len) == -1)
		error(Esrch);
	if(q.type & QTDIR)
		error(Etype);
	if(len >= 1*MiB)
		error(Efsize);
	s = slurp(t, q.path, len);
	e = parseusers(fd, s);
	if(e != nil){
		if(fs->users != nil){
			fprint(2, "load users: %s\n", e);
			fprint(2, "keeping old table\n");
			error(e);
		}
		if(!permissive){
			fprint(2, "user table broken: %s\n", e);
			fprint(2, "\tnot permissive: bailing\n");
			error(e);
		}
		fprint(2, "user table broken: %s\n", e);
		fprint(2, "\tfalling back to default\n");
		parseusers(fd, "-1:adm::\n0:none::\n");
	}
	if((u = name2user("none")) != nil)
		noneid = u->id;
	if((u = name2user("adm")) != nil)
		admid = u->id;
	if((u = name2user("nogroup")) != nil)
		nogroupid = u->id;
	free(s);
}
