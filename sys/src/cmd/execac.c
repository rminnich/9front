#include <u.h>
#include <libc.h>

void
usage(void)
{
	fprint(2, "Usage: testexecap [-c core (default 0)] path [args]\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	void * core = (void *)0xecac;
	int i;

	ARGBEGIN {
	case 'c':
		core = (void *)strtoul(EARGF(usage()), nil, 0);
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
	exec(core, argv);
	print("Returned? %r\n");
}
