/*
 * I²C driver for MT7688
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"../port/error.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/i2c.h"


enum {
	I²C_SM0CFG2	=	0x28,
	I²C_SM0CTL0	=	0x40,
	ctl0_strch	=	1<<0,
	clt0_en		=	1<<1,
	I²C_SM0CTL1	=	0x44,
	ctl1_trig	=	1<<0,
	ctl1_start	=	1<<4,
	ctl1_write	=	2<<4,
	ctl1_stop	=	3<<4,
	ctl1_rnack	=	4<<4,
	ctl1_rack	=	5<<4,
	I²C_SM0D0	=	0x50,
	I²C_SM0D1	=	0x54,
};

#define	ctl0_clkdiv(x)	(((x) & 0x7FF) << 16)

#define i2crd(o)	(*IO(u32int, (I2CBASE + (o))))
#define	i2cwr(o, v)	(*IO(u32int, (I2CBASE + (o))) = (v))

int i2cdebug = 0;


typedef struct Ctlr Ctlr;
struct Ctlr
{
	int		clkdiv;
};


static void
reset(Ctlr *ctlr)
{
	i2cwr(I²C_SM0CTL0, ctl0_strch | clt0_en | ctl0_clkdiv(ctlr->clkdiv));
	i2cwr(I²C_SM0CFG2, 0x0);

	if(i2cdebug)
		print("i2c reset: %lux\n", i2crd(I²C_SM0CTL0));
}


static int
init(I2Cbus *bus)
{
	Ctlr *ctlr = bus->ctlr;

/*
 *	Base clock on MT7688 is 40MHz
 *	To get the standard speed for I²C of 100kHz,
 *	40MHz / 100kHz (and -1 to round up)
 */

	ctlr->clkdiv = 40000000 / (bus->speed - 1);

	reset(ctlr);

	return 0;
}


static void
i2cstop(void)
{
	i2cwr(I²C_SM0CTL1, ctl1_stop | ctl1_trig);
}


static int
i2cbusy(void)
{
	u32int buf;
	int t;

	for(t = 0; t < 1000; t++){
		buf = i2crd(I²C_SM0CTL1);
		if((buf & ctl1_trig) == 0){
			return 0;
		}
		delay(1);
	}

	i2cstop();
	return 1;
}


static int
i2cstart(u32int flag, u32int bytes)
{
	if(bytes > 0)
		bytes = bytes - 1;

	i2cwr(I²C_SM0CTL1, ctl1_trig | flag | bytes << 8);

	if(i2cbusy()){
		return 1;
	}

	return 0;
}


static int
i2cack(u32int bytes)
{
	u32int buf, ack, expect;

	expect = (1 << bytes) - 1;
	buf = i2crd(I²C_SM0CTL1);
	ack = (buf >> 16) & 0xFF;

	if(i2cdebug)
		print("want; %d - got: %d - \n", expect, ack);

	if(ack == expect){
		return 0;
	}

	i2cstop();
	return 1;
}


static int
io(I2Cdev *dev, uchar *pkt, int olen, int ilen)
{
	int oΔ, iΔ, i, bytes;
	u16int addr, last;
	u32int data[2];


	if(i2cdebug){
		int alen = 1;
		if(olen > alen && (pkt[0] & 0xF8) == 0xF0)
			alen++;

		if(alen > 1){
			addr = pkt[1] | (pkt[0] << 8);
		} else {
			addr = pkt[0];
		}

		print("addr: %uX - ", addr>>1);
	}

	if(i2cbusy()){
		print("I²C not ready\n");
		return 0;
	}

	if(i2cstart(ctl1_start, 0)){
		print("I²C no start\n");
		return 0;
	}

	i = 0;
	oΔ = olen;
	iΔ = ilen;

	if(i2cdebug)
		print("total out: %d - in: %d - ", oΔ, iΔ);

	while(oΔ){
		bytes = (oΔ >= 8) ? 8 : oΔ;
		data[0] = 0;
		data[1] = 0;
		memmove(data, &pkt[i], bytes);
		i2cwr(I²C_SM0D0, data[0]);
		i2cwr(I²C_SM0D1, data[1]);
		if(i2cstart(ctl1_write, bytes)){
			print("I²C timeout\n");
			return 0;
		}
		if(i2cack(bytes)){
			if(i2cdebug)
				print("I²C write failed\n");
			return 0;
		}
		i += bytes;
		oΔ -= bytes;
	}

	while(iΔ){
		bytes = (iΔ >= 8) ? 8 : iΔ;
		data[0] = 0;
		data[1] = 0;
		last = (iΔ < 8) ? ctl1_rnack : ctl1_rack;
		if(i2cstart(last, bytes)){
			print("I²C timeout\n");
			return 0;
		}
		data[0] = i2crd(I²C_SM0D0);
		data[1] = i2crd(I²C_SM0D1);
		memmove(&pkt[i], data, bytes);
		if(last == ctl1_rack){
			if(i2cack(bytes)){
				if(i2cdebug)
					print("I²C read failed\n");
				return 0;
			}
		}
		i += bytes;
		iΔ -= bytes;
	}

	if(i2cdebug)
		print("return: %d \n", i);

	i2cstop();

	return i;
}


static Ctlr ctlr1;


void
i2c7688link(void)
{
	static I2Cbus i2c1 = {"i2c1", 400000, &ctlr1, init, io};
	addi2cbus(&i2c1);
}
