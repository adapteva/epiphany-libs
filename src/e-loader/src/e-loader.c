/*
  File: e-loader.c

  This file is part of the Epiphany Software Development Kit.

  Copyright (C) 2013 Adapteva, Inc.
  Contributed by Oleg Raikhman, Yaniv Sapir <support@adapteva.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License (LGPL)
  as published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  and the GNU Lesser General Public License along with this program,
  see the files COPYING and COPYING.LESSER.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>

#include <sys/mman.h>
#include <fcntl.h>
#include <err.h>

#include "e-loader.h"

#define diag(vN)   if (e_load_verbose >= vN)

int ee_process_SREC(char *executable, e_epiphany_t *pEpiphany, e_mem_t *pEMEM, int row, int col);

int e_load_verbose = 0;
FILE *fd;

// TODO: replace with platform data
#define EMEM_SIZE (0x02000000)

int e_load(char *executable, e_epiphany_t *dev, unsigned row, unsigned col, e_bool_t start)
{
	int status;

	status = e_load_group(executable, dev, row, col, 1, 1, start);

	return status;
}


int e_load_group(char *executable, e_epiphany_t *dev, unsigned row, unsigned col, unsigned rows, unsigned cols, e_bool_t start)
{
	e_mem_t emem, *pemem;
	int    irow, icol;
	int    status;

	status = E_OK;

	pemem = &emem;

	if (dev && pemem)
	{
		if (e_alloc(pemem, 0, EMEM_SIZE))
		{
			fprintf(fd, "\nERROR: Can't allocate external memory buffer!\n\n");
			exit(1);
		}

		if (executable[0] != '\0')
		{
			diag(L_D1) { fprintf(fd, "e_load_group(): loading SREC file %s ...\n", executable); }

			for (irow=row; irow<(row+rows); irow++)
				for (icol=col; icol<(col+cols); icol++)
					if (ee_process_SREC(executable, dev, pemem, irow, icol) == E_ERR)
					{
						fprintf(fd, "ERROR: Can't parse SREC file.\n");
						return E_ERR;
					}

			for (irow=row; irow<(row+rows); irow++)
				for (icol=col; icol<(col + cols); icol++)
					if (start)
					{
						diag(L_D1) { fprintf(fd, "e_load_group(): send SYNC signal to core (%d,%d)...\n", irow, icol); }
						e_start(dev, irow, icol);
						diag(L_D1) { fprintf(fd, "e_load_group(): done.\n"); }
					}

			diag(L_D1) { fprintf(fd, "e_load_group(): done loading.\n"); }
		}

		diag(L_D1) { fprintf(fd, "e_load_group(): closed connection.\n"); }
	}
	else
	{
		fprintf(fd, "ERROR: Can't connect to Epiphany or external memory.\n");
		return E_ERR;
	}

	diag(L_D1) { fprintf(fd, "e_load_group(): leaving loader.\n"); }

	return status;
}



void e_set_loader_verbosity(e_loader_diag_t verbose)
{
	fd = stderr;
	e_load_verbose = verbose;
	diag(L_D1) { fprintf(fd, "e_set_loader_verbosity(): setting loader verbosity to %d.\n", verbose); }
	e_set_host_verbosity(verbose);

	return;
}

