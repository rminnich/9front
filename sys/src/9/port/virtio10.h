enum {
	/* §2.1 Device Status Field */
	Sacknowledge = 1,
	Sdriver = 2,
	Sdriverok = 4,
	Sfeaturesok = 8,
	Sfailed = 128,

	/* feat[1] bits */
	Fversion1 = 1<<(32-32),

	Vconf_devfeatsel = 0,
	Vconf_devfeat = 4,
	Vconf_drvfeatsel = 8,
	Vconf_drvfeat = 12,
	Vconf_msixcfg = 16,
	Vconf_nqueues = 18,
	Vconf_status = 20,
	Vconf_cfggen = 21,
	Vconf_queuesel = 22,
	Vconf_queuesize = 24,
	Vconf_queuemsixvect = 26,
	Vconf_queueenable = 28,
	Vconf_queuenotifyoff = 30,
	Vconf_queuedesc = 32,
	Vconf_queueavail = 40,
	Vconf_queueused = 48,
	Vconf_sz = 56,

	Vio_port = 0,
	Vio_mem,
};

typedef struct Vio Vio;
struct Vio
{
	int type;
	union {
		int port;
		uchar *mem;
	};
};

typedef struct Vring Vring;
struct Vring
{
	u16int	flags;
	u16int	idx;
};

typedef struct Vdesc Vdesc;
struct Vdesc
{
	u64int	addr;
	u32int	len;
	u16int	flags;
	u16int	next;
};

typedef struct Vused Vused;
struct Vused
{
	u32int	id;
	u32int	len;
};

/* machine dependent functions
for most archs this is provided by port/virtio10mem.c which does pci mem bar only
except for x86 which has to deal with io space and mem space in pc/virtio10pc.c 
*/
u8int vin8(Vio *, int);
u16int vin16(Vio *, int);
u32int vin32(Vio *, int);
u64int vin64(Vio *, int);
void vout8(Vio *, int, u8int);
void vout16(Vio *, int, u16int);
void vout32(Vio *, int, u32int);
void vout64(Vio *, int, u64int);
void virtiounmap(Vio *, usize);
Vio* virtiomapregs(Pcidev *, int, int, Vio *);
