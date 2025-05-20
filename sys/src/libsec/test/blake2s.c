#include <u.h>
#include <libc.h>
#include <libsec.h>

struct {
	char *in, *exp;
} tests[] = {
	{ /* empty input */
		"",
		"69217a3079908094e11121d042354a7c1f55b6482ca1a51e1b250dfd1ed0eef9",
	},
	{ /* reference hash */
		"abc",
		"508c5e8c327c14e2e1a72ba34eeb452f37458b209ed63a294d999b4c86675982"
	},
	{ /* exactly 1 block */
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
		"f85b88e0ac55872416d202c5f4881e7dbc9c7270542ef75074ff9b0a610b5a0e",
	},
	{ /* exactly 2 blocks */
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
		"ea263e84e451e17ff77d642cd7a751757765aded33d62b96f1e998af31024e30",
	},
	{ /* 1 block and some change */
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
		"c4b49a77ee46b6c166d56157131d1ec182153d0004428d6ac011edc942becd93",
	},
	{ /* 2 blocks and some change */
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
		"cae0fc9b9f296425db4a4af96f83e947649e78954f4081ed72ebdfdfb29cfca4",
	},
	{ /* 3 blocks and some change */
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
		"1b066f374e7e2d95a64643c5da9e1b89eab1c202cd5c17e27bc061ba3bbdc24a",
	},
};

void
main(int, char**)
{
	DigestState *s;
	uchar digest[B2s_256dlen];
	char buf[256];
	int i;
	char *p;

	fmtinstall('H', encodefmt);
	for(i = 0; i < nelem(tests); i++){
		blake2s_256((uchar*)tests[i].in, strlen(tests[i].in), digest, nil);
		snprint(buf, sizeof buf, "%.*lH", B2s_256dlen, digest);
		if(strcmp(buf, tests[i].exp) != 0){
			fprint(2, "Test: %s\nExp: %s\nGot: %s\n\n", tests[i].in, tests[i].exp, buf);
			exits("fail");
		}

		s = nil;
		memset(digest, 0, B2s_256dlen);
		for(p = tests[i].in; *p != 0; p++)
			s = blake2s_256((uchar*)p, 1, nil, s);

		blake2s_256(nil, 0, digest, s);
		snprint(buf, sizeof buf, "%.*lH", B2s_256dlen, digest);
		if(strcmp(buf, tests[i].exp) != 0){
			fprint(2, "Trickle Test: %s\nExp: %s\nGot: %s\n\n", tests[i].in, tests[i].exp, buf);
			exits("fail");
		}
	}
	exits(nil);
}
