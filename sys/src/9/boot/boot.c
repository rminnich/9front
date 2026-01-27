#include <u.h>
#include <libc.h>

char bin[] = "/bin";
char root[] = "/root";

void
main(int, char *argv[])
{
	char buf[32];

	for(int i = 0; i < 32; i++) write(1, "hi\n", 3);
	/* setup the boot namespace */
	bind("/boot", bin, MAFTER);

	if(fork() == 0){
		print("HEY LET'S RUN PAQFS\n");
		execl("/bin/paqfs", "-qa", "-c", "8", "-m", root, "/boot/bootfs.paq", nil);
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
	strcat(buf, bin);
	bind(buf, bin, MAFTER);
	bind("/root/rc", "/rc", MREPL);
	bind("/rc/bin", bin, MAFTER);

	exec("/bin/bootrc", argv);
Err:
	print("WE ARE FUCKED\n");
	errstr(buf, sizeof buf);
	_exits(buf);
}
