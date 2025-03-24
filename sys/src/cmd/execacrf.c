#include <u.h>
#include <libc.h>
/* 6c execacrf.c && 6l execacrf.6 && cp 6.out /amd64/bin/execacrf */
void
usage(void)
{
	fprint(2, "Usage: execac path [args]\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{

	ARGBEGIN {
	default:
		print(" badflag('%c')", ARGC());
	}
	ARGEND

	if (argc < 1)
		usage();

print("Before rfork\n\n\n");
	int pid;
	pid = rfork((1<<15) /* RFCORE */);

	print("1: After the fork.  Should be running on AC\n\n\n");

	print("2: After the fork.  Should be running on AC\n\n\n");

	print("3: Here! argv[0]=%s, argv[1]=%s\n", argv[0], argv[1]);
	exec(argv[0], argv);

}
