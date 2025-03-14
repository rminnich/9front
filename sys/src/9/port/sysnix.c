#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"edf.h"

#include	<a.out.h>

uintptr
sysexecac(va_list list)
{
	uintptr sysexec(va_list);
 	uintptr ar0;
 	uintptr flags;
	va_list eac = list;
	char ** args;
	void *v[2];

 	flags = va_arg(eac, uintptr);
	print("sysexecac: flags %p\n", flags);
 	switch(flags){
 	case EXTC:
		list = eac;
 	default:
		print("sysexecac: normal\n");
 		return sysexec(list);
 	case EXAC:
 		up->ac = getac(up, -1);
 		break;
 	case EXXC:
 		error("EXXC not supported yet");
 	}

	/* So this is kind of a nasty hack.
	 * sysexec expects a va_args with a char * and a char **
	 * i.e., path and argv. The path in our case is the core role.
	 * So we need to get the argv, and reconstitute a new
	 * va_list with the path, and the argv, as the two elements. */
	
	args = va_arg(eac, char **);
	v[0] = args[0];
	v[1] = args;
	if (waserror()) {
		stopac();
		nexterror();
	}
	ar0 = sysexec((va_list)v);

	nixprepage(-1);
	up->procctl = Proc_toac;
	up->prepagemem = 1; /* for any later sbrk calls ... */
	poperror();

	return ar0;
}
