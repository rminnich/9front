#include <u.h>
#include <libc.h>

void
usage(void)
{
	fprint(2, "Usage: testexecap [-c core (default 0)] path [args]\n");
	exits("usage");
}

int	sysr1(int, char*, char*[]);
int execac(int, char *name, char* argv[]);
void
main(int argc, char *argv[])
{
	int core = 0;

	ARGBEGIN {
	case 'c':
		core = atoi(EARGF(usage()));
		break;
	default:
		print(" badflag('%c')", ARGC());
	}
	ARGEND

	if (argc < 1)
		usage();


	sysr1/*execac*/(core, argv[0], &argv[0]);
	print("Returned? %r\n");
}
