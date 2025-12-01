/*
 * PL011 UART from Miller's BCM2835 driver
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

extern PhysUart sbiphysuart;

static Uart sbiuart = {
	.regs	= nil,
	.name	= "uart0",
	.freq	= 24*Mhz,
	.baud	= 115200,
	.phys	= &sbiphysuart,
};

static void
donothing(Uart*, int)
{
}

static int
ureturn0(Uart*, int)
{
	return 0;
}

static void
putc(Uart *, int c)
{
	sbiputc((char)c);
}

static int
getc(Uart *)
{
	int sbigetc(void);
	return sbigetc();
}

void
uartconsinit(void)
{
	sbiputc('u');
	consuart = &sbiuart;
	sbiputc('V');
	consuart->console = 1;
	sbiputc('X');
	uartctl(consuart, "l8 pn s1");
	sbiputc('Y');
	for(int i = 0; i < 15; i++) putc(consuart, "SBI USART HERE\n"[i]);
}

PhysUart sbiphysuart = {
	.name		= "sbi",
	.pnp		= nil,
	.enable		= nil,
	.disable	= nil,
	.kick		= nil,
	.dobreak	= donothing,
	.baud		= ureturn0,
	.bits		= ureturn0,
	.stop		= ureturn0,
	.parity		= ureturn0,
	.modemctl	= donothing,
	.rts		= donothing,
	.dtr		= donothing,
	.fifo		= donothing,
	.getc		= getc,
	.putc		= putc,
};
