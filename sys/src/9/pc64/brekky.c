#include <u.h>
#include <libc.h>

void
main()
{
	int x = brk((void *)0x406ff8);
	int y = brk(nil);

	for(int i = 0; i < 1024; i++) malloc(i*512);
	/* 6c -c brekky.c; 6l -o 6.brekky brekky.6; cp 6.brekky /amd64/bin/brekky */
}
