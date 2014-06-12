/*
  The MIT License (MIT)

  Copyright (c) 2013 Adapteva, Inc

  Contributed by Yaniv Sapir <support@adapteva.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

//#include "e-hal.h"

//bool e_is_on_chip(unsigned coreid);

#define diag(vN)   if (e_host_verbose >= vN)

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
#   define EPI_OK     0
#   define EPI_ERR    1
#   define EPI_WARN   2
#endif

#define EPIPHANY_DEVICE         "/dev/epiphany"
#define ESYS_BASE               0x80800000
#define ESYS_REGS_OFFSET        0xf0000    

// Epiphany System Registers
#define ESYS_CONFIG    0x0f00
#define ESYS_RESET     0x0f04
#define ESYS_VERSION   0x0f08
#define ESYS_FILTERL   0x0f0c
#define ESYS_FILTERH   0x0f10
#define ESYS_FILTERC   0x0f14
#define ESYS_TIMEOUT   0x0f18


int e_open(Epiphany_t *);
int e_close(Epiphany_t *);
int e_read_esys(Epiphany_t *, const off_t);

int main(int argc, char **argv)
{
    Epiphany_t *dev, Epiphany;
    unsigned int hw_rev;

    /* Silence unused variable warnings */
    (void)argc;
    (void)argv;

    dev = &Epiphany;

    if ( EPI_OK != e_open(dev) ) {
        warnx("main(): failed to open the epiphany device.");   
        return EPI_ERR;
    }

    hw_rev = e_read_esys(dev, ESYS_VERSION);
    printf("Epiphany Hardware Revision: %02x.%02x.%02x.%02x\n", (hw_rev>>24)&0xff,
           (hw_rev>>16)&0xff, (hw_rev>>8)&0xff, (hw_rev>>0)&0xff);

    e_close(dev);

    return 0;
}


/////////////////////////////////
// Device communication functions
//
// Epiphany access
int e_open(Epiphany_t *dev)
{
    // Open memory device
    dev->memfd = open(EPIPHANY_DEVICE, O_RDWR | O_SYNC);
    if (dev->memfd == -1)
    {
        warnx("e_open(): /dev/epiphany file open failure. errno is %s", strerror(errno));
        return EPI_ERR;
    }

    // e-sys regs
    dev->esys.phy_base = ESYS_BASE;
    dev->esys.map_size = 0x1000;
    dev->esys.map_mask = (dev->esys.map_size - 1);

    dev->esys.mapped_base = mmap(0, dev->esys.map_size, PROT_READ|PROT_WRITE, MAP_SHARED,
                                 dev->memfd, ESYS_REGS_OFFSET);
    dev->esys.base = dev->esys.mapped_base;

    if ((dev->esys.mapped_base == MAP_FAILED))
    {
        warnx("e_open(): mmap failure.");
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


int e_read_esys(Epiphany_t *dev, const off_t offset)
{
    volatile int *pfrom;
    int           data = 0;

    pfrom = (int *) (dev->esys.base + (offset & dev->esys.map_mask));
    data  = *pfrom;

    return data;
}

