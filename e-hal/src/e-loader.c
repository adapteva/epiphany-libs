/*
  File: e-loader.c

  This file is part of the Epiphany Software Development Kit.

  Copyright (C) 2013 Adapteva, Inc.
  See AUTHORS for list of contributors.
  Support e-mail: <support@adapteva.com>

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
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <err.h>
#include <elf.h>

#include "e-loader.h"
#include "esim-target.h"

typedef unsigned int uint;

#define diag(vN)   if (e_load_verbose >= vN)

static e_return_stat_t ee_process_ELF(const char *executable, e_epiphany_t *pEpiphany, e_mem_t *pEMEM, int row, int col);
static int ee_set_core_config(e_epiphany_t *pEpiphany, e_mem_t *pEMEM, int row, int col);

e_loader_diag_t e_load_verbose = 0;

/* diag_fd is set by e_set_loader_verbosity() */
FILE *diag_fd = NULL;

// TODO: replace with platform data
#define EMEM_SIZE (0x02000000)

int e_load(const char *executable, e_epiphany_t *dev, unsigned row, unsigned col, e_bool_t start)
{
	int status;

	status = e_load_group(executable, dev, row, col, 1, 1, start);

	return status;
}


int e_load_group(const char *executable, e_epiphany_t *dev, unsigned row, unsigned col, unsigned rows, unsigned cols, e_bool_t start)
{
	e_mem_t      emem, *pemem;
	unsigned int irow, icol;
	int          status;
	FILE        *fp;
	char         hdr[5] = {'\0', '\0', '\0', '\0', '\0'};
	char         elfHdr[4] = {ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3};
	char         srecHdr[2] = {'S', '0'};
	e_bool_t     iself;
	e_return_stat_t retval;

#ifndef ESIM_TARGET
	if (esim_target_p()) {
		warnx("e_load_group(): " EHAL_TARGET_ENV " environment variable set to esim but target not compiled in.");
		return E_ERR;
	}
#endif

	status = E_OK;
	iself  = E_FALSE;

	pemem = &emem;

	if (dev && pemem)
	{
		// Allocate External DRAM for the epiphany executable code
		// TODO: this is barely scalable. Really need to test ext. mem size to load
		// and possibly split the ext. mem accesses into 1MB chunks.
		if (e_alloc(pemem, 0, EMEM_SIZE))
		{
			warnx("\nERROR: Can't allocate external memory buffer!\n\n");
			return E_ERR;
		}

		if (executable[0] != '\0')
		{
			if ( (fp = fopen(executable, "rb")) != NULL )
			{
				fseek(fp, 0, SEEK_SET);
				fread(hdr, 1, 4, fp);
				fclose(fp);
			} else {
				warnx("ERROR: Can't open executable file \"%s\".\n", executable);
				e_free(pemem);
				return E_ERR;
			}

			if (!strncmp(hdr, elfHdr, 4))
			{
				iself = E_TRUE;
				diag(L_D1) { fprintf(diag_fd, "e_load_group(): loading ELF file %s ...\n", executable); }
			}
			else if (!strncmp(hdr, srecHdr, 2))
			{
				diag(L_D1) { fprintf(diag_fd, "e_load_group(): ERROR: SREC support removed\n"); }
				warnx("ERROR: SREC file support is deprecated. Use elf format.\n");
				e_free(pemem);
				return E_ERR;
			}
			else
			{
				diag(L_D1) { fprintf(diag_fd, "e_load_group(): Executable header %02x %02x %02x %02x\n",
									 hdr[0], hdr[1], hdr[2], hdr[3]); }
				warnx("ERROR: Can't load executable file: unidentified format.\n");
				e_free(pemem);
				return E_ERR;
			}

			for (irow=row; irow<(row+rows); irow++)
				for (icol=col; icol<(col+cols); icol++)
				{
					if (iself)
						retval = ee_process_ELF(executable, dev, pemem, irow, icol);
					else
						retval = E_ERR;

					if (retval == E_ERR)
					{
						warnx("ERROR: Can't load executable file \"%s\".\n", executable);
						e_free(pemem);
						return E_ERR;
					} else
						ee_set_core_config(dev, pemem, irow, icol);
				}

			if (start)
				for (irow=row; irow<(row+rows); irow++)
					for (icol=col; icol<(col + cols); icol++)
						{
							diag(L_D1) { fprintf(diag_fd, "e_load_group(): send SYNC signal to core (%d,%d)...\n", irow, icol); }
							e_start(dev, irow, icol);
							diag(L_D1) { fprintf(diag_fd, "e_load_group(): done.\n"); }
						}

			diag(L_D1) { fprintf(diag_fd, "e_load_group(): done loading.\n"); }
		}

		e_free(pemem);
		diag(L_D1) { fprintf(diag_fd, "e_load_group(): closed connection.\n"); }
	}
	else
	{
		warnx("ERROR: Can't connect to Epiphany or external memory.\n");
		return E_ERR;
	}

	diag(L_D1) { fprintf(diag_fd, "e_load_group(): leaving loader.\n"); }

	return status;
}


int ee_set_core_config(e_epiphany_t *pEpiphany, e_mem_t *pEMEM, int row, int col)
{
	e_group_config_t *pgrpcfg  = (void *) SIZEOF_IVT;
	e_emem_config_t  *pememcfg = (void *) SIZEOF_IVT + sizeof(e_group_config_t);

	e_group_config_t e_group_config;
	e_emem_config_t  e_emem_config;

	e_group_config.objtype     = E_EPI_GROUP;
	e_group_config.chiptype    = pEpiphany->type;
	e_group_config.group_id    = pEpiphany->base_coreid;
	e_group_config.group_row   = pEpiphany->row;
	e_group_config.group_col   = pEpiphany->col;
	e_group_config.group_rows  = pEpiphany->rows;
	e_group_config.group_cols  = pEpiphany->cols;
	e_group_config.core_row    = row;
	e_group_config.core_col    = col;

	e_group_config.alignment_padding = 0xdeadbeef;

	e_emem_config.objtype   = E_EXT_MEM;
	e_emem_config.base      = pEMEM->ephy_base;

	e_write(pEpiphany, row, col, (off_t) pgrpcfg,  &e_group_config, sizeof(e_group_config_t));
	e_write(pEpiphany, row, col, (off_t) pememcfg, &e_emem_config,  sizeof(e_emem_config_t));

	return 0;
}


e_loader_diag_t e_set_loader_verbosity(e_loader_diag_t verbose)
{
	e_loader_diag_t old_load_verbose;

	old_load_verbose = e_load_verbose;
	diag_fd = stderr;
	e_load_verbose = verbose;
	diag(L_D1) { fprintf(diag_fd, "e_set_loader_verbosity(): setting loader verbosity to %d.\n", verbose); }
	e_set_host_verbosity(verbose);

	return old_load_verbose;
}

static e_return_stat_t ee_process_ELF(const char *executable, e_epiphany_t *pEpiphany, e_mem_t *pEMEM, int row, int col)
{
	FILE       *elfStream;
	Elf32_Ehdr hdr;
	Elf32_Phdr *phdr;
	e_bool_t   islocal, isonchip;
	int        ihdr;
	void       *pto;
	unsigned   globrow, globcol;
	unsigned   CoreID;
	int        status = E_OK;
	void      *buf = NULL;
	uint32_t   addr;


	islocal   = E_FALSE;
	isonchip  = E_FALSE;

	elfStream = fopen(executable, "rb");

	fread(&hdr, sizeof(hdr), 1, elfStream);
	phdr = alloca(sizeof(*phdr) * hdr.e_phnum);
	fseek(elfStream, hdr.e_phoff, SEEK_SET);
	fread(phdr, sizeof(*phdr), hdr.e_phnum, elfStream);

	for (ihdr=0; ihdr<hdr.e_phnum; ihdr++)
	{
		if (phdr[ihdr].p_vaddr & 0xfff00000)
		{
			// This is a global address. Check if address is on an eCore.
			islocal  = E_FALSE;
			isonchip = e_is_addr_on_chip((void *) phdr[ihdr].p_vaddr);
		} else {
			// This is a local address.
			islocal  = E_TRUE;
			isonchip = E_TRUE;
		}

		diag(L_D3) { fprintf(diag_fd, "ee_process_ELF(): copying the data (%d bytes)", phdr[ihdr].p_filesz); }

		if (esim_target_p()) {
			addr = phdr[ihdr].p_vaddr;
			if (islocal) {
				addr |= (pEpiphany->core[row][col].id << 20);
			}
			buf = realloc(buf, phdr[ihdr].p_filesz);
			fseek(elfStream, phdr[ihdr].p_offset, SEEK_SET);
			fread(buf, phdr[ihdr].p_filesz, sizeof(char), elfStream);
			if (ES_OK != es_ops.mem_store(pEMEM->esim, addr, phdr[ihdr].p_filesz, (uint8_t *) buf)) {
				fprintf(diag_fd, "ee_process_ELF(): Error: ESIM error writing to 0x%x", addr);
				return E_ERR;
			}
		} else {
			if (islocal)
			{
				// If this is a local address
				diag(L_D3) { fprintf(diag_fd, " to core (%d,%d)\n", row, col); }
				pto = pEpiphany->core[row][col].mems.base + phdr[ihdr].p_vaddr; // TODO: should this be p_paddr instead of p_vaddr?
			}
			else if (isonchip)
			{
				// If global address, check if address is of an eCore.
				CoreID = phdr[ihdr].p_vaddr >> 20;
				ee_get_coords_from_id(pEpiphany, CoreID, &globrow, &globcol);
				diag(L_D3) { fprintf(diag_fd, " to core (%d,%d)\n", globrow, globcol); }
				pto = pEpiphany->core[globrow][globcol].mems.base + (phdr[ihdr].p_vaddr & ~(0xfff00000)); // TODO: should this be p_paddr instead of p_vaddr?
			}
			else
			{
				// If it is not on an eCore, it is on external memory.
				diag(L_D3) { fprintf(diag_fd, " to external memory.\n"); }
				pto = (void *) phdr[ihdr].p_vaddr;
				if ((phdr[ihdr].p_vaddr >= pEMEM->ephy_base) && (phdr[ihdr].p_vaddr < (pEMEM->ephy_base + pEMEM->emap_size)))
				{
					diag(L_D3) { fprintf(diag_fd, "ee_process_ELF(): converting virtual (0x%08x) ", (uint) phdr[ihdr].p_vaddr); }
					pto = pto - (pEMEM->ephy_base - pEMEM->phy_base);
					diag(L_D3) { fprintf(diag_fd, "to physical (0x%08x)...\n", (uint) phdr[ihdr].p_vaddr); }
				}
				diag(L_D3) { fprintf(diag_fd, "ee_process_ELF(): converting physical (0x%08x) ", (uint) phdr[ihdr].p_vaddr); }
				pto = pto - (uint) pEMEM->phy_base + (uint) pEMEM->base;
				diag(L_D3) { fprintf(diag_fd, "to offset (0x%08x)...\n", (uint) pto); }
			}
			fseek(elfStream, phdr[ihdr].p_offset, SEEK_SET);
			fread(pto, phdr[ihdr].p_filesz, sizeof(char), elfStream);
		}
	}

	if (esim_target_p())
		free(buf);

	fclose(elfStream);

	return status;
}
