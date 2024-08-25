#include <u.h>
#include <libc.h>
#include <ctype.h>

/*
 *  force a connection to hangup
 */
int
hangup(int ctl)
{
	static char msg[] = "hangup";
	return write(ctl, msg, sizeof(msg)-1) != sizeof(msg)-1;
}
