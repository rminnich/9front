#include "u.h"
#include "mem.h"

enum {
	/* ARM SBSA */
	Wdogwcs = 0x00,
};

static void
wdogoff(void)
{
	u32int *r = (u32int *)(VIRTIO+0x13a0000ULL);

	/* Doghouse (Disable) */
	r[Wdogwcs] = 0;
}

void
archlx2klink(void)
{
	wdogoff();
}
