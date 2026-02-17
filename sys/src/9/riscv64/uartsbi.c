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

enum {
	Spew = 0,
};

extern PhysUart sbiphysuart;

static Uart sbiuart = {
	.regs	= nil,
	.name	= "uart0",
	.freq	= 24*Mhz,
	.baud	= 115200,
	.phys	= &sbiphysuart,
};

static Uart*
pnp(void)
{
	return &sbiuart;
}

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
	int c = sbigetc();
	return c;
}

void
uartconsinit(void)
{
	if (Spew) sbiputc('u');
	consuart = &sbiuart;
	if (Spew) sbiputc('V');
	consuart->console = 1;
	if (Spew) sbiputc('X');
	uartctl(consuart, "l8 pn s1");
	if (Spew) sbiputc('Y');
	if (Spew) for(int i = 0; i < 15; i++) putc(consuart, "SBI USART HERE\n"[i]);
}

static void
interrupt(void)
{
	int c;
	while ((c = getc(nil)) != -1) {
		if (Spew) sbiputc('I');
		uartrecv(&sbiuart, (u8int)c);
	}
	
}

static int sbiready = 0;
static void
enable(Uart *, int)
{
	print("SBI UART ENABLE\n");
	addclock0link(interrupt, 100);
	sbiready = 1;
}

static void
kick(Uart *uart)
{
	if (Spew) sbiputc('1');
	if (Spew) sbiputc('2');
	coherence();
	if (Spew) sbiputc('3');
	while(1) {
		if (Spew) sbiputc('4');
		if(uart->op >= uart->oe && uartstageoutput(uart) == 0)
			break;
		if (Spew) sbiputc('5');		sbiputc(*(uart->op++));
	}
	if (Spew) sbiputc('6');
	coherence();
}


PhysUart sbiphysuart = {
	.name		= "sbi",
	.pnp		= pnp,
	.enable		= enable,
	.disable	= nil,
	.kick		= kick,
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
