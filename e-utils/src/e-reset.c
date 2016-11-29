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

#include <e-hal.h>

e_platform_t e_pl;
e_epiphany_t Epiphany, *pEpiphany;

int main(int argc, char *argv[])
{
	int irow, icol;
	unsigned cid, cfg, stat, pc;
	e_bool_t testme;

	pEpiphany = &Epiphany;
	e_set_host_verbosity(H_D0);

	e_init(NULL);

	if ((argc > 1) && (!strcmp(argv[1], "-t")))
	{
		testme = E_TRUE;

		e_get_platform_info(&e_pl);
		if (e_open(pEpiphany, 0, 0, e_pl.rows, e_pl.cols))
		{
			fprintf(stderr, "\nERROR: Can't establish connection to Epiphany device!\n\n");
			exit(1);
		}
	} else {
		testme = E_FALSE;
	}

	if (testme)
	{
		for (irow=0; irow<Epiphany.rows; irow++)
			for (icol=0; icol<Epiphany.cols; icol++)
			{
				cfg = 1;
				e_write(pEpiphany, irow, icol, E_REG_CONFIG, &cfg, sizeof(unsigned));
				e_read(pEpiphany, irow, icol, E_REG_COREID, &cid,  sizeof(unsigned));
				e_read(pEpiphany, irow, icol, E_REG_CONFIG, &cfg,  sizeof(unsigned));
				e_read(pEpiphany, irow, icol, E_REG_STATUS, &stat, sizeof(unsigned));
				e_read(pEpiphany, irow, icol, E_REG_PC,     &pc,   sizeof(unsigned));

				fprintf(stderr, "CoreID = 0x%03x  CONFIG = 0x%08x  STATUS = 0x%08x  PC = 0x%08x\n", cid, cfg, stat, pc);
			}
	}

	if (testme) fprintf(stderr, "Resetting ESYS... \n");

	e_reset_system();

	if (testme) fprintf(stderr, "Done.\n");

	if (testme)
	{
		for (irow=0; irow<Epiphany.rows; irow++)
			for (icol=0; icol<Epiphany.cols; icol++)
			{
				e_read(pEpiphany, irow, icol, E_REG_COREID, &cid,  sizeof(unsigned));
				e_read(pEpiphany, irow, icol, E_REG_CONFIG, &cfg,  sizeof(unsigned));
				e_read(pEpiphany, irow, icol, E_REG_STATUS, &stat, sizeof(unsigned));
				e_read(pEpiphany, irow, icol, E_REG_PC,     &pc,   sizeof(unsigned));

				fprintf(stderr, "CoreID = 0x%03x  CONFIG = 0x%08x  STATUS = 0x%08x  PC = 0x%08x\n", cid, cfg, stat, pc);
			}

		e_close(pEpiphany);
	}

	e_finalize();

	return 0;
}

