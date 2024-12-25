#include <u.h>
#include <libc.h>
#include <auth.h>

char *keypat;

void
usage(void)
{
	fprint(2, "usage: %s fmt\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char params[512];
	AuthRpc *rpc;
	int fd;

	ARGBEGIN{
	case 'k':
		keypat = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	quotefmtinstall();
	if(keypat == nil)
		snprint(params, sizeof(params), "proto=totp label=%q", argv[0]);
	else
		snprint(params, sizeof(params), "proto=totp %s", keypat);

	if((fd = open("/mnt/factotum/rpc", ORDWR|OCEXEC)) == -1)
		sysfatal("open /mnt/factotum/rpc: %r");
	if((rpc = auth_allocrpc(fd)) == nil)
		sysfatal("allocrpc: %r");
	if(auth_rpc(rpc, "start", params, strlen(params)) != ARok
	|| auth_rpc(rpc, "read", nil, 0) != ARok)
		sysfatal("totp proto: %r");
	rpc->arg[rpc->narg] = '\0';
	print("%s\n", rpc->arg);

	close(fd);
	auth_freerpc(rpc);
	exits(nil);
}
