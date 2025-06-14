#include <u.h>
#include <libc.h>
#include <libsec.h>
#include <mp.h>
/* https://www.rfc-editor.org/rfc/rfc7748.html#section-5.2 */

struct {
	char *s, *u, *exp;
	int r;
} tests_x25519[] = {
	{
		"a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4",
		"e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c",
		"c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552",
		1,
	},
	{
		"4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d",
		"e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493",
		"95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957",
		1,
	},
	{
		"0900000000000000000000000000000000000000000000000000000000000000",
		"0900000000000000000000000000000000000000000000000000000000000000",
		"422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079",
		1,
	},
	{
		"0900000000000000000000000000000000000000000000000000000000000000",
		"0900000000000000000000000000000000000000000000000000000000000000",
		"684cf59ba83309552800ef566f2f4d3c1c3887c49360e3875f2eb94d99532c51",
		1000,
	},
/* takes too long on my machine
	{
		"0900000000000000000000000000000000000000000000000000000000000000",
		"0900000000000000000000000000000000000000000000000000000000000000",
		"7c3911e0ab2586fd864497297e575e6f3bc601c0883c30df5f4dd2d24f665424",
		1000000,
	},
*/
	/* Diffie-Hellman Test vector */
	{
		"77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a",
		"0900000000000000000000000000000000000000000000000000000000000000",
		"8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a",
		1,
	},
	{
		"5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb",
		"0900000000000000000000000000000000000000000000000000000000000000",
		"de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f",
		1,
	},
	{
		"77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a",
		"de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f",
		"4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742",
		1,
	},
	{
		"5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb",
		"8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a",
		"4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742",
		1,
	},
	
};

int chatty;

int
parsehex(char *s, uchar *h, char *l)
{
	char *e;
	mpint *m;
	int n;

	n = strlen(s);
	if(n == 0)
		return 0;
	assert((n & 1) == 0);
	n >>= 1;
	e = nil;
	m = strtomp(s, &e, 16, nil);
	if(m == nil || *e != '\0')
		abort();
	mptober(m, h, n);
	if(l != nil && chatty)
		print("%s = %.*H\n", l, n, h);
	return n;
}

void
main(int argc, char **argv)
{
	uchar s[32], u[32], out[32];
	char buf[64+1];
	int i, j;

	fmtinstall('H', encodefmt);

	ARGBEGIN {
	case 'd':
		chatty++;
		break;
	} ARGEND;

	for(i = 0; i < nelem(tests_x25519); i++){
		parsehex(tests_x25519[i].s, s, "scalar");
		parsehex(tests_x25519[i].u, u, "u-coordiante");

		for(j = 0; j < tests_x25519[i].r; j++) {
			x25519(out, s, u);
			memcpy(u, s, sizeof(u));
			memcpy(s, out, sizeof(s));
		}
	
		snprint(buf, sizeof buf, "%.*lH", 64, out);
		if(strcmp(buf, tests_x25519[i].exp) != 0){
			fprint(2, 
				"Test %d  (%d rounds):\n"
				"\ts in: %s\n"
				"\tu in: %s\n"
				"Exp: %s\n"
				"Got: %s\n\n",
				i, tests_x25519[i].r,
				tests_x25519[i].s, tests_x25519[i].u, 
				tests_x25519[i].exp, buf
			);
			exits("fail");
		}
	}
	
	exits(nil);
}
