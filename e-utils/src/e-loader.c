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

#include "e-hal.h"
#include "e-loader.h"

void usage();

int main(int argc, char *argv[])
{
	char eprog[255];
	e_bool_t istart;
	e_epiphany_t dev;
	e_platform_t plat;
	unsigned row, col, rows, cols;
	int iarg, iiarg;

	if (E_OK != e_init(NULL))
	{
		fprintf(stderr, "Epiphany HAL initialization failed\n");
		exit(EXIT_FAILURE);
	}

	if (E_OK != e_get_platform_info(&plat))
	{
		fprintf(stderr, "Failed to get Epiphany platform info\n");
		exit(EXIT_FAILURE);
	}

	istart = E_FALSE;
	row  = 0;
	col  = 0;
	rows = cols  = 1;
	iarg = iiarg = 1;

	while (iiarg < argc)
	{
		if        (!strcmp(argv[iiarg], "-h") || !strcmp(argv[iiarg], "--help"))
		{
			usage();
			return 0;
		} else if (!strcmp(argv[iiarg], "-r") || !strcmp(argv[iiarg], "--reset"))
		{
			/* Deprecated: no-op */
			iarg++;
		} else if (!strcmp(argv[iiarg], "-s") || !strcmp(argv[iiarg], "--start"))
		{
			istart = E_TRUE;
			iarg++;
		}
		iiarg++;
	}

	switch (argc - iarg)
	{
	case 5:
		rows = atoi(argv[iarg+3]);
		cols = atoi(argv[iarg+4]);
	case 3:
		row  = atoi(argv[iarg+1]);
		col  = atoi(argv[iarg+2]);
	case 1:
		strncpy(eprog, argv[iarg], 254);
		break;
	default:
		usage();
		exit(1);
	}


	if (E_OK != e_reset_system()) {
		fprintf(stderr, "Failed to reset Epiphany system\n");
		exit(EXIT_FAILURE);
	}

	if (E_OK != e_open(&dev, row, col, rows, cols))
	{
		fprintf(stderr, "Failed to open Epiphany workgroup\n");
		exit(EXIT_FAILURE);
	}

	printf("Loading program \"%s\" on cores (%d,%d)-(%d,%d)\n", eprog, row, col, (row+rows-1), (col+cols-1));

	e_set_loader_verbosity(L_D1);

	if (E_OK != e_load_group(eprog, &dev, 0, 0, rows, cols, istart))
	{
		fprintf(stderr, "Failed loading program to group\n");
		exit(EXIT_FAILURE);
	}

	e_close(&dev);

	return 0;
}


void usage()
{
	printf("Usage: e-loader [-s|--start] [-h|--help] <e-program> [<row> <col> [<rows> <cols>]]\n");
	printf("   -s,--start  - run the programs after loading on the cores.\n");
	printf("   row,col     - (absolute) core coordinates to load program (default is 0,0).\n");
	printf("   rows,cols   - size of core workgroup to load program (default is 1,1).\n");

	return;
}
