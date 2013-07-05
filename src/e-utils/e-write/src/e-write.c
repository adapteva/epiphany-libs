#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "e-hal.h"

const unsigned EMEM_SIZE = 0x02000000;

void usage();
void trim_str(char *a);

int main(int argc, char *argv[])
{
	e_epiphany_t edev;
	e_mem_t      emem;
	void         *dev;
	e_platform_t plat;
	e_bool_t isexternal;
	int row, col, i, args, iarg;
	unsigned addr, inval;
	char buf[256];
	FILE *fe;

	iarg = 1;
	fe = stderr;
	// fe = fopen("/dev/null", "w");

	if (argc < 3)
	{
		usage();
		exit(1);
	}

	row  = atoi(argv[iarg++]);

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
		args = 4;
		printf("Writing to external memory buffer at offset 0x%x.\n", addr);
	} else {
		col = atoi(argv[iarg++]);
		sscanf(argv[iarg++], "%x", &addr);
		addr = (addr >> 2) << 2;
		e_open(&edev, row, col, 1, 1);
		dev = &edev;
		args = 5;
		printf("Writing to core (%d,%d) at offset 0x%x.\n", row, col, addr);
	}


	if (argc < args)
		do {
			gets(buf);
			trim_str(buf);
			if (strlen(buf) == 0)
				break;
			sscanf(buf, "%x", &inval);
			printf("[0x%08x] = 0x%08x\n", addr, inval);
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
	fclose(fe);

	return 0;
}


void usage()
{
	printf("Usage: e-write <row> [<col>] <address> [<val0> <val1> ...]\n");
	printf("   row            - target core row coordinate, or (-1) for ext. memory.\n");
	printf("   col            - target core column coordinate. if row is (-1) skip this parameter.\n");
	printf("   address        - base address of destination array of words (32-bit hex)\n");
	printf("   val0,val1,...  - data words to write to destination (32-bit hex).\n");
	printf("                    If none specified, input is taken interactively, one\n");
	printf("                    word at a time until an empty input is received.\n");

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

