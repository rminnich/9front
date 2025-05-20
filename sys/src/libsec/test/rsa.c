#include "os.h"
#include <mp.h>
#include <libsec.h>
#include <bio.h>

void
main(int argc, char **argv)
{
	int n;
	vlong start;
	char *p;
	uchar buf[4096];
	Biobuf b;
	RSApriv *rsa;
	mpint *clr, *enc, *clr2;
	int iflag, pflag;

	iflag = pflag = 0;
	ARGBEGIN{
	case 'i':
		iflag++;
		break;
	case 'p':
		pflag++;
		break;
	}ARGEND

	fmtinstall('B', mpfmt);

	rsa = rsagen(1024, 16, 0);
	if(rsa == nil)
		sysfatal("rsagen");
	Binit(&b, 0, OREAD);
	clr = mpnew(0);
	clr2 = mpnew(0);
	enc = mpnew(0);

	strtomp("123456789abcdef123456789abcdef123456789abcdef123456789abcdef", nil, 16, clr);
	rsaencrypt(&rsa->pub, clr, enc);
	
	start = nsec();
	for(n = 0; n < 10; n++)
		rsadecrypt(rsa, enc, clr);
	if(pflag)
		print("%lld\n", nsec()-start);

	start = nsec();
	for(n = 0; n < 10; n++)
		mpexp(enc, rsa->dk, rsa->pub.n, clr2);
	if(pflag)
		print("%lld\n", nsec()-start);

	if(mpcmp(clr, clr2) != 0)
		sysfatal("%B != %B", clr, clr2);

	if(!iflag)
		exits(nil);
	print("> ");
	while(p = Brdline(&b, '\n')){
		n = Blinelen(&b);
		letomp((uchar*)p, n, clr);
		print("clr %B\n", clr);
		rsaencrypt(&rsa->pub, clr, enc);
		print("enc %B\n", enc);
		rsadecrypt(rsa, enc, clr);
		print("clr %B\n", clr);
		n = mptole(clr, buf, sizeof(buf), nil);
		write(1, buf, n);
		print("> ");
	}
}
