#include "os.h"
#include <mp.h>
#include <libsec.h>

struct {
	char *in, *out;
} tests[] = {
	{ "", "31d6cfe0d16ae931b73c59d7e0c089c0" },
	{ "a", "bde52cb31de33e46245e05fbdbd6fb24" },
	{ "abc", "a448017aaf21d8525fc10ae87aa6729d" },
	{ "message digest", "d9130a8164549fe818874806e1c7014b" },
	{ "abcdefghijklmnopqrstuvwxyz", "d79e1c308aa5bbcdeea8ed63df412da9" },
	{ "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "043f8582f241db351ce627e153e7f0e4" },
	{ "12345678901234567890123456789012345678901234567890123456789012345678901234567890" , "e33b4ddc9c38f2199c3e7b164fcc0536" },
};

void
main(void)
{
	int i;
	uchar digest[MD5dlen];
	char buf[256];

	fmtinstall('H', encodefmt);
	for(i = 0; i < nelem(tests); i++){
		md4((uchar*)tests[i].in, strlen(tests[i].in), digest, 0);
		snprint(buf, sizeof buf, "%.*lH", MD5dlen, digest);
		if(strcmp(tests[i].out, buf) != 0){
			print("Input: %s\n", tests[i].in);
			print("Exp: %s\n", tests[i].out);
			print("Got: %s\n", buf);
			exits("fail");
		}
	}
	exits(nil);
}
