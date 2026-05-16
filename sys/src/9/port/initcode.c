/*
 * IMPORTANT!  DO NOT ADD LIBRARY CALLS TO THIS FILE.
 * The entire text image must fit on one page
 * (and there's no data segment, so any read/write data must be on the stack).
 */

#include <u.h>
#include <libc.h>

char cons[] = "/dev/eia0"; // "/dev/cons";
char boot[] = "/boot/boot";
char dev[] = "/dev";
char c[] = "#c";
char d[] = "#d";
char e[] = "#e";
char ec[] = "#ec";
char p[] = "#p";
char s[] = "#s";
char t[] = "#t";
char σ[] = "#σ";
char env[] = "/env";
char fd[] = "/fd";
char proc[] = "/proc";
char srv[] = "/srv";
char shr[] = "/shr";

void
startboot(char*, char **argv)
{
	char buf[256];	/* keep this fairly large to capture error details */
	static char *backup[2] = {"boot", 0};
	argv = argv ? argv : backup;
	bind(t, dev, MAFTER);
	bind(c, dev, MAFTER);
	bind(d, fd, MREPL);
	bind(e, env, MREPL|MCREATE);
	bind(ec, env, MAFTER);
	bind(p, proc, MREPL);
	bind(s, srv, MREPL|MCREATE);
	bind(σ, shr, MREPL);

	if (0){
	int fd;
	if ((fd = open("#t", OREAD)) < 0){
		rerrstr(buf, sizeof(buf));
		write(1, buf, sizeof(buf));
	}
	while (1) {
	int amt;
	if ((amt = read(fd, buf, sizeof(buf))) < 0){
		rerrstr(buf, sizeof(buf));
		write(1, buf, sizeof(buf));
		break;
	}
	if (amt == 0) break;
	for(int i = 0; i < amt; i += 32) write(1, &buf[i], 32);
	}
	close(0);
	}
	if (open(cons, OREAD) < 0) {
		rerrstr(buf, sizeof(buf));
		write(1, buf, sizeof(buf));
	}
	open(cons, OWRITE);
	open(cons, OWRITE);

	exec(boot, argv);

	rerrstr(buf, sizeof buf);
	buf[sizeof buf - 1] = '\0';
	_exits(buf);
}
