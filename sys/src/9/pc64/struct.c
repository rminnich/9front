#include	"u.h"
#include	"tos.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/pci.h"
#include	"ureg.h"
//#define	offsetof(s, m)	(ulong)(&(((s*)0)->m))

void
main()
{
	print("#define M_PROC %d\n", offsetof(Mach, proc));
	print("#define PROC_DBGREG %d\n", offsetof(Proc, dbgreg));
	print("#define M_STACK %d\n", offsetof(Mach, stack));

	print("#define UREG_SS %d\n", offsetof(Ureg, ss));
	print("#define UREG_SP %d\n", offsetof(Ureg, sp));
	print("#define UREG_FLAGS %d\n", offsetof(Ureg, flags));
	print("#define UREG_CS %d\n", offsetof(Ureg, cs));

	print("#define UREG_PC %d\n", offsetof(Ureg, pc));

	print("#define UREG_DS %d\n", offsetof(Ureg, ds));
	print("#define UREG_eS %d\n", offsetof(Ureg, es));
	print("#define UREG_FS %d\n", offsetof(Ureg, fs));
	print("#define UREG_GS %d\n", offsetof(Ureg, gs));

	print("#define UREG_AX %d\n", offsetof(Ureg, ax));
	print("#define UREG_BP %d\n", offsetof(Ureg, bp));
	print("#define UREG_CX %d\n", offsetof(Ureg, cx));
	print("#define UREG_R11 %d\n", offsetof(Ureg, r11));


}
