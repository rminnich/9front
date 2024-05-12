#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include "ip.h"

static void
nullbind(Ipifc*, int, char**)
{
}

static void
nullunbind(Ipifc*)
{
}

static void
nullbwrite(Ipifc*, Block *bp, int, uchar*, Routehint*)
{
	freeb(bp);
	error("nullbwrite");
}

Medium nullmedium =
{
.maxtu=		64*1024,
.name=		"null",
.bind=		nullbind,
.unbind=	nullunbind,
.bwrite=	nullbwrite,
};

/* used in ipifc to prevent unbind while bind is in progress */
Medium unboundmedium =
{
.maxtu=		64*1024,
.name=		"unbound",
.bind=		nullbind,
.unbind=	nullunbind,
.bwrite=	nullbwrite,
};

void
nullmediumlink(void)
{
	addipmedium(&nullmedium);
}
