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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "e-hal.h"

const unsigned EMEM_SIZE = 0x02000000;

void usage();
void trim_str(char *a);

typedef struct {
	e_bool_t verbose;
	e_bool_t raw;
} prtopt_t;

prtopt_t prtopt = {E_FALSE, E_FALSE};

int main(int argc, char *argv[])
{
	e_epiphany_t edev;
	e_mem_t      emem;
	void         *dev;
	e_platform_t plat;
	e_bool_t isexternal;
	int row, col, i, args, iarg, opts;
	unsigned addr, inval;
	char buf[256];

	iarg = 1;
	opts = 0;

	if (argc < 3)
	{
		usage();
		exit(1);
	} else if (!strcmp(argv[iarg], "-v"))
	{
		prtopt.verbose = E_TRUE;
		prtopt.raw     = E_FALSE;
		iarg++;
		opts++;
	} else if (!strcmp(argv[iarg], "-r"))
	{
		prtopt.verbose = E_FALSE;
		prtopt.raw     = E_TRUE;
		iarg++;
		opts++;
	}

	row = atoi(argv[iarg++]);

	e_set_host_verbosity(H_D0);
	e_init(NULL);
	e_get_platform_info(&plat);

	if (row < 0)
	{
		isexternal = E_TRUE;
	} else {
		isexternal = E_FALSE;
	}

	if (isexternal)
	{
		sscanf(argv[iarg++], "%x", &addr);
		addr = (addr >> 2) << 2;
		e_alloc(&emem, (off_t) 0x0, EMEM_SIZE);
		dev = &emem;
		args = 4 + opts;
		if (prtopt.verbose) printf("Writing to external memory buffer at offset 0x%x.\n", addr);
	} else {
		col = atoi(argv[iarg++]);
		if ((row >= plat.rows) || (col >= plat.cols) || (col < 0))
		{
			printf("Core coordinates exceed platform boundaries!\n");
			e_finalize();
			exit(1);
		}

		sscanf(argv[iarg++], "%x", &addr);
		addr = (addr >> 2) << 2;
		e_open(&edev, row, col, 1, 1);
		dev = &edev;
		args = 5 + opts;
		if (prtopt.verbose) printf("Writing to core (%d,%d) at offset 0x%x.\n", row, col, addr);
	}


	if (argc < args)
		do {
			printf("[0x%08x] = ", addr);
			gets(buf);
			trim_str(buf);
			if (strlen(buf) == 0)
				break;
			sscanf(buf, "%x", &inval);
			e_write(dev, 0, 0, addr, &inval, sizeof(inval));
			addr = addr + sizeof(int);
		} while (1);
	else
		for (i=iarg; i<argc; i++)
		{
			sscanf(argv[i], "%x", &inval);
			printf("[0x%08x] = 0x%08x\n", addr, inval);
			e_write(dev, 0, 0, addr, &inval, sizeof(inval));
			addr = addr + sizeof(int);
		}


	if (isexternal)
		e_free(&emem);
	else
		e_close(&edev);
	e_finalize();

	return 0;
}


void usage()
{
	printf("Usage: e-write [-v] <row> [<col>] <address> [<val0> <val1> ...]\n");
	printf("   row            - target core row coordinate, or (-1) for ext. memory.\n");
	printf("   col            - target core column coordinate. if row is (-1) skip this parameter.\n");
	printf("   address        - base address of destination array of words (32-bit hex)\n");
	printf("   val0,val1,...  - data words to write to destination (32-bit hex).\n");
	printf("                    If none specified, input is taken interactively, one\n");
	printf("                    word at a time until an empty input is received.\n");
	printf("   -v             - verbose mode. Print more information.\n");

	return;
}


void trim_str(char *a)
{
    char *b = a;
    while (isspace(*b))   ++b;
    while (*b)            *a++ = *b++;
    *a = '\0';
    while (isspace(*--a)) *a = '\0';
}

