#include <u.h>
#include <libc.h>
#include <ctype.h>

/*
 *  make an address, add the defaults
 */
char *
netmkaddrbuf(char *addr, char *defnet, char *defsrv, char *buf, int len)
{
	char *cp;

	cp = strchr(addr, '!');
	if(cp == nil){
		/*
		 *  dump network name
		 */
		if(defnet == nil){
			if(defsrv != nil)
				snprint(buf, len, "net!%s!%s", addr, defsrv);
			else
				snprint(buf, len, "net!%s", addr);
		}
		else {
			if(defsrv != nil)
				snprint(buf, len, "%s!%s!%s", defnet, addr, defsrv);
			else
				snprint(buf, len, "%s!%s", defnet, addr);
		}
	} else {
		cp = strchr(cp+1, '!');
		if(cp != nil || defsrv == nil){
			/*
			 *  if there is already a service or no defsrv given
			 */
			snprint(buf, len, "%s", addr);
		} else {
			/*
			 *  add default service
			 */
			snprint(buf, len,"%s!%s", addr, defsrv);
		}
	}
	return buf;
}

char *
netmkaddr(char *addr, char *defnet, char *defsrv)
{
	static char buf[256];

	return netmkaddrbuf(addr, defnet, defsrv, buf, sizeof(buf));
}
