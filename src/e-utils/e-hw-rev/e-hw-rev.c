/*
  File: read_hw_ver.c
*/

#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>

//#include "e-hal.h"

//bool e_is_on_chip(unsigned coreid);

#define diag(vN)   if (e_host_verbose >= vN)

static int e_host_verbose = 0;
static FILE *fd;

typedef struct {
	off_t           phy_base;    // physical global base address of memory region
	size_t          map_size;    // size of mapped region
	off_t           map_mask;    // for mmap
	void           *mapped_base; // for mmap
	void           *base;        // application space base address of memory region
} Epiphany_mmap_t;


typedef struct {
	int             memfd;       // for mmap
	Epiphany_mmap_t esys;        // e-system registers data structure
} Epiphany_t;



#ifndef EPI_OK
#	define EPI_OK     0
#	define EPI_ERR    1
#	define EPI_WARN   2
#endif


#define ESYS_BASE_REGS 0x808f0000

// Epiphany System Registers
#define ESYS_CONFIG    0x0f00
#define ESYS_RESET     0x0f04
#define ESYS_VERSION   0x0f08
#define ESYS_FILTERL   0x0f0c
#define ESYS_FILTERH   0x0f10
#define ESYS_FILTERC   0x0f14
#define ESYS_TIMEOUT   0x0f18



int main(int argc, char *argv[])
{
	Epiphany_t *dev, Epiphany;
	unsigned int hw_rev;


	fprintf(stderr, "Page size = %d (0x%x)\n", getpagesize(), (unsigned int) sysconf(_SC_PAGESIZE));


	dev = &Epiphany;

	e_open(dev);

	hw_rev = e_read_esys(dev, ESYS_VERSION);
	fprintf(stderr, "Epiphany Hardware Revision: %02x.%02x.%02x.%02x\n", (hw_rev>>24)&0xff, (hw_rev>>16)&0xff, (hw_rev>>8)&0xff, (hw_rev>>0)&0xff);

	e_close(dev);

	return 0;
}


/////////////////////////////////
// Device communication functions
//
// Epiphany access
int e_open(Epiphany_t *dev)
{
	uid_t UID;
	int i;

	UID = getuid();
	if (UID != 0)
	{
		warnx("e_open(): Program must be invoked with superuser privilege (sudo).");
		return EPI_ERR;
	}

	// Open memory device
	dev->memfd = open("/dev/mem", O_RDWR | O_SYNC);
	if (dev->memfd == 0)
	{
		warnx("e_open(): /dev/mem file open failure.");
		return EPI_ERR;
	}

	// e-sys regs
	dev->esys.phy_base = ESYS_BASE_REGS;
	dev->esys.map_size = 0x1000;
	dev->esys.map_mask = (dev->esys.map_size - 1);

	dev->esys.mapped_base = mmap(0, dev->esys.map_size, PROT_READ|PROT_WRITE, MAP_SHARED,
	                          dev->memfd, dev->esys.phy_base & ~dev->esys.map_mask);
	dev->esys.base = dev->esys.mapped_base + (dev->esys.phy_base & dev->esys.map_mask);

	if ((dev->esys.mapped_base == MAP_FAILED))
	{
		warnx("e_open(): ESYS mmap failure.");
		return EPI_ERR;
	}

	return EPI_OK;
}


int e_close(Epiphany_t *dev)
{
	munmap(dev->esys.mapped_base, dev->esys.map_size);

	close(dev->memfd);

	return EPI_OK;
}


int e_read_esys(Epiphany_t *dev, const off_t from_addr)
{
	volatile int *pfrom;
	int           data;

	pfrom = (int *) (dev->esys.base + (from_addr & dev->esys.map_mask));
	data  = *pfrom;

	return data;
}

