#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ip.h>
#include <ndb.h>
#include "ipconfig.h"

enum {
	SOLICIT	= 1,
	ADVERTISE,
	REQUEST,
	CONFIRM,
	RENEW,
	REBIND,
	REPLY,
	RELEASE,
	DECLINE,
	RECONFIGURE,
	INFOREQ,
	RELAYFORW,
	RELAYREPL,
};

static uchar v6dhcpservers[IPaddrlen] = {
	0xff, 0x02, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 1, 0, 2,
};

static uchar sid[256];
static int sidlen;

static int
openlisten(void)
{
	int n, fd, cfd;
	char data[128], devdir[40];

	snprint(data, sizeof(data), "%s/udp!%I!546", conf.mpoint, conf.lladdr);
	for (n = 0; (cfd = announce(data, devdir)) < 0; n++) {
		if(!noconfig)
			sysfatal("can't announce for dhcp: %r");

		/* might be another client - wait and try again */
		warning("can't announce %s: %r", data);
		sleep(jitter());
		if(n > 10)
			return -1;
	}

	if(fprint(cfd, "headers") < 0)
		sysfatal("can't set header mode: %r");

	fprint(cfd, "ignoreadvice");

	snprint(data, sizeof(data), "%s/data", devdir);
	fd = open(data, ORDWR);
	if(fd < 0)
		sysfatal("open %s: %r", data);
	close(cfd);
	return fd;
}

static int
findopt(int id, uchar **sp, uchar *e)
{
	uchar *p;
	int opt;
	int len;

	p = *sp;
	while(p + 4 <= e) {
		opt = (int)p[0] << 8 | p[1];
		len = (int)p[2] << 8 | p[3];
		p += 4;
		if(p + len > e)
			break;
		if(opt == id){
			*sp = p;
			return len;
		}
		p += len;
	}
	return -1;
}

static int
badstatus(int type, int opt, uchar *p, uchar *e)
{
	int len, status;

	len = findopt(13, &p, e);
	if(len < 0)
		return 0;
	if(len < 2)
		return 1;
	status = (int)p[0] << 8 | p[1];
	if(status == 0)
		return 0;
	warning("dhcpv6: bad status in request/response type %x, option %d, status %d: %.*s",
		type, opt, status, len - 2, (char*)p + 2);
	return status;
}

static int
transaction(int fd, int type, int irt, int retrans, int timeout)
{
	union {
		Udphdr;
		uchar	buf[4096];
	} ipkt, opkt;

	int tra, opt, len, status, sleepfor, jitter;
	uchar *p, *e, *x;
	ulong t1, apflt;

	conf.lease = ~0UL;	/* infinity */

	tra = lrand() & 0xFFFFFF;

	ipmove(opkt.laddr, conf.lladdr);
	ipmove(opkt.raddr, v6dhcpservers);
	ipmove(opkt.ifcaddr, conf.lladdr);
	hnputs(opkt.lport, 546);
	hnputs(opkt.rport, 547);

	p = opkt.buf + Udphdrsize;

	*p++ = type;
	*p++ = tra >> 16;
	*p++ = tra >> 8;
	*p++ = tra >> 0;

	/* client identifier */
	*p++ = 0x00; *p++ = 0x01;
	/* len */
	*p++ = conf.duidlen >> 8;
	*p++ = conf.duidlen;
	memmove(p, conf.duid, conf.duidlen);
	p += conf.duidlen;

	/* IA for non-temporary address */
	len = 12;
	if(validv6prefix(conf.laddr))
		len += 4 + IPaddrlen+2*4;
	*p++ = 0x00; *p++ = 0x03;
	*p++ = len >> 8;
	*p++ = len;
	/* IAID */
	*p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x01;
	/* T1, T2 */
	*p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
	*p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
	if(len > 12){
		*p++ = 0x00; *p++ = 0x05;
		*p++ = 0x00; *p++ = IPaddrlen+2*4;
		memmove(p, conf.laddr, IPaddrlen);
		p += IPaddrlen;
		memset(p, 0xFF, 2*4);
		p += 2*4;
	}

	/* IA for prefix delegation */
	len = 12;
	if(validv6prefix(conf.v6pref))
		len += 4 + 2*4+1+IPaddrlen;
	*p++ = 0x00; *p++ = 0x19;
	*p++ = len >> 8;
	*p++ = len;
	/* IAID */
	*p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x02;	/* lies */
	/* T1, T2 */
	*p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
	*p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
	if(len > 12){
		*p++ = 0x00; *p++ = 0x1a;
		*p++ = 0x00; *p++ = 2*4+1+IPaddrlen;

		*p++ = conf.preflt >> 24;
		*p++ = conf.preflt >> 16;
		*p++ = conf.preflt >> 8;
		*p++ = conf.preflt;

		*p++ = conf.validlt >> 24;
		*p++ = conf.validlt >> 16;
		*p++ = conf.validlt >> 8;
		*p++ = conf.validlt;

		*p++ = conf.prefixlen;

		ipmove(p, conf.v6pref);
		p += IPaddrlen;
	}

	/* Option Request */
	*p++ = 0x00; *p++ = 0x06;
	*p++ = 0x00; *p++ = 0x02;
	*p++ = 0x00; *p++ = 0x17;	/* DNS servers */

	/* server identifier */
	if(sidlen > 0
	&& type != SOLICIT && type != CONFIRM && type != REBIND){
		*p++ = 0x00; *p++ = 0x02;
		/* len */
		*p++ = sidlen >> 8;
		*p++ = sidlen;;
		memmove(p, sid, sidlen);
		p += sidlen;
	}

	len = -1;
	for(sleepfor = irt; timeout > 0; sleepfor <<= 1){
		DEBUG("sending dhcpv6 request %x", opkt.buf[Udphdrsize]);

		jitter = sleepfor / 10;
		if(jitter > 1)
			sleepfor += nrand(jitter);

		alarm(sleepfor);
		if(len < 0)
			write(fd, opkt.buf, p - opkt.buf);

		len = read(fd, ipkt.buf, sizeof(ipkt.buf));
		timeout += alarm(0);
		timeout -= sleepfor;
		if(len == 0)
			break;
		if(len < 0){
			if(--retrans == 0)
				break;
			continue;
		}
		if(len < Udphdrsize+4)
			continue;

		if(ipkt.buf[Udphdrsize+1] != ((tra>>16)&0xFF)
		|| ipkt.buf[Udphdrsize+2] != ((tra>>8)&0xFF)
		|| ipkt.buf[Udphdrsize+3] != ((tra>>0)&0xFF))
			continue;

		DEBUG("got dhcpv6 reply %x from %I on %I", ipkt.buf[Udphdrsize+0], ipkt.raddr, ipkt.ifcaddr);

		type |= (int)ipkt.buf[Udphdrsize+0]<<8;
		switch(type){
		case ADVERTISE << 8 | SOLICIT:
		case REPLY << 8 | REQUEST:
		case REPLY << 8 | RENEW:
		case REPLY << 8 | REBIND:
			goto Response;
		default:
			return -1;
		}
	}
	return -1;

Response:
	for(p = ipkt.buf + Udphdrsize + 4, e = ipkt.buf + len; p < e; p = x) {
		if (p+4 > e)
			return -1;

		opt = (int)p[0] << 8 | p[1];
		len = (int)p[2] << 8 | p[3];
		p += 4;
		x = p+len;
		if (x > e)
			return -1;

		DEBUG("got dhcpv6 option %d: [%d] %.*H", opt, len, len, p);

		switch(opt){
		case 1:		/* client identifier */
			continue;
		case 2:		/* server identifier */
			if(len < 1 || len > sizeof(sid))
				break;
			sidlen = len;
			memmove(sid, p, sidlen);
			continue;
		case 3:		/* IA for non-temporary address */
			if(p+12 > x)
				break;

			t1 =	(ulong)p[4] << 24 |
				(ulong)p[5] << 16 |
				(ulong)p[6] << 8 |
				(ulong)p[7];

			/* skip IAID, T1, T2 */
			p += 12;

			status = badstatus(type, opt, p, x);
			if(status != 0)
				return -status;

			/* IA Addresss */
			if(findopt(5, &p, x) < IPaddrlen + 2*4)
				break;

			ipmove(conf.laddr, p);
			memset(conf.mask, 0xFF, IPaddrlen);
			p += IPaddrlen;

			/* preferred lifetime of IA Address */
			apflt =	(ulong)p[0] << 24 |
				(ulong)p[1] << 16 |
				(ulong)p[2] << 8 |
				(ulong)p[3];

			/* adjust lease */
			if(t1 != 0 && t1 < conf.lease)
				conf.lease = t1;
			if(apflt != 0 && apflt < conf.lease)
				conf.lease = apflt;

			continue;
		case 13:		/* status */
			status = badstatus(type, opt, p - 4, x);
			if(status != 0)
				return -status;
			continue;
		case 23:		/* dns servers */
			if(len % IPaddrlen)
				break;
			addaddrs(conf.dns, sizeof(conf.dns), p, len);
			continue;
		case 25:		/* IA for prefix delegation */
			if(p+12 > x)
				break;

			t1 =	(ulong)p[4] << 24 |
				(ulong)p[5] << 16 |
				(ulong)p[6] << 8 |
				(ulong)p[7];

			/* skip IAID, T1, T2 */
			p += 12;

			status = badstatus(type, opt, p, x);
			if(status != 0){
				if(type == (ADVERTISE << 8 | SOLICIT))
					continue;
				return -status;
			}

			/* IA Prefix */
			if(findopt(26, &p, x) < 2*4+1+IPaddrlen)
				break;

			conf.preflt = 	(ulong)p[0] << 24 |
					(ulong)p[1] << 16 |
					(ulong)p[2] << 8 |
					(ulong)p[3];
			conf.validlt = 	(ulong)p[4] << 24 |
					(ulong)p[5] << 16 |
					(ulong)p[6] << 8 |
					(ulong)p[7];
			p += 8;
			if(conf.preflt > conf.validlt)
				break;

			conf.prefixlen = *p++ & 127;
			genipmask(conf.v6mask, conf.prefixlen);
			maskip(p, conf.v6mask, conf.v6pref);

			/* adjust lease */
			if(t1 != 0 && t1 < conf.lease)
				conf.lease = t1;
			if(conf.preflt != 0 && conf.preflt < conf.lease)
				conf.lease = conf.preflt;

			continue;
		default:
			DEBUG("unknown dhcpv6 option: %d", opt);
			continue;
		}
		warning("dhcpv6: malformed option %d: [%d] %.*H", opt, len, len, x-len);
	}
	return 0;
}

int
dhcpv6query(int renew)
{
	int fd;

	if(!renew){
		ipmove(conf.laddr, IPnoaddr);
		ipmove(conf.v6pref, IPnoaddr);
		ipmove(conf.v6mask, IPnoaddr);
		conf.prefixlen = 0;
		conf.preflt = 0;
		conf.validlt = 0;
		conf.autoflag = 0;
		conf.onlink = 0;
	}

	if(conf.duidlen <= 0)
		return -1;

	fd = openlisten();
	if(fd < 0)
		return -1;

	if(renew){
		if(!validv6prefix(conf.laddr))
			goto fail;
		/*
		 * the standard says 600 seconds for maxtimeout,
		 * but this seems ridiculous. better start over.
		 */
		if(transaction(fd, RENEW, 10*1000, 0, 30*1000) < 0){
			if(!validv6prefix(conf.laddr))
				goto fail;
			if(transaction(fd, REBIND, 10*1000, 0, 30*1000) < 0)
				goto fail;
		}
	} else {
		/*
		 * the standard says SOL_MAX_RT is 3600 seconds,
		 * but it is better to fail quickly here and wait
		 * for the next router advertisement.
		 */
		if(transaction(fd, SOLICIT, 1000, 0, 10*1000) < 0)
			goto fail;
		if(!validv6prefix(conf.laddr))
			goto fail;
		if(transaction(fd, REQUEST, 1000, 10, 30*1000) < 0)
			goto fail;
	}
	if(!validv6prefix(conf.laddr))
		goto fail;
	close(fd);
	return 0;
fail:
	close(fd);
	return -1;

}
