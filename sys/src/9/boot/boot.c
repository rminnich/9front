#include <u.h>
#include <libc.h>

char bin[] = "/bin";
char root[] = "/root";

void
main(int, char *argv[])
{
	char buf[32];

	print("HI!\n");
	while (1) {
		print("read from stdin, 1 byte\n");
		int rc = read(0, buf, 1);
		if (rc < 1) {
			print("shit, no stdin: %r");
			exits("shit. no stdin;%r");
		}
		print("rc is %d\n", rc);
		if (rc > 0) print("byte is %c\n", buf[0]);
	}

	while (0) {
		print("hit the any key");
		print("let's go\n");
		switch(fork()) {
			case 0:
				print("KID ... return\n");
				exits("all done");
				break;
			case -1:
				print("FORK:%r\n");
				break;
			default:
				memset(buf, 0, sizeof(buf));
				if(await(buf, sizeof(buf)-1) < 0) {
					print("await:%r\n");
				}
				print("returned ... %s\n", buf);
				break;
		}
		print("read stdin\n");
		read(0, buf, 1);
		print("you typed %c\n", buf[0]);
	}
	for(int i = 0; i < 32; i++) write(1, "hi\n", 3);
	/* setup the boot namespace */
	bind("/boot", bin, MAFTER);

	if(fork() == 0){
		print("HEY LET'S RUN PAQFS\n");
		execl("/bin/paqfs", "-qa", "-c", "8", "-m", root, "/boot/bootfs.paq", nil);
		//("/bin/rc", "-m", "/lib/rcmain");
		print("FUCK IT FAILED: %r\n");
		goto Err;
	}
	print("WAIT FOR PAQFS\n");
	if(await(buf, sizeof(buf)) < 0)
		goto Err;
	print("SEEMS OK\n");

	bind(root, "/", MAFTER);

	buf[0] = '/';
	buf[1+read(open("/env/cputype", OREAD|OCEXEC), buf+1, sizeof buf - 6)] = '\0';
	print("cputype is %s", buf);
	strcat(buf, bin);
	bind(buf, bin, MAFTER);
	
	bind("/root/rc", "/rc", MREPL);
	bind("/rc/bin", bin, MAFTER);
static uchar x[4096];
int i;
i = stat("/bin/bootrc", x, 4096);
print("---------->>>>>>>>>>>>>>>>>>stat of /bin/bootrc is %d\n", i);
	exec("/bin/bootrc", argv);
Err:
	print("WE ARE FUCKED\n");
	errstr(buf, sizeof buf);
	_exits(buf);
}
