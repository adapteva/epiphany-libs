/*
The MIT License (MIT)

Copyright (c) 2016 Adapteva, Inc

Contributed by Ola Jeppsson <ola@adapteva.com>

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

#include <e-hal.h>

void usage()
{
	printf(
"Usage: e-dump-regs [-s] <row> <col>\n"
"   -s             - print only special core registers\n"
"   row            - target core row coordinate\n"
"   col            - target core column coordinate\n");
}

char *scr_names[1 + E_REG_RMESHROUTE - E_REG_CONFIG] = {
	[E_REG_CONFIG		- E_REG_CONFIG]	= "config",
	[E_REG_STATUS		- E_REG_CONFIG]	= "status",
	[E_REG_PC			- E_REG_CONFIG]	= "pc",
	[E_REG_DEBUGSTATUS	- E_REG_CONFIG]	= "debugstatus",
	[E_REG_LC			- E_REG_CONFIG]	= "lc",
	[E_REG_LS			- E_REG_CONFIG]	= "ls",
	[E_REG_LE			- E_REG_CONFIG]	= "le",
	[E_REG_IRET			- E_REG_CONFIG]	= "iret",
	[E_REG_IMASK		- E_REG_CONFIG]	= "imask",
	[E_REG_ILAT			- E_REG_CONFIG]	= "ilat",
	[E_REG_ILATST		- E_REG_CONFIG]	= "ilatst",
	[E_REG_ILATCL		- E_REG_CONFIG]	= "ilatcl",
	[E_REG_IPEND		- E_REG_CONFIG]	= "ipend",
	[E_REG_CTIMER0		- E_REG_CONFIG]	= "ctimer0",
	[E_REG_CTIMER1		- E_REG_CONFIG]	= "ctimer1",
	[E_REG_FSTATUS		- E_REG_CONFIG]	= "fstatus",
	[E_REG_DEBUGCMD		- E_REG_CONFIG]	= "debugcmd",
	[E_REG_DMA0CONFIG	- E_REG_CONFIG]	= "dma0config",
	[E_REG_DMA0STRIDE	- E_REG_CONFIG]	= "dma0stride",
	[E_REG_DMA0COUNT	- E_REG_CONFIG]	= "dma0count",
	[E_REG_DMA0SRCADDR	- E_REG_CONFIG]	= "dma0srcaddr",
	[E_REG_DMA0DSTADDR	- E_REG_CONFIG]	= "dma0dstaddr",
	[E_REG_DMA0AUTODMA0	- E_REG_CONFIG]	= "dma0autodma0",
	[E_REG_DMA0AUTODMA1	- E_REG_CONFIG]	= "dma0autodma1",
	[E_REG_DMA0STATUS	- E_REG_CONFIG]	= "dma0status",
	[E_REG_DMA1CONFIG	- E_REG_CONFIG]	= "dma1config",
	[E_REG_DMA1STRIDE	- E_REG_CONFIG]	= "dma1stride",
	[E_REG_DMA1COUNT	- E_REG_CONFIG]	= "dma1count",
	[E_REG_DMA1SRCADDR	- E_REG_CONFIG]	= "dma1srcaddr",
	[E_REG_DMA1DSTADDR	- E_REG_CONFIG]	= "dma1dstaddr",
	[E_REG_DMA1AUTODMA0	- E_REG_CONFIG]	= "dma1autodma0",
	[E_REG_DMA1AUTODMA1	- E_REG_CONFIG]	= "dma1autodma1",
	[E_REG_DMA1STATUS	- E_REG_CONFIG]	= "dma1status",
	[E_REG_MEMSTATUS	- E_REG_CONFIG]	= "memstatus",
	[E_REG_MEMPROTECT	- E_REG_CONFIG]	= "memprotect",
	[E_REG_MESHCONFIG	- E_REG_CONFIG]	= "meshconfig",
	[E_REG_COREID		- E_REG_CONFIG]	= "coreid",
	[E_REG_MULTICAST	- E_REG_CONFIG]	= "multicast",
	[E_REG_RESETCORE	- E_REG_CONFIG]	= "resetcore",
	[E_REG_CMESHROUTE	- E_REG_CONFIG]	= "cmeshroute",
	[E_REG_XMESHROUTE	- E_REG_CONFIG]	= "xmeshroute",
	[E_REG_RMESHROUTE	- E_REG_CONFIG]	= "rmeshroute",
};

void print_header()
{
	char tmp[60];
	memset(tmp, '=', 59);
	tmp[59] = '\0';

	printf("%-12s\t%-10s\n", "Register", "Value");
	printf("%-40s\n", tmp);
}

void dump_gprs(e_epiphany_t *dev)
{
	uint32_t i;
	uint32_t regs[64];

	memset(regs, 0x13, sizeof(regs));

	for (i = 0; i <= E_REG_R63 - E_REG_R0; i += 4)
		e_read(dev, 0, 0, E_REG_R0 + i, &regs[i / 4], 4);

	for (i = 0; i < 64; i++)
		printf("r%-2d%9s\t0x%08x\n", i, " ", regs[i]);
}

void dump_scrs(e_epiphany_t *dev)
{
	uint32_t i;
	uint32_t regs[1 + (E_REG_RMESHROUTE - E_REG_CONFIG) / 4];

	memset(regs, 0x13, sizeof(regs));

	for (i = 0; i <= E_REG_RMESHROUTE - E_REG_CONFIG; i += 4)
		e_read(dev, 0, 0, E_REG_CONFIG + i, &regs[i / 4], 4);

	for (i = 0; i <= E_REG_RMESHROUTE - E_REG_CONFIG; i += 4)
		if (scr_names[i])
			printf("%-12s\t0x%08x\n", scr_names[i], regs[i / 4]);
}

int main(int argc, char *argv[])
{
	bool scr_only = false;
	e_epiphany_t dev;
	e_platform_t platform;
	int row, col, arg = 1;

	if (argc < 3 || 4 < argc) {
		usage();
		exit(1);
	}

	if (argc == 4 && !strcmp(argv[arg], "-s")) {
		scr_only = true;
		arg++;
	}

	row = atoi(argv[arg++]);
	col = atoi(argv[arg++]);

	e_init(NULL);
	e_get_platform_info(&platform);
	e_open(&dev, row, col, 1, 1);

	print_header();

	if (!scr_only)
		dump_gprs(&dev);

	dump_scrs(&dev);

	e_close(&dev);
	e_finalize();

	return 0;
}
