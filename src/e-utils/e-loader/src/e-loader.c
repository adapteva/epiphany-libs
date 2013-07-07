#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "e-hal.h"

void usage();

int main(int argc, char *argv[])
{
	char eprog[255];
	e_bool_t ireset, istart;
	e_epiphany_t dev;
	e_platform_t plat;
	unsigned row, col, rows, cols;
	int iarg, iiarg;

	e_get_platform_info(&plat);
	ireset = E_FALSE;
	istart = E_FALSE;
	row  = plat.row;
	col  = plat.col;
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
			ireset = E_TRUE;
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

	e_init(NULL);

	if (ireset)
		e_reset_system();

	e_open(&dev, row, col, rows, cols);

	printf("Loading program \"%s\" on cores (%d,%d)-(%d,%d)\n", eprog, row, col, (row+rows-1), (col+cols-1));

	e_set_loader_verbosity(L_D1);
	e_load_group(eprog, &dev, 0, 0, rows, cols, istart);

	e_close(&dev);
	e_finalize();

	return 0;
}


void usage()
{
	printf("Usage: e-loader [-r|--reset] [-s|--start] [-h|--help] <e-program> [<row> <col> [<rows> <cols>]]\n");
	printf("   -r,--reset  - perform a full hardware reset of the Epiphany platform.\n");
	printf("   -s,--start  - run the programs after loading on the cores.\n");
	printf("   row,col     - (absolute) core coordinates to load program (default is 0,0).\n");
	printf("   rows,cols   - size of core workgroup to load program (default is 1,1).\n");

	return;
}
