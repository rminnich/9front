/*
 * virtio 1.0 ethernet driver
 * http://docs.oasis-open.org/virtio/virtio/v1.0/virtio-v1.0.html
 *
 * In contrast to ethervirtio.c, this driver handles the non-legacy
 * interface for virtio ethernet which uses mmio for all register accesses
 * and requires a laborate pci capability structure dance to get working.
 *
 * It is kind of pointless as it is most likely slower than
 * port i/o (harder to emulate on the pc platform).
 * 
 * The reason why this driver is needed it is that vultr set the
 * disable-legacy=on option in the -device parameter for qemu
 * on their hypervisor.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "../port/error.h"
#include "../port/netif.h"
#include "../port/etherif.h"
#include "../port/virtio10.h"

typedef struct Vheader Vheader;
typedef struct Vqueue Vqueue;

typedef struct Ctlr Ctlr;

enum {
	/* flags in Qnetstatus */
	Nlinkup = (1<<0),
	Nannounce = (1<<1),

	/* feat[0] bits */
	Fmac = 1<<5,
	Fstatus = 1<<16,
	Fctrlvq = 1<<17,
	Fctrlrx = 1<<18,

	/* vring used flags */
	Unonotify = 1,
	/* vring avail flags */
	Rnointerrupt = 1,

	/* descriptor flags */
	Dnext = 1,
	Dwrite = 2,
	Dindirect = 4,

	/* struct sizes */
	VringSize = 4,
	VdescSize = 16,
	VusedSize = 8,
	VheaderSize = 12,

	Vrxq	= 0,
	Vtxq	= 1,
	Vctlq	= 2,

	/* class/cmd for Vctlq */
	CtrlRx	= 0x00,
		CmdPromisc	= 0x00,
		CmdAllmulti	= 0x01,
	CtrlMac	= 0x01,
		CmdMacTableSet	= 0x00,
	CtrlVlan= 0x02,
		CmdVlanAdd	= 0x00,
		CmdVlanDel	= 0x01,
};

enum
{
	Vnet_mac0 = 0,
	Vnet_status = 6,
	Vnet_sz = 8
};

struct Vheader
{
	u8int	flags;
	u8int	segtype;
	u16int	hlen;
	u16int	seglen;
	u16int	csumstart;
	u16int	csumend;
};

struct Vqueue
{
	Rendez;

	uint	qsize;
	uint	qmask;

	Vdesc	*desc;

	Vring	*avail;
	u16int	*availent;
	u16int	*availevent;

	Vring	*used;
	Vused	*usedent;
	u16int	*usedevent;
	u16int	lastused;

	uint	nintr;
	uint	nnote;

	/* notify register */
	Vio	notify;
};

struct Ctlr {
	Lock;

	QLock	ctllock;

	int	attached;

	/* registers */
	Vio	cfg;
	Vio 	dev;
	Vio	isr;
	Vio	notify;
	u32int	notifyoffmult;

	uvlong	port;
	Pcidev	*pcidev;
	Ctlr	*next;
	int	active;
	ulong	feat[2];
	int	nqueue;

	Bpool	pool;

	/* virtioether has 3 queues: rx, tx and ctl */
	Vqueue	queue[3];
};

static Ctlr *ctlrhead;

static int
vhasroom(void *v)
{
	Vqueue *q = v;
	return q->lastused != q->used->idx;
}

static void
vqnotify(Ctlr *ctlr, int x)
{
	Vqueue *q;

	coherence();
	q = &ctlr->queue[x];
	if(q->used->flags & Unonotify)
		return;
	q->nnote++;
	vout16(&q->notify, 0, x);
}

static void
txproc(void *v)
{
	Vheader *header;
	Block **blocks;
	Ether *edev;
	Ctlr *ctlr;
	Vqueue *q;
	Vused *u;
	Block *b;
	int i, j;

	edev = v;
	ctlr = edev->ctlr;
	q = &ctlr->queue[Vtxq];

	header = smalloc(VheaderSize);
	blocks = smalloc(sizeof(Block*) * (q->qsize/2));

	for(i = 0; i < q->qsize/2; i++){
		j = i << 1;
		q->desc[j].addr = PADDR(header);
		q->desc[j].len = VheaderSize;
		q->desc[j].next = j | 1;
		q->desc[j].flags = Dnext;

		q->availent[i] = q->availent[i + q->qsize/2] = j;

		j |= 1;
		q->desc[j].next = 0;
		q->desc[j].flags = 0;
	}

	q->avail->flags &= ~Rnointerrupt;

	while(waserror())
		;

	while((b = qbread(edev->oq, 1000000)) != nil){
		for(;;){
			/* retire completed packets */
			while((i = q->lastused) != q->used->idx){
				u = &q->usedent[i & q->qmask];
				i = (u->id & q->qmask) >> 1;
				if(blocks[i] == nil)
					break;
				freeb(blocks[i]);
				blocks[i] = nil;
				q->lastused++;
			}

			/* have free slot? */
			i = q->avail->idx & (q->qmask >> 1);
			if(blocks[i] == nil)
				break;

			/* ring full, wait and retry */
			if(!vhasroom(q))
				sleep(q, vhasroom, q);
		}

		/* slot is free, fill in descriptor */
		blocks[i] = b;
		j = (i << 1) | 1;
		q->desc[j].addr = PADDR(b->rp);
		q->desc[j].len = BLEN(b);
		coherence();
		q->avail->idx++;
		vqnotify(ctlr, Vtxq);
	}

	pexit("ether out queue closed", 1);
}

static void
rxproc(void *v)
{
	Vheader *header;
	Block **blocks;
	Ether *edev;
	Ctlr *ctlr;
	Vqueue *q;
	Vused *u;
	Block *b;
	int i, j;

	edev = v;
	ctlr = edev->ctlr;
	q = &ctlr->queue[Vrxq];

	header = smalloc(VheaderSize);
	blocks = smalloc(sizeof(Block*) * (q->qsize/2));

	ctlr->pool.size = ETHERMAXTU;
	growbp(&ctlr->pool, q->qsize*2);

	for(i = 0; i < q->qsize/2; i++){
		j = i << 1;
		q->desc[j].addr = PADDR(header);
		q->desc[j].len = VheaderSize;
		q->desc[j].next = j | 1;
		q->desc[j].flags = Dwrite|Dnext;

		q->availent[i] = q->availent[i + q->qsize/2] = j;

		j |= 1;
		q->desc[j].next = 0;
		q->desc[j].flags = Dwrite;
	}

	q->avail->flags &= ~Rnointerrupt;

	while(waserror())
		;

	for(;;){
		/* replenish receive ring */
		do {
			i = q->avail->idx & (q->qmask >> 1);
			if(blocks[i] != nil)
				break;
			if((b = iallocbp(&ctlr->pool)) == nil)
				break;
			blocks[i] = b;
			j = (i << 1) | 1;
			q->desc[j].addr = PADDR(b->rp);
			q->desc[j].len = BALLOC(b);
			coherence();
			q->avail->idx++;
		} while(q->avail->idx != q->used->idx);
		vqnotify(ctlr, Vrxq);

		/* wait for any packets to complete */
		if(!vhasroom(q))
			sleep(q, vhasroom, q);

		/* retire completed packets */
		while((i = q->lastused) != q->used->idx) {
			u = &q->usedent[i & q->qmask];
			i = (u->id & q->qmask) >> 1;
			if((b = blocks[i]) == nil)
				break;

			blocks[i] = nil;
			b->wp = b->rp + u->len - VheaderSize;
			etheriq(edev, b);
			q->lastused++;
		}
	}
}

static int
vctlcmd(Ether *edev, uchar class, uchar cmd, uchar *data, int ndata)
{
	uchar hdr[2], ack[1];
	Ctlr *ctlr;
	Vqueue *q;
	Vdesc *d;
	int i;

	ctlr = edev->ctlr;
	q = &ctlr->queue[Vctlq];
	if(q->qsize < 3)
		return -1;

	qlock(&ctlr->ctllock);
	while(waserror())
		;

	ack[0] = 0x55;
	hdr[0] = class;
	hdr[1] = cmd;

	d = &q->desc[0];
	d->addr = PADDR(hdr);
	d->len = sizeof(hdr);
	d->next = 1;
	d->flags = Dnext;
	d++;
	d->addr = PADDR(data);
	d->len = ndata;
	d->next = 2;
	d->flags = Dnext;
	d++;
	d->addr = PADDR(ack);
	d->len = sizeof(ack);
	d->next = 0;
	d->flags = Dwrite;

	i = q->avail->idx & q->qmask;
	q->availent[i] = 0;
	coherence();

	q->avail->flags &= ~Rnointerrupt;
	q->avail->idx++;
	vqnotify(ctlr, Vctlq);
	while(!vhasroom(q))
		sleep(q, vhasroom, q);
	q->lastused = q->used->idx;
	q->avail->flags |= Rnointerrupt;

	qunlock(&ctlr->ctllock);
	poperror();

	if(ack[0] != 0)
		print("#l%d: vctlcmd: %ux.%ux -> %ux\n", edev->ctlrno, class, cmd, ack[0]);

	return ack[0];
}

static void
interrupt(Ureg*, void* arg)
{
	Ether *edev;
	Ctlr *ctlr;
	Vqueue *q;
	int i;

	edev = arg;
	ctlr = edev->ctlr;
	if(vin8(&ctlr->isr, 0) & 1){
		for(i = 0; i < ctlr->nqueue; i++){
			q = &ctlr->queue[i];
			if(vhasroom(q)){
				q->nintr++;
				wakeup(q);
			}
		}
	}
}

static void
attach(Ether* edev)
{
	char name[KNAMELEN];
	Ctlr* ctlr;
	int i;

	ctlr = edev->ctlr;
	ilock(ctlr);
	if(ctlr->attached){
		iunlock(ctlr);
		return;
	}
	ctlr->attached = 1;

	/* enable the queues */
	for(i = 0; i < ctlr->nqueue; i++){
		vout16(&ctlr->cfg, Vconf_queuesel, i);
		vout16(&ctlr->cfg, Vconf_queueenable, 1);
	}

	/* driver is ready */
	vout8(&ctlr->cfg, Vconf_status, vin8(&ctlr->cfg, Vconf_status) | Sdriverok);

	iunlock(ctlr);

	/* start kprocs */
	snprint(name, sizeof name, "#l%drx", edev->ctlrno);
	kproc(name, rxproc, edev);
	snprint(name, sizeof name, "#l%dtx", edev->ctlrno);
	kproc(name, txproc, edev);
}

static char*
ifstat(void *a, char *s, char *e)
{
	Ether *edev;
	Ctlr *ctlr;
	Vqueue *q;
	int i;

	if(s >= e)
		return s;
	edev = a;
	ctlr = edev->ctlr;
	s = seprint(s, e, "devfeat %32.32lub %32.32lub\n", ctlr->feat[1], ctlr->feat[0]);
	s = seprint(s, e, "devstatus %8.8ub\n", vin8(&ctlr->cfg, Vconf_status));
	for(i = 0; i < ctlr->nqueue; i++){
		q = &ctlr->queue[i];
		s = seprint(s, e,
			"vq%d %#p size %d avail->idx %d used->idx %d lastused %hud nintr %ud nnote %ud\n",
			i, q, q->qsize, q->avail->idx, q->used->idx, q->lastused, q->nintr, q->nnote);
	}
	return s;
}

static void
shutdown(Ether* edev)
{
	Ctlr *ctlr = edev->ctlr;

	coherence();
	vout8(&ctlr->cfg, Vconf_status, 0);
	coherence();

	pciclrbme(ctlr->pcidev);
}

static void
promiscuous(void *arg, int on)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	uchar b[1];

	if((ctlr->feat[0] & (Fctrlvq|Fctrlrx)) != (Fctrlvq|Fctrlrx))
		return;

	b[0] = on != 0;
	vctlcmd(edev, CtrlRx, CmdPromisc, b, sizeof(b));
}

static void
multicast(void *arg, uchar*, int)
{
	Ether *edev = arg;
	Ctlr *ctlr = edev->ctlr;
	uchar b[1];

    if((ctlr->feat[0] & (Fctrlvq|Fctrlrx)) != (Fctrlvq|Fctrlrx))
		return;

	b[0] = edev->nmaddr > 0;
	vctlcmd(edev, CtrlRx, CmdAllmulti, b, sizeof(b));
}

static int
initqueue(Vqueue *q, int size)
{
	uchar *p;

	q->desc = mallocalign(VdescSize*size, 16, 0, 0);
	if(q->desc == nil)
		return -1;
	p = mallocalign(VringSize + 2*size + 2, 2, 0, 0);
	if(p == nil){
FreeDesc:
		free(q->desc);
		q->desc = nil;
		return -1;
	}
	q->avail = (void*)p;
	p += VringSize;
	q->availent = (void*)p;
	p += sizeof(u16int)*size;
	q->availevent = (void*)p;
	p = mallocalign(VringSize + VusedSize*size + 2, 4, 0, 0);
	if(p == nil){
		free(q->avail);
		q->avail = nil;
		goto FreeDesc;
	}
	q->used = (void*)p;
	p += VringSize;
	q->usedent = (void*)p;
	p += VusedSize*size;
	q->usedevent = (void*)p;

	q->qsize = size;
	q->qmask = q->qsize - 1;

	q->lastused = q->avail->idx = q->used->idx = 0;

	q->avail->flags |= Rnointerrupt;

	return 0;
}

static int
matchvirtiocfgcap(Pcidev *p, int cap, int off, int typ)
{
	int bar;

	if(cap != 9 || pcicfgr8(p, off+3) != typ)
		return 1;

	/* skip invalid or non memory bars */
	bar = pcicfgr8(p, off+4);
	if(bar < 0 || bar >= nelem(p->mem) 
	|| p->mem[bar].size == 0)
		return 1;

	return 0;
}

static int
virtiocap(Pcidev *p, int typ)
{
	return pcienumcaps(p, matchvirtiocfgcap, typ);
}

static Ctlr*
pciprobe(void)
{
	Ctlr *c, *h, *t;
	Pcidev *p;
	Vio cfg;
	int bar,cap, n, i;
	uvlong mask;

	h = t = nil;

	/* §4.1.2 PCI Device Discovery */
	for(p = nil; p = pcimatch(p, 0x1AF4, 0x1041);){
		/* non-transitional devices will have a revision > 0 */
		if(p->rid == 0)
			continue;
		if((cap = virtiocap(p, 1)) < 0)
			continue;
		bar = pcicfgr8(p, cap+4) % nelem(p->mem);
		if(virtiomapregs(p, cap, Vconf_sz, &cfg) == nil)
			continue;
		if((c = mallocz(sizeof(Ctlr), 1)) == nil){
			print("ethervirtio: no memory for Ctlr\n");
			break;
		}
		c->cfg = cfg;
		c->pcidev = p;
		mask = p->mem[bar].bar&1?~0x3ULL:~0xFULL;
		c->port = p->mem[bar].bar & mask;

		pcienable(p);
		if(virtiomapregs(p, virtiocap(p, 4), Vnet_sz, &c->dev) == nil)
			goto Baddev;
		if(virtiomapregs(p, virtiocap(p, 3), 0, &c->isr) == nil)
			goto Baddev;
		cap = virtiocap(p, 2);
		if(virtiomapregs(p, cap, 0, &c->notify) == nil)
			goto Baddev;
		c->notifyoffmult = pcicfgr32(p, cap+16);

		/* device reset */
		coherence();
		vout8(&cfg, Vconf_status, 0);
		while(vin8(&cfg, Vconf_status) != 0)
			delay(1);
		vout8(&cfg, Vconf_status, Sacknowledge|Sdriver);

		/* negotiate feature bits */
		vout32(&cfg, Vconf_devfeatsel, 1);
		c->feat[1] = vin32(&cfg, Vconf_devfeat);

		vout32(&cfg, Vconf_devfeatsel, 0);
		c->feat[0] = vin32(&cfg, Vconf_devfeat);

		vout32(&cfg, Vconf_drvfeatsel, 1);
		vout32(&cfg, Vconf_drvfeat, c->feat[1] & Fversion1);

		vout32(&cfg, Vconf_drvfeatsel, 0);
		vout32(&cfg, Vconf_drvfeat, c->feat[0] & (Fmac|Fctrlvq|Fctrlrx));

		vout8(&cfg, Vconf_status, vin8(&cfg, Vconf_status) | Sfeaturesok);

		for(i=0; i<nelem(c->queue); i++){
			vout16(&cfg, Vconf_queuesel, i);
			n = vin16(&cfg, Vconf_queuesize);
			if(n == 0 || (n & (n-1)) != 0){
				if(i < 2)
					print("ethervirtio: queue %d has invalid size %d\n", i, n);
				break;
			}
			if(initqueue(&c->queue[i], n) < 0)
				break;
			c->queue[i].notify = c->notify;
			if(c->queue[i].notify.type == Vio_port)
				c->queue[i].notify.port+= c->notifyoffmult * vin16(&cfg, Vconf_queuenotifyoff);
			else
				c->queue[i].notify.mem+= c->notifyoffmult * vin16(&cfg, Vconf_queuenotifyoff);
			coherence();
			vout64(&cfg, Vconf_queuedesc, PADDR(c->queue[i].desc));
			vout64(&cfg, Vconf_queueavail, PADDR(c->queue[i].avail));
			vout64(&cfg, Vconf_queueused, PADDR(c->queue[i].used));
		}
		if(i < 2){
			print("ethervirtio: no queues\n");
Baddev:
			pcidisable(p);
			/* TODO, vunmap */
			free(c);
			continue;
		}
		c->nqueue = i;		

		if(h == nil)
			h = c;
		else
			t->next = c;
		t = c;
	}

	return h;
}


static int
reset(Ether* edev)
{
	static uchar zeros[Eaddrlen];
	Ctlr *ctlr;
	int i;

	if(ctlrhead == nil)
		ctlrhead = pciprobe();

	for(ctlr = ctlrhead; ctlr != nil; ctlr = ctlr->next){
		if(ctlr->active)
			continue;
		if(edev->port == 0 || edev->port == ctlr->port){
			ctlr->active = 1;
			break;
		}
	}

	if(ctlr == nil)
		return -1;

	edev->ctlr = ctlr;
	edev->port = ctlr->port;
	edev->irq = ctlr->pcidev->intl;
	edev->tbdf = ctlr->pcidev->tbdf;
	edev->mbps = 1000;
	edev->link = 1;

	if((ctlr->feat[0] & Fmac) != 0 && memcmp(edev->ea, zeros, Eaddrlen) == 0){
		for(i = 0; i < Eaddrlen; i++)
			edev->ea[i] = vin8(&ctlr->dev, Vnet_mac0+i);
	} else {
		for(i = 0; i < Eaddrlen; i++);
			vout8(&ctlr->dev, Vnet_mac0+i, edev->ea[i]);
	}

	edev->arg = edev;

	edev->attach = attach;
	edev->shutdown = shutdown;
	edev->ifstat = ifstat;
	edev->multicast = multicast;
	edev->promiscuous = promiscuous;

	pcisetbme(ctlr->pcidev);
	intrenable(edev->irq, interrupt, edev, edev->tbdf, edev->name);

	return 0;
}

void
ethervirtio10link(void)
{
	addethercard("virtio10", reset);
}
