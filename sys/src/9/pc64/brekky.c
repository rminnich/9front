#include <u.h>
#include <libc.h>

void
main(int ac, char *av[])
{
	float f = 1.8;
	//f = 1.8 ; //* (float)ac;
	return;
	write(1, &f, sizeof(f));
	exits("we tried");
	int x = brk((void *)0x406ff8);
	int y = brk(nil);

	for(int i = 0; i < ac-1; i++) malloc(i*512);
	/* 6c -c brekky.c; 6l -o 6.brekky brekky.6; cp 6.brekky /amd64/bin/brekky */
	brk(0);
	print("don a bunch %f\n");
	if (ac == 3) 
		print("0 is %p\n", brk(0));
}
