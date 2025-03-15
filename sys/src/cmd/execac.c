#include <u.h>
#include <libc.h>

void
usage(void)
{
	fprint(2, "Usage: execac [-c core (default 0)] path [args]\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	uintptr core = 0;
	int i;
	void *eargs[2];

	ARGBEGIN {
	case 'c':
		core = strtoul(EARGF(usage()), nil, 0);
		break;
	default:
		print(" badflag('%c')", ARGC());
	}
	ARGEND

	if (argc < 1)
		usage();

	for(i = 0; i < argc; i++)
		print("execac:%s[%d],", argv[i], i);
	print("\n");
	eargs[0] = argv[0];
	eargs[1] = argv;
	print("Requsting core %lld\n", core);
	core = 0xecac | (core << 16);
	print("exec flags = %llx\n", core);
	exec((void*)(core), (char **)eargs);
	print("Returned? %r\n");}
