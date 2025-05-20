#include "os.h"
#include <mp.h>
#include <libsec.h>

uchar key[] = "Jefe";
uchar data[] = "what do ya want for nothing?";
char out[] = "750c783e6ab0b503eaa86e310a5db738";

void
main(void)
{
	uchar hash[MD5dlen];
	char buf[256];

	fmtinstall('H', encodefmt);
	hmac_md5(data, strlen((char*)data), key, 4, hash, nil);
	snprint(buf, sizeof buf, "%.*lH", MD5dlen, hash);
	if(strcmp(buf, out) != 0){
		print("Exp: %s\n", out);
		print("Got: %s\n", buf);
		exits("fail");
	}
	exits(nil);
}
