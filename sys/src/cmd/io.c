#include <u.h>
#include <libc.h>

char *datac[] = {0,"#P/iob","#P/iow",0,"#P/iol",0,0,0,"#P/msr"};
char *file;

void
usage(void)
{
	fprint(2, "%s: [-f file] [ -WLME ] [-r | -w] address [ value ] [ mask ]\n", argv0);
	exits("usage");
}

void
main(int argc, char** argv) {
	int fd, size, op;
	ulong port;
	uvlong data, value, mask;
	uchar datab[8];
	
	data = value = mask = 0;
	size = 1;
	op = -1;
	ARGBEGIN {
		case 'f': file = EARGF(usage()); break;
		case 'W': size = 2; break;
		case 'L': size = 4; break;
		case 'M': size = 8; break;
		case 'E': datac[1] = datac[2] = datac[4] = datac[8] = "#P/ec"; break;
		case 'r': op = OREAD; break;
		case 'w': op = OWRITE; break;
		default: usage();
	} ARGEND;
	if(op == -1) usage();
	if(argc < 1) usage();
	port = strtoul(*argv, 0, 0);
	argv++, argc--;
	if(op == OWRITE) {
		if(argc < 1) usage();
		value = strtoull(*argv, 0, 0);
		argv++, argc--;
	}
	if(argc > 0){
		mask = ~strtoull(*argv, 0, 0);
		argv++, argc--;
		if(op == OWRITE){
			op = ORDWR;
			value &= ~mask;
		}
	}  
	if(op == OREAD)
		mask = ~mask;
	USED(argv);
	if(argc != 0) usage();
	fd = open(file==nil?datac[size]:file, op);
	if(fd == -1) sysfatal("open: %r");
	if(op == OREAD || op == ORDWR) {
		memset(datab, 0, 8);
		if(pread(fd, datab, size, port) != size)
			sysfatal("pread: %r");
		data = datab[0] | (datab[1] << 8) | (datab[2] << 16) |
			(datab[3] << 24) | ((uvlong)datab[4] << 32) |
			((uvlong)datab[5] << 40) | ((uvlong)datab[6] << 48) | 
			((uvlong)datab[7] << 56);
		data &= mask;
	}
	if(op == OWRITE || op == ORDWR) {
		data |= value;
		datab[0] = data;
		datab[1] = data >> 8;
		datab[2] = data >> 16;
		datab[3] = data >> 24;
		datab[4] = data >> 32;
		datab[5] = data >> 40;
		datab[6] = data >> 48;
		datab[7] = data >> 56;
		if(pwrite(fd, datab, size, port) != size)
			sysfatal("pwrite: %r");
	}
	print("0x%ullx\n", data);
	exits(nil);
}
