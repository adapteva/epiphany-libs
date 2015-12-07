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
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <err.h>
#include <elf.h>

#include "e-loader.h"
#include "esim-target.h"


typedef unsigned long long ulong64;
#define diag(vN)   if (e_load_verbose >= vN)

extern void ee_get_coords_from_id(e_epiphany_t *dev, unsigned coreid,
								  unsigned *row, unsigned *col);

static e_return_stat_t ee_process_elf(const void *file, e_epiphany_t *dev,
									  e_mem_t *emem, int row, int col);
static int ee_set_core_config(e_epiphany_t *dev, e_mem_t *emem,
							  int row, int col);

e_loader_diag_t e_load_verbose = L_D0;

/* diag_fd is set by e_set_loader_verbosity() */
FILE *diag_fd = NULL;

// TODO: replace with platform data
#define EMEM_SIZE (0x02000000)

#define EM_ADAPTEVA_EPIPHANY   0x1223  /* Adapteva's Epiphany architecture.  */
static inline bool is_epiphany_exec_elf(Elf32_Ehdr *ehdr)
{
	return ehdr
		&& memcmp(ehdr->e_ident, ELFMAG, SELFMAG) == 0
		&& ehdr->e_ident[EI_CLASS] == ELFCLASS32
		&& ehdr->e_type == ET_EXEC
		&& ehdr->e_version == EV_CURRENT
		&& ehdr->e_machine == EM_ADAPTEVA_EPIPHANY;
}

static bool is_srec_file(const char *hdr)
{
	const char srechdr[] = {'S', '0'};
	return (memcmp(hdr, srechdr, sizeof(srechdr)) == 0);
}

int e_load(const char *executable, e_epiphany_t *dev, unsigned row, unsigned col, e_bool_t start)
{
	int status;

	status = e_load_group(executable, dev, row, col, 1, 1, start);

	return status;
}

int e_load_group(const char *executable, e_epiphany_t *dev, unsigned row, unsigned col, unsigned rows, unsigned cols, e_bool_t start)
{
	e_mem_t      emem;
	unsigned int irow, icol;
	int          status;
	int          fd;
	struct stat  st;
	void        *file;
	e_return_stat_t retval;

#ifndef ESIM_TARGET
	if (esim_target_p()) {
		warnx("e_load_group(): " EHAL_TARGET_ENV " environment variable set to esim but target not compiled in.");
		return E_ERR;
	}
#endif

	status = E_OK;

	if (!dev) {
		warnx("ERROR: Can't connect to Epiphany or external memory.\n");
		return E_ERR;
	}

	// Allocate External DRAM for the epiphany executable code
	// TODO: this is barely scalable. Really need to test ext. mem size to load
	// and possibly split the ext. mem accesses into 1MB chunks.
	if (e_alloc(&emem, 0, EMEM_SIZE)) {
		warnx("\nERROR: Can't allocate external memory buffer!\n\n");
		return E_ERR;
	}

	fd = open(executable, O_RDONLY);
	if (fd == -1) {
		warnx("ERROR: Can't open executable file \"%s\".\n", executable);
		e_free(&emem);
		return E_ERR;
	}

	if (fstat(fd, &st) == -1) {
		warnx("ERROR: Can't stat file \"%s\".\n", executable);
		close(fd);
		e_free(&emem);
		return E_ERR;
    }

	file = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (file == MAP_FAILED) {
		warnx("ERROR: Can't mmap file \"%s\".\n", executable);
		close(fd);
		e_free(&emem);
		return E_ERR;
    }

	if (is_epiphany_exec_elf((Elf32_Ehdr *) file)) {
		diag(L_D1) { fprintf(diag_fd, "e_load_group(): loading ELF file %s ...\n", executable); }
	} else if (is_srec_file((char *) file)) {
		diag(L_D1) { fprintf(diag_fd, "e_load_group(): ERROR: SREC support removed\n"); }
		warnx("ERROR: SREC file support is deprecated. Use elf format.\n");
		status = E_ERR;
		goto out;
	} else {
		diag(L_D1) { fprintf(diag_fd, "e_load_group(): ERROR: unidentified file format\n"); }
		warnx("ERROR: Can't load executable file: unidentified format.\n");
		status = E_ERR;
		goto out;
	}

	for (irow=row; irow<(row+rows); irow++) {
		for (icol=col; icol<(col+cols); icol++) {
			retval = ee_process_elf(file, dev, &emem, irow, icol);
			if (retval == E_ERR) {
				warnx("ERROR: Can't load executable file \"%s\".\n", executable);
				status = E_ERR;
				goto out;
			}

			ee_set_core_config(dev, &emem, irow, icol);
		}
	}

	if (start) {
		for (irow=row; irow<(row+rows); irow++) {
			for (icol=col; icol<(col + cols); icol++) {
				diag(L_D1) {
					fprintf(diag_fd,
							"e_load_group(): send SYNC signal to core (%d,%d)...\n",
							irow, icol); }
				e_start(dev, irow, icol);
				diag(L_D1) {fprintf(diag_fd, "e_load_group(): done.\n"); }
			}
		}
	}

	diag(L_D1) { fprintf(diag_fd, "e_load_group(): done loading.\n"); }

out:
	munmap(file, st.st_size);
	close(fd);
	e_free(&emem);

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

#define COREID(_addr) ((_addr) >> 20)
static inline bool is_local(uint32_t addr)
{
	return COREID(addr) == 0;
}

static bool is_valid_addr(uint32_t addr)
{
	return is_local(addr)
		|| e_is_addr_on_chip((void *) addr)
		|| e_is_addr_in_emem(addr);
}

static bool is_valid_range(uint32_t from, uint32_t size)
{
	return is_valid_addr(from) && is_valid_addr(from + size - 1);
}


static e_return_stat_t
ee_process_elf(const void *file, e_epiphany_t *dev, e_mem_t *emem,
			   int row, int col)
{
	Elf32_Ehdr *ehdr;
	Elf32_Phdr *phdr;
	bool       islocal, isonchip;
	int        ihdr;
	unsigned   globrow, globcol;
	unsigned   coreid;
	uintptr_t  dst;
	/* TODO: Make src const (need fix in esim.h first) */
	uint8_t   *src = (uint8_t *) file;

	islocal  = false;
	isonchip = false;

	ehdr = (Elf32_Ehdr *) &src[0];
	phdr = (Elf32_Phdr *) &src[ehdr->e_phoff];

	/* Range-check sections */
	for (ihdr = 0; ihdr < ehdr->e_phnum; ihdr++) {
		if (!is_valid_range(phdr[ihdr].p_vaddr, phdr[ihdr].p_memsz))
			return E_ERR;
	}

	for (ihdr = 0; ihdr < ehdr->e_phnum; ihdr++) {
		islocal = is_local(phdr[ihdr].p_vaddr);
		isonchip = islocal ? true
						   /* TODO: Don't cast to void */
						   : e_is_addr_on_chip((void *) ((uintptr_t) phdr[ihdr].p_vaddr));

		diag(L_D3) {
			fprintf(diag_fd, "ee_process_elf(): copying the data (%d bytes)",
					phdr[ihdr].p_filesz); }

		/* Address calculation */
		if (esim_target_p()) {
			dst = phdr[ihdr].p_vaddr;
			dst = islocal ? dst | dev->core[row][col].id << 20 : dst;
		} else {
			if (islocal) {
				diag(L_D3) { fprintf(diag_fd, " to core (%d,%d)\n", row, col); }

				// TODO: should this be p_paddr instead of p_vaddr?
				dst = ((uintptr_t) dev->core[row][col].mems.base)
					+ phdr[ihdr].p_vaddr;
			} else if (isonchip) {
				coreid = phdr[ihdr].p_vaddr >> 20;
				ee_get_coords_from_id(dev, coreid, &globrow, &globcol);
				diag(L_D3) {
					fprintf(diag_fd, " to core (%d,%d)\n", globrow, globcol); }
				// TODO: should this be p_paddr instead of p_vaddr?
				dst = ((uintptr_t) dev->core[globrow][globcol].mems.base)
					+ (phdr[ihdr].p_vaddr & 0x000fffff);
			} else {
				// If it is not on an eCore, it's in external memory.
				diag(L_D3) { fprintf(diag_fd, " to external memory.\n"); }
				dst = phdr[ihdr].p_vaddr - emem->ephy_base
					+ (uintptr_t) emem->base;
				diag(L_D3) {
					fprintf(diag_fd,
							"ee_process_elf(): converting virtual (0x%08llx) to physical (0x%08llx)...\n",
							(ulong64) phdr[ihdr].p_vaddr,
							(ulong64) dst); }
			}
		}

		/* Write */
		if (esim_target_p()) {
			if (ES_OK != es_ops.mem_store(dev->esim, dst,
										  phdr[ihdr].p_filesz,
										  &src[phdr[ihdr].p_offset])) {
				fprintf(diag_fd,
						"ee_process_elf(): Error: ESIM error writing to 0x%llx",
						(ulong64) dst);
				return E_ERR;
			}
		} else {
			memcpy((void *) dst, &src[phdr[ihdr].p_offset],
				   phdr[ihdr].p_filesz);
		}
		/* We might want to clear mem in range [p_filesz-p_memsz] here.
		 * .bss sections have this. For now assume all memory is cleared
		 * elsewhere. */
	}

	return E_OK;
}
