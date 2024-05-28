/*
 * IGMPv2 - internet group management protocol (and MLDv1)
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include "ip.h"
#include "ipv6.h"

enum
{
	IGMP_IPHDRSIZE	= 20,		/* size of ip header */
	IGMP_HDRSIZE	= 8,		/* size of IGMP header */
	IP_IGMPPROTO	= 2,

	IGMPquery	= 1,
	IGMPreport	= 2,
	IGMPv2report	= 6,
	IGMPv2leave	= 7,

	IP_MLDPROTO	= HBH,		/* hop-by-hop header */

	MLDquery	= 130,
	MLDreport	= 131,
	MLDdone		= 132,

	MSPTICK		= 100,
	MAXTIMEOUT	= 10000/MSPTICK,	/* at most 10 secs for a response */

	NHASH		= 1<<5,
#define hashipa(a) (((a)[IPaddrlen-2] + (a)[IPaddrlen-1])%NHASH)
};

typedef struct IGMPpkt IGMPpkt;
struct IGMPpkt
{
	/* ip header */
	uchar	vihl;		/* Version and header length */
	uchar	tos;		/* Type of service */
	uchar	len[2];		/* packet length (including headers) */
	uchar	id[2];		/* Identification */
	uchar	frag[2];	/* Fragment information */
	uchar	Unused;	
	uchar	proto;		/* Protocol */
	uchar	cksum[2];	/* checksum of ip portion */
	uchar	src[4];		/* Ip source */
	uchar	dst[4];		/* Ip destination */

	/* igmp header */
	uchar	vertype;	/* version and type */
	uchar	resptime;
	uchar	igmpcksum[2];	/* checksum of igmp portion */
	uchar	group[4];	/* multicast group */

	uchar	payload[];
};

#define IGMPPKTSZ offsetof(IGMPpkt, payload[0])

typedef struct MLDpkt MLDpkt;
struct MLDpkt {
	IPV6HDR;

	uchar	type;
	uchar	code;
	uchar	cksum[2];
	uchar	delay[2];
	uchar	res[2];
	uchar	group[IPaddrlen];
	uchar	payload[];
};

#define MLDPKTSZ offsetof(MLDpkt, payload[0])

static uchar mldhbhopt[] = {
	ICMPv6,	/* NextHeader */
	0x00,	/* HeaderLength */
		0x05,		/* Option: Router Alert */
		0x02,		/* Length */
		0x00, 0x00,	/* MLD */

		0x01,		/* Option: PadN */
		0x00,		/* Length */
};

typedef struct Report Report;
struct Report
{
	Report	*next;
	Proto	*proto;
	Ipifc	*ifc;
	int	ifcid;
	int	timeout;	/* in MSPTICK's */
	Ipmulti	*multi;
};

typedef struct Priv Priv;
struct Priv
{
	QLock;
	Rendez	r;
	int	nreports;
	Report	*reports[NHASH];
};

static void
igmpsendreport(Fs *f, uchar *src, uchar *dst, uchar *group, int done)
{
	IGMPpkt *p;
	Block *bp;

	bp = allocb(IGMPPKTSZ);
	bp->wp += IGMPPKTSZ;
	p = (IGMPpkt*)bp->rp;
	memset(p, 0, IGMPPKTSZ);
	p->vihl = IP_VER4;
	memmove(p->src, src+IPv4off, IPv4addrlen);
	memmove(p->dst, dst+IPv4off, IPv4addrlen);
	p->vertype = (1<<4) | (done? IGMPv2leave: IGMPv2report);
	p->resptime = 0;
	p->proto = IP_IGMPPROTO;
	memmove(p->group, group+IPv4off, IPv4addrlen);
	hnputs(p->igmpcksum, ptclcsum(bp, IGMP_IPHDRSIZE, IGMP_HDRSIZE));
	ipoput4(f, bp, nil, 1, DFLTTOS, nil);	/* TTL of 1 */
}

static int
mldvalidgroup(uchar *group)
{
	if(ipismulticast(group) != V6)
		return 0;

	/*
	 * MLD messages are never sent for multicast addresses
	 * whos scope is 0 (reserved) or 1 (node-local).
	 * MLD messages ARE sent for multicast addresses whose
	 * scope is 2 (link-local) ... except all-nodes address.
	 */
	if((group[1] & 0xF) < Link_local_scop
	|| ipcmp(group, v6allnodesL) == 0)
		return 0;

	return 1;
}

static void
mldsendreport(Fs *f, uchar *src, uchar *dst, uchar *group, int done)
{
	MLDpkt *p;
	Block *bp;

	if(!islinklocal(src))
		return;

	if(!mldvalidgroup(group))
		return;

	bp = allocb(sizeof(mldhbhopt)+MLDPKTSZ);
	bp->wp += sizeof(mldhbhopt)+MLDPKTSZ;
	bp->rp += sizeof(mldhbhopt);
	p = (MLDpkt*)bp->rp;
	memset(p, 0, MLDPKTSZ);
	ipmove(p->src, src);
	ipmove(p->dst, dst);
	p->type = done? MLDdone: MLDreport;
	p->code = 0;
	ipmove(p->group, group);

	/* generate checksum */
	hnputl(p->vcf, 0);
	hnputs(p->ploadlen, MLDPKTSZ-IP6HDR);
	p->proto = 0;
	p->ttl = ICMPv6;	/* ttl gets set later */
	hnputs(p->cksum, 0);
	hnputs(p->cksum, ptclcsum(bp, 0, MLDPKTSZ));

	/* add hop-by-hop option header */
	bp->rp -= sizeof(mldhbhopt);
	memmove(bp->rp, p, IP6HDR);
	memmove(bp->rp + IP6HDR, mldhbhopt, sizeof(mldhbhopt));
	p = (MLDpkt*)bp->rp;
	p->proto = IP_MLDPROTO;
	hnputs(p->ploadlen, BLEN(bp) - IP6HDR);
	
	ipoput6(f, bp, nil, 1, DFLTTOS, nil);	/* TTL of 1 */
}

static void
sendreport(Proto *pr, uchar *ia, uchar *group, int done)
{
	switch(pr->ipproto){
	case IP_IGMPPROTO:
		igmpsendreport(pr->f, ia, group, group, done);
		break;
	case IP_MLDPROTO:
		mldsendreport(pr->f, ia, group, group, done);
		break;
	}
}

static int
isreport(void *a)
{
	return ((Priv*)a)->nreports != 0;
}

static void
igmpproc(void *a)
{
	Proto *igmp = a;
	Priv *priv = igmp->priv;
	Report *list, *rp, **lrp;
	uint h;

	for(;;){
		sleep(&priv->r, isreport, priv);
		for(;;){
			qlock(priv);
			if(priv->nreports == 0)
				break;
	
			/* time out reports and put them in a list */
			list = nil;
			for(h = 0; h < NHASH; h++){
				lrp = &priv->reports[h];
				while((rp = *lrp) != nil){
					if(rp->timeout > 1 && nrand(rp->timeout) != 0){
						lrp = &rp->next;
						rp->timeout--;
						continue;
					}
					*lrp = rp->next;
					rp->next = list;
					list = rp;
					priv->nreports--;
				}
			}
			qunlock(priv);

			/* send all timed out reports */
			while((rp = list) != nil){
				list = rp->next;
				rp->next = nil;

				if(!waserror()){
					sendreport(rp->proto, rp->multi->ia, rp->multi->ma, 0);
					poperror();
				}
				free(rp->multi);
				free(rp);
			}

			tsleep(&up->sleep, return0, 0, MSPTICK);
		}
		qunlock(priv);
	}
}

static void
queuereport(Proto *pr, Ipifc *ifc, uchar *group, int timeout)
{
	Priv *priv = pr->priv;
	Ipmulti *mp, *xp;
	Report *rp;
	uint h;

	if(timeout < 1 || timeout > MAXTIMEOUT)
		timeout = MAXTIMEOUT;

	for(mp = ipifcgetmulti(pr->f, ifc, group); mp != nil; mp = xp){
		group = mp->ma;
		xp = mp->next;
		mp->next = nil;

		h = hashipa(group);
		qlock(priv);
		for(rp = priv->reports[h]; rp != nil; rp = rp->next){
			if(rp->proto == pr
			&& rp->ifc == ifc && rp->ifcid == ifc->ifcid
			&& ipcmp(rp->multi->ma, group) == 0)
				break;
		}
		if(rp != nil){
			/*
			 * already reporting this group on this interface,
			 * only update the timeout when it is shorter.
			 */
			if(timeout < rp->timeout)
				rp->timeout = timeout;
		Skip:
			qunlock(priv);
			free(mp);
			continue;
		}
		rp = malloc(sizeof(Report));
		if(rp == nil)
			goto Skip;

		rp->proto = pr;
		rp->ifc = ifc;
		rp->ifcid = ifc->ifcid;
		rp->timeout = timeout;
		rp->multi = mp;

		rp->next = priv->reports[h];
		priv->reports[h] = rp;
		if(priv->nreports++ == 0)
			wakeup(&priv->r);
		qunlock(priv);
	}
}

static void
purgereport(Proto *pr, Ipifc *ifc, uchar *group)
{
	Priv *priv = pr->priv;
	Report *rp, **lrp;
	uint h;

	h = hashipa(group);
	qlock(priv);
	for(lrp = &priv->reports[h]; (rp = *lrp) != nil; lrp = &rp->next){
		if(rp->proto == pr
		&& rp->ifc == ifc && rp->ifcid == ifc->ifcid
		&& ipcmp(rp->multi->ma, group) == 0){
			*lrp = rp->next;
			rp->next = nil;

			priv->nreports--;
			break;
		}
	}
	qunlock(priv);

	if(rp == nil)
		return;

	free(rp->multi);
	free(rp);
}

static void
mldiput(Proto *mld, Ipifc *ifc, Block *bp)
{
	MLDpkt *p;
	uchar *opt, *payload;

	p = (MLDpkt*)(bp->rp);
	/* check we have the hop-by-hop header */
	if((p->vcf[0] & 0xF0) != IP_VER6 || p->proto != IP_MLDPROTO)
		goto error;
	if(p->ttl != 1 || !isv6mcast(p->dst) || !islinklocal(p->src))
		goto error;

	/* strip the hop-by-hop option header */
	if(BLEN(bp) < IP6HDR+sizeof(mldhbhopt))
		goto error;
	opt = bp->rp + IP6HDR;
	if(opt[0] != ICMPv6)
		goto error;
	payload = opt + ((int)opt[1] + 1)*8;
	if(payload >= bp->wp)
		goto error;
	if(memcmp(opt+2, mldhbhopt+2, 4) != 0)
		goto error;
	memmove(payload-IP6HDR, bp->rp, IP6HDR);
	bp->rp = payload - IP6HDR;
	if(BLEN(bp) < MLDPKTSZ)
		goto error;
	p = (MLDpkt*)bp->rp;

	/* verify ICMPv6 checksum */
	hnputl(p->vcf, 0);  	/* borrow IP header as pseudoheader */
	p->ttl = ICMPv6;
	p->proto = 0;
	hnputs(p->ploadlen, MLDPKTSZ-IP6HDR);
	if(ptclcsum(bp, 0, MLDPKTSZ))
		goto error;
	if(ipcmp(p->group, IPnoaddr) != 0 && !mldvalidgroup(p->group))
		goto error;

	switch(p->type){
	case MLDquery:
		queuereport(mld, ifc, p->group, nhgets(p->delay)/MSPTICK);
		break;
	case MLDreport:
		purgereport(mld, ifc, p->group);
		break;
	}
error:
	freeblist(bp);
}

static void
igmpiput(Proto *igmp, Ipifc *ifc, Block *bp)
{
	uchar group[IPaddrlen];
	IGMPpkt *p;

	p = (IGMPpkt*)bp->rp;
	if((p->vihl & 0xF0) != IP_VER4)
		goto error;
	if(BLEN(bp) < IGMP_IPHDRSIZE+IGMP_HDRSIZE)
		goto error;
	if((p->vertype>>4) != 1)
		goto error;
	if(ptclcsum(bp, IGMP_IPHDRSIZE, IGMP_HDRSIZE))
		goto error;

	v4tov6(group, p->group);
	if(ipcmp(group, v4prefix) != 0 && ipismulticast(group) != V4)
		goto error;

	switch(p->vertype & 0xF){
	case IGMPquery:
		queuereport(igmp, ifc, group, p->resptime);
		break;
	case IGMPreport:
	case IGMPv2report:
		purgereport(igmp, ifc, group);
		break;
	}
error:
	freeblist(bp);
}

static void
multicastreport(Fs *f, Ipifc *ifc, uchar *ma, uchar *ia, int done)
{
	Proto *pr = Fsrcvpcolx(f, isv4(ma)? IP_IGMPPROTO: IP_MLDPROTO);
	purgereport(pr, ifc, ma);
	sendreport(pr, ia, ma, done);
}

void
igmpinit(Fs *f)
{
	Proto *igmp, *mld;

	igmp = smalloc(sizeof(Proto));
	igmp->priv = smalloc(sizeof(Priv));
	igmp->name = "igmp";
	igmp->connect = nil;
	igmp->announce = nil;
	igmp->ctl = nil;
	igmp->state = nil;
	igmp->close = nil;
	igmp->rcv = igmpiput;
	igmp->stats = nil;
	igmp->ipproto = IP_IGMPPROTO;
	igmp->nc = 0;
	igmp->ptclsize = 0;
	Fsproto(f, igmp);

	mld = smalloc(sizeof(Proto));
	mld->priv = igmp->priv;
	mld->name = "mld";
	mld->connect = nil;
	mld->announce = nil;
	mld->ctl = nil;
	mld->state = nil;
	mld->close = nil;
	mld->rcv = mldiput;
	mld->stats = nil;
	mld->ipproto = IP_MLDPROTO;
	mld->nc = 0;
	mld->ptclsize = 0;
	Fsproto(f, mld);

	multicastreportfn = multicastreport;
	kproc("igmpproc", igmpproc, igmp);
}
