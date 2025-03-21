#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"edf.h"

#include	<a.out.h>

#define DBG if(0)print

/* a note on ABI. For standard exec, the va_list is a standard sysexec: 
 * a char * and a char **, two pointers. 
 * For sysexecac, va_list contains a flags, and a va_list compatible
 * with the sysexec abi. */
uintptr
sysexecac(va_list list)
{
	uintptr sysexec(va_list);
 	uintptr ar0;
 	uintptr flags;
	va_list eac = list;
	char ** args;

 	flags = va_arg(eac, uintptr);
 	switch(flags){
 	case EXTC:
		break;
 	default:
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
	
	if (waserror()) {
		if (up->ac)
			stopac();
		nexterror();
	}
	args = va_arg(eac, char **);
	DBG("args is %p; args[0] is %p; args[1] is %p\n", args, ((uintptr*)args)[0], ((uintptr*)args)[1]);
	ar0 = sysexec((va_list)args);

	up->procctl = Proc_toac;
	poperror();

	return ar0;
}
