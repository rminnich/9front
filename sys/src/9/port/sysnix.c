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
 	uint flags;
	va_list eac = list;
 	flags = va_arg(eac, unsigned int);
 	switch(flags){
 	case EXTC:
 	default:
 		return sysexec(list);
 	case EXAC:
 		up->ac = getac(up, -1);
 		break;
 	case EXXC:
 		error("EXXC not supported yet");
 	}

	ar0 = sysexec(eac);

	nixprepage(-1);
	up->procctl = Proc_toac;
	up->prepagemem = 1; /* for any later sbrk calls ... */

	return ar0;
}
