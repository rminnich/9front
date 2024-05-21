enum {
	IRQfiq		= -1,

	PPI		= 16,
	SPI		= 32,

	IRQcntps	= PPI+13,
	IRQcntpns	= PPI+14,

	IRQuart		= SPI+32,

	IRQusb1		= SPI+80,
	IRQusb2		= SPI+81,

	IRQpci3		= SPI+119,
	IRQpci5		= SPI+129,
};

#define BUSUNKNOWN (-1)
#define PCIWINDOW	0
#define	PCIWADDR(x)	(PADDR(x)+PCIWINDOW)
