#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"

static BIOS32si* pcibiossi;

static int
pcicfgrw8bios(int tbdf, int rno, int data, int read)
{
	BIOS32ci ci;

	memset(&ci, 0, sizeof(BIOS32ci));
	ci.ebx = (BUSBNO(tbdf)<<8)|(BUSDNO(tbdf)<<3)|BUSFNO(tbdf);
	ci.edi = rno;
	if(read){
		ci.eax = 0xB108;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return ci.ecx & 0xFF;
	}
	else{
		ci.eax = 0xB10B;
		ci.ecx = data & 0xFF;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return 0;
	}

	return -1;
}

static int
pcicfgrw16bios(int tbdf, int rno, int data, int read)
{
	BIOS32ci ci;

	memset(&ci, 0, sizeof(BIOS32ci));
	ci.ebx = (BUSBNO(tbdf)<<8)|(BUSDNO(tbdf)<<3)|BUSFNO(tbdf);
	ci.edi = rno;
	if(read){
		ci.eax = 0xB109;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return ci.ecx & 0xFFFF;
	}
	else{
		ci.eax = 0xB10C;
		ci.ecx = data & 0xFFFF;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return 0;
	}

	return -1;
}

static int
pcicfgrw32bios(int tbdf, int rno, int data, int read)
{
	BIOS32ci ci;

	memset(&ci, 0, sizeof(BIOS32ci));
	ci.ebx = (BUSBNO(tbdf)<<8)|(BUSDNO(tbdf)<<3)|BUSFNO(tbdf);
	ci.edi = rno;
	if(read){
		ci.eax = 0xB10A;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return ci.ecx;
	}
	else{
		ci.eax = 0xB10D;
		ci.ecx = data;
		if(!bios32ci(pcibiossi, &ci)/* && !(ci.eax & 0xFF)*/)
			return 0;
	}

	return -1;
}

int
pcibiosinit(int *maxdno, int *maxbno)
{
	BIOS32ci ci;

	pcibiossi = bios32open("$PCI");
	if(pcibiossi == nil)
		return -1;

	memset(&ci, 0, sizeof(BIOS32ci));
	ci.eax = 0xB101;
	if(bios32ci(pcibiossi, &ci) || ci.edx != ((' '<<24)|('I'<<16)|('C'<<8)|'P')){
		bios32close(pcibiossi);
		pcibiossi = nil;
		return -1;
	}
	if(ci.eax & 0x01)
		*maxdno = 31;
	else
		*maxdno = 15;
	*maxbno = ci.ecx & 0xff;

	pcicfgrw8 = pcicfgrw8bios;
	pcicfgrw16 = pcicfgrw16bios;
	pcicfgrw32 = pcicfgrw32bios;

	return 0;
}
