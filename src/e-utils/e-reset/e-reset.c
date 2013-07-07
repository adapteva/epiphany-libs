#include <stdio.h>
#include <stdlib.h>

#include <e-hal.h>

e_platform_t e_pl;
e_epiphany_t Epiphany, *pEpiphany;

int main(int argc, char *argv[])
{
	int irow, icol;
	unsigned cid, cfg, stat, pc, imask;
	e_bool_t testme;

	pEpiphany = &Epiphany;
	e_set_host_verbosity(H_D0);

	e_init(NULL);

	if ((argc > 1) && (!strcmp(argv[1], "-t")))
	{
		testme = e_true;

		e_get_platform_info(&e_pl);
		if (e_open(pEpiphany, 0, 0, e_pl.rows, e_pl.cols))
		{
			fprintf(stderr, "\nERROR: Can't establish connection to Epiphany device!\n\n");
			exit(1);
		}
	} else {
		testme = e_false;
	}

	if (testme)
	{
		for (irow=0; irow<Epiphany.rows; irow++)
			for (icol=0; icol<Epiphany.cols; icol++)
			{
				cfg = 1;
				e_write(pEpiphany, irow, icol, E_CONFIG, &cfg, sizeof(unsigned));
				e_read(pEpiphany, irow, icol, E_COREID, &cid,  sizeof(unsigned));
				e_read(pEpiphany, irow, icol, E_CONFIG, &cfg,  sizeof(unsigned));
				e_read(pEpiphany, irow, icol, E_STATUS, &stat, sizeof(unsigned));
				e_read(pEpiphany, irow, icol, E_PC,     &pc,   sizeof(unsigned));

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
				e_read(pEpiphany, irow, icol, E_COREID, &cid,  sizeof(unsigned));
				e_read(pEpiphany, irow, icol, E_CONFIG, &cfg,  sizeof(unsigned));
				e_read(pEpiphany, irow, icol, E_STATUS, &stat, sizeof(unsigned));
				e_read(pEpiphany, irow, icol, E_PC,     &pc,   sizeof(unsigned));

				fprintf(stderr, "CoreID = 0x%03x  CONFIG = 0x%08x  STATUS = 0x%08x  PC = 0x%08x\n", cid, cfg, stat, pc);
			}

		e_close(pEpiphany);
	}

	e_finalize();

	return 0;
}

