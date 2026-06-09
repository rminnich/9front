
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

extern uchar __objtype_bin_paqfscode[];
extern ulong __objtype_bin_paqfslen;
extern uchar __objtype_bin_rccode[];
extern ulong __objtype_bin_rclen;
extern uchar __objtype_bin_auth_factotumcode[];
extern ulong __objtype_bin_auth_factotumlen;
extern uchar bootfs_paqcode[];
extern ulong bootfs_paqlen;
extern uchar bootcode[];
extern ulong bootlen;

void bootlinks(void){

	addbootfile("paqfs", __objtype_bin_paqfscode, __objtype_bin_paqfslen);
	addbootfile("rc", __objtype_bin_rccode, __objtype_bin_rclen);
	addbootfile("factotum", __objtype_bin_auth_factotumcode, __objtype_bin_auth_factotumlen);
	addbootfile("bootfs.paq", bootfs_paqcode, bootfs_paqlen);
	addbootfile("boot", bootcode, bootlen);

}

