/*
  File: epiphany-hal.c

  This file is part of the Epiphany Software Development Kit.

  Copyright (C) 2013 Adapteva, Inc.
  Contributed by Yaniv Sapir <support@adapteva.com>

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

#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include "epiphany-hal-defs.h"
#include "e-hal.h"

bool e_is_on_chip(Epiphany_t *dev, unsigned coreid);
int  parse_hdf(E_Platform_t *dev, char *hdf);
int  parse_simple_hdf(E_Platform_t *dev, char *hdf);

#define diag(vN)   if (e_host_verbose >= vN)

static int e_host_verbose = 0;
static FILE *fd;

char const hdf_env_var_name[] = "EPIPHANY_HW_DEF_FILE";

static E_Platform_t e_platform;

/////////////////////////////////
// Device communication functions
//
// Epiphany access
int e_init(e_hdf_format_t hdf_type, char *hdf)
{
	uid_t UID;

	UID = getuid();
	if (UID != 0)
	{
		warnx("e_init(): Program must be invoked with superuser privilege (sudo).");
		return EPI_ERR;
	}

	char *hdf_env = getenv(hdf_env_var_name);
	if (hdf == NULL)
	{
		if (hdf_env == NULL)
		{
			warnx("e_init(): No Hardware Definition File (HDF) is specified.");
			return EPI_ERR;
		}
		hdf = hdf_env;
	}

	diag(H_D2) { fprintf(fd, "e_init(): opening HDF %s\n", hdf); }
	if (parse_hdf(&e_platform, hdf))
	{
		warnx("e_init(): Error parsing Hardware Definition File (HDF).");
		return EPI_ERR;
	}

	e_platform.initialized = true;
	return 0;
}


int e_finish()
{
	free(e_platform.chip);
	free(e_platform.emem);

	return EPI_OK;
}


int e_open(Epiphany_t *dev)
{
	uid_t UID;
	int i;

	UID = getuid();
	if (UID != 0)
	{
		warnx("e_open(): Program must be invoked with superuser privilege (sudo).");
		return EPI_ERR;
	}


	// Set device geometry
	e_get_coords_from_id(dev->base_coreid, &(dev->row), &(dev->col));
//	dev->rows      = EPI_ROWS;
//	dev->cols      = EPI_COLS;
	dev->num_cores = (dev->rows * dev->cols);

	dev->core = (Epiphany_core_t *) malloc(dev->num_cores * sizeof(Epiphany_core_t));
	if (!dev->core)
	{
		warnx("e_open(): Error allocating eCore descriptors.");
		return EPI_ERR;
	}


	// Open memory device
	dev->memfd = open("/dev/mem", O_RDWR | O_SYNC);
	if (dev->memfd == 0)
	{
		warnx("e_open(): /dev/mem file open failure.");
		return EPI_ERR;
	}

	// Map individual cores to virtual memory space
	for (i=0; i<dev->num_cores; i++)
	{
		diag(H_D2) { fprintf(fd, "e_open(): opening core #%d\n", i); }

		e_get_coords_from_id(dev->base_coreid, &(dev->row), &(dev->col));
		dev->core[i].id = e_get_id_from_num(dev, i);
		e_get_coords_from_id(dev->core[i].id, &(dev->core[i].row), &(dev->core[i].col));

		// SRAM array
		dev->core[i].mems.phy_base = (dev->core[i].id << 20 | LOC_BASE_MEMS);
		dev->core[i].mems.map_size = MAP_SIZE_MEMS;
		dev->core[i].mems.map_mask = MAP_MASK_MEMS;

		dev->core[i].mems.mapped_base = mmap(0, dev->core[i].mems.map_size, PROT_READ|PROT_WRITE, MAP_SHARED,
		                                  dev->memfd, dev->core[i].mems.phy_base & ~dev->core[i].mems.map_mask);
		dev->core[i].mems.base = dev->core[i].mems.mapped_base + (dev->core[i].mems.phy_base & dev->core[i].mems.map_mask);


		// e-core regs
		dev->core[i].regs.phy_base = (dev->core[i].id << 20 | LOC_BASE_REGS);
		dev->core[i].regs.map_size = MAP_SIZE_REGS;
		dev->core[i].regs.map_mask = MAP_MASK_REGS;

		dev->core[i].regs.mapped_base = mmap(0, dev->core[i].regs.map_size, PROT_READ|PROT_WRITE, MAP_SHARED,
		                                  dev->memfd, dev->core[i].regs.phy_base & ~dev->core[i].regs.map_mask);
		dev->core[i].regs.base = dev->core[i].regs.mapped_base + (dev->core[i].regs.phy_base & dev->core[i].regs.map_mask);

		if ((dev->core[i].mems.mapped_base == MAP_FAILED))
		{
			warnx("e_open(): ECORE[%d] MEM mmap failure.", i);
			return EPI_ERR;
		}

		if ((dev->core[i].regs.mapped_base == MAP_FAILED))
		{
			warnx("e_open(): ECORE[%d] REGS mmap failure.", i);
			return EPI_ERR;
		}
	}

	// e-sys regs
//	dev->esys.phy_base = ESYS_BASE_REGS;
	dev->esys.map_size = 0x1000;
	dev->esys.map_mask = (dev->esys.map_size - 1);

	dev->esys.mapped_base = mmap(0, dev->esys.map_size, PROT_READ|PROT_WRITE, MAP_SHARED,
	                          dev->memfd, dev->esys.phy_base & ~dev->esys.map_mask);
	dev->esys.base = dev->esys.mapped_base + (dev->esys.phy_base & dev->esys.map_mask);

	if ((dev->esys.mapped_base == MAP_FAILED))
	{
		warnx("e_open(): ESYS mmap failure.");
		return EPI_ERR;
	}

	return EPI_OK;
}


int e_close(Epiphany_t *dev)
{
	int i;

	for (i=0; i<dev->num_cores; i++)
	{
		munmap(dev->core[i].mems.mapped_base, dev->core[i].mems.map_size);
		munmap(dev->core[i].regs.mapped_base, dev->core[i].regs.map_size);
	}

	free(dev->core);

	munmap(dev->esys.mapped_base, dev->esys.map_size);

	close(dev->memfd);

	return EPI_OK;
}


ssize_t e_read(Epiphany_t *dev, unsigned corenum, const off_t from_addr, void *buf, size_t count)
{
	ssize_t rcount;

	if (from_addr < dev->core[corenum].mems.map_size)
		rcount = e_read_buf(dev, corenum, from_addr, buf, count);
	else {
		*((unsigned *) (buf)) = e_read_reg(dev, corenum, from_addr);
		rcount = 4;
	}

	return rcount;
}


ssize_t e_write(Epiphany_t *dev, unsigned corenum, off_t to_addr, const void *buf, size_t count)
{
	ssize_t wcount;
	unsigned reg;

	if (to_addr < dev->core[corenum].mems.map_size)
		wcount = e_write_buf(dev, corenum, to_addr, buf, count);
	else {
		reg = *((unsigned *) (buf));
		e_write_reg(dev, corenum, to_addr, reg);
		wcount = 4;
	}

	return wcount;
}


int e_read_word(Epiphany_t *dev, unsigned corenum, const off_t from_addr)
{
	volatile int *pfrom;
	int           data;

	pfrom = (int *) (dev->core[corenum].mems.base + (from_addr & dev->core[corenum].mems.map_mask));
	data  = *pfrom;

	return data;
}


ssize_t e_write_word(Epiphany_t *dev, unsigned corenum, off_t to_addr, int data)
{
	int *pto;

	pto = (int *) (dev->core[corenum].mems.base + (to_addr & dev->core[corenum].mems.map_mask));
	*pto = data;

	return sizeof(int);
}


ssize_t e_read_buf(Epiphany_t *dev, unsigned corenum, const off_t from_addr, void *buf, size_t count)
{
	const void *pfrom;

	pfrom = (dev->core[corenum].mems.base + (from_addr & dev->core[corenum].mems.map_mask));
#ifdef __E64G4_BURST_PATCH__
	int i;

	//	if ((corenum >= 0) && (corenum <= 63))
	if ((corenum >= 8) && (corenum <= 23))
		for (i=0; i<count; i+=sizeof(char))
			*(((char *) buf) + i) = *(((char *) pfrom) + i);
	else
		memcpy(buf, pfrom, count);
#else // __E64G4_BURST_PATCH__
	memcpy(buf, pfrom, count);
#endif // __E64G4_BURST_PATCH__

	return count;
}


ssize_t e_write_buf(Epiphany_t *dev, unsigned corenum, off_t to_addr, const void *buf, size_t count)
{
	void *pto;

	pto = (dev->core[corenum].mems.base + (to_addr & dev->core[corenum].mems.map_mask));
	memcpy(pto, buf, count);

	return count;
}


int e_read_reg(Epiphany_t *dev, unsigned corenum, const off_t from_addr)
{
	volatile int *pfrom;
	int           data;

	pfrom = (int *) (dev->core[corenum].regs.base + (from_addr & dev->core[corenum].regs.map_mask));
	data  = *pfrom;

	return data;
}


ssize_t e_write_reg(Epiphany_t *dev, unsigned corenum, off_t to_addr, int data)
{
	int *pto;

	pto = (int *) (dev->core[corenum].regs.base + (to_addr & dev->core[corenum].regs.map_mask));
	*pto = data;

	return sizeof(int);
}


int e_read_esys(Epiphany_t *dev, const off_t from_addr)
{
	volatile int *pfrom;
	int           data;

	pfrom = (int *) (dev->esys.base + (from_addr & dev->esys.map_mask));
	data  = *pfrom;

	return data;
}


ssize_t e_write_esys(Epiphany_t *dev, off_t to_addr, int data)
{
	int *pto;

	pto = (int *) (dev->esys.base + (to_addr & dev->esys.map_mask));
	*pto = data;

	return sizeof(int);
}




// TODO TODO TODO: have to integrate this in the platform structure!!!
#define DRAM_BASE_ADDRESS    0x1e000000
#define DRAM_SIZE            0x02000000
#define EPI_EXT_MEM_BASE     0x8e000000
#define EPI_EXT_MEM_SIZE     0x02000000


// eDRAM access
int e_alloc(DRAM_t *dram, off_t mbase, size_t msize)
{
	uid_t UID;

	UID = getuid();
	if (UID != 0)
	{
		warnx("e_alloc(): Program must be invoked with superuser privilege (sudo).");
		return EPI_ERR;
	}

	dram->memfd = open("/dev/mem", O_RDWR | O_SYNC);
	if (dram->memfd == 0)
	{
		warnx("e_alloc(): /dev/mem file open failure.");
		return EPI_ERR;
	}

	dram->map_size = msize;
	dram->map_mask = dram->map_size - 1;

	dram->phy_base = (e_platform.emem_phy_base + mbase);
	dram->mapped_base = mmap(0, dram->map_size, PROT_READ|PROT_WRITE, MAP_SHARED,
	                                  dram->memfd, dram->phy_base & ~dram->map_mask);
	dram->base = dram->mapped_base + (dram->phy_base & dram->map_mask);

	dram->ephy_base = (e_platform.emem_phy_base + mbase);
	dram->emap_size = msize;

	if (dram->mapped_base == MAP_FAILED)
	{
		warnx("e_alloc(): mmap failure.");
		return EPI_ERR;
	}


	return EPI_OK;
}


int e_free(DRAM_t *dram)
{
	munmap(dram->mapped_base, dram->map_size);
	close(dram->memfd);

	return 0;
}


ssize_t e_mread(DRAM_t *dram, const off_t from_addr, void *buf, size_t count)
{
	ssize_t rcount;

	rcount = e_mread_buf(dram, from_addr, buf, count);

	return rcount;
}


ssize_t e_mwrite(DRAM_t *dram, off_t to_addr, const void *buf, size_t count)
{
	ssize_t wcount;

	wcount = e_mwrite_buf(dram, to_addr, buf, count);

	return wcount;
}


int e_mread_word(DRAM_t *dram, const off_t from_addr)
{
	volatile int *pfrom;
	int           data;

	pfrom = (int *) (dram->base + (from_addr & dram->map_mask));
	data  = *pfrom;

	return data;
}


ssize_t e_mwrite_word(DRAM_t *dram, off_t to_addr, int data)
{
	int *pto;

	pto = (int *) (dram->base + (to_addr & dram->map_mask));
	*pto = data;

	return sizeof(int);
}


ssize_t e_mread_buf(DRAM_t *dram, const off_t from_addr, void *buf, size_t count)
{
	const void *pfrom;

	pfrom = (dram->base + (from_addr & dram->map_mask));
	memcpy(buf, pfrom, count);

	return count;
}


ssize_t e_mwrite_buf(DRAM_t *dram, off_t to_addr, const void *buf, size_t count)
{
	void *pto;

	pto = (dram->base + (to_addr & dram->map_mask));
	memcpy(pto, buf, count);

	return count;
}





/////////////////////////
// Core control functions
int e_reset_core(Epiphany_t *pEpiphany, unsigned corenum)
{
	int RESET0 = 0x0;
	int RESET1 = 0x1;

	diag(H_D1) { fprintf(fd, "   Resetting core %d (0x%03x)...", corenum, pEpiphany->core[corenum].id); }
	e_write_reg(pEpiphany, corenum, EPI_CORE_RESET, RESET1);
	e_write_reg(pEpiphany, corenum, EPI_CORE_RESET, RESET0);
	diag(H_D1) { fprintf(fd, " Done.\n"); }

	return EPI_OK;
}


int e_reset_esys(Epiphany_t *pEpiphany)
{
	diag(H_D1) { fprintf(fd, "   Resetting ESYS..."); fflush(stdout); }
	e_write_esys(pEpiphany, ESYS_RESET, 0);
	sleep(1);
	diag(H_D1) { fprintf(fd, " Done.\n"); }

	return EPI_OK;
}


int e_reset(Epiphany_t *pEpiphany, e_resetid_t resetid)
{
	int corenum;

	if (resetid == E_RESET_CORES) {
		diag(H_D1) { fprintf(fd, "   Resetting all cores..."); fflush(stdout); }
		for (corenum=0; corenum<pEpiphany->num_cores; corenum++) {
			e_reset_core(pEpiphany, corenum);
		}
	} else if (resetid == E_RESET_CHIP) {
		diag(H_D1) { fprintf(fd, "   Resetting chip..."); fflush(stdout); }
		errx(3, "\nEXITTING\n");
	} else if (resetid == E_RESET_ESYS) {
		diag(H_D1) { fprintf(fd, "   Resetting full ESYS..."); fflush(stdout); }
		e_reset_esys(pEpiphany);
	} else {
		diag(H_D1) { fprintf(fd, "   Invalid RESET ID!\n"); fflush(stdout); }
		return EPI_ERR;
	}
	diag(H_D1) { fprintf(fd, " Done.\n"); fflush(stdout); }

	return EPI_OK;
}


int e_start(Epiphany_t *pEpiphany, unsigned coreid)
{
	int corenum;
	int SYNC = 0x1;
	int *pILAT;

	corenum = e_get_num_from_id(pEpiphany, coreid);
	pILAT = (int *) ((char *) pEpiphany->core[corenum].regs.base + EPI_ILAT);
	diag(H_D1) { fprintf(fd, "   SYNC (0x%x) to core %d...", (unsigned) pILAT, corenum); fflush(stdout); }
	*pILAT = (*pILAT) | SYNC;
	diag(H_D1) { fprintf(fd, " Done.\n"); }

	return EPI_OK;
}





////////////////////
// Utility functions
unsigned e_get_num_from_coords(Epiphany_t *dev, unsigned row, unsigned col)
{
	unsigned corenum;

	corenum = (col & (dev->cols-1)) + ((row & (dev->rows-1)) * dev->cols);

	return corenum;
}


unsigned e_get_num_from_id(Epiphany_t *dev, unsigned coreid)
{
	unsigned corenum;

	corenum = (coreid & (dev->cols-1)) + (((coreid >> 6) & (dev->rows-1)) * dev->cols);

	return corenum;
}


unsigned e_get_id_from_coords(Epiphany_t *dev, unsigned row, unsigned col)
{
	unsigned coreid;

	coreid = (dev->base_coreid + (col & (dev->cols-1))) + ((row & (dev->rows-1)) << 6);

	return coreid;
}


unsigned e_get_id_from_num(Epiphany_t *dev, unsigned corenum)
{
	int coreid;

	coreid = dev->base_coreid + (corenum & (dev->cols-1)) + (((corenum / dev->cols) & (dev->rows-1)) << 6);

	return coreid;
}


void e_get_coords_from_id(unsigned coreid, unsigned *row, unsigned *col)
{
	// TODO these are the absolute coords. Do we need relative ones?
	*row = (coreid >> 6) & 0x3f;
	*col = (coreid >> 0) & 0x3f;

	return;
}


void e_get_coords_from_num(Epiphany_t *dev, unsigned corenum, unsigned *row, unsigned *col)
{
	// TODO this gives the *relative* coords in a chip!
	*row = (corenum / dev->cols) & (dev->rows-1);
	*col = (corenum >> 0)        & (dev->cols-1);

	return;
}


bool e_is_on_chip(Epiphany_t *dev, unsigned coreid)
{
	unsigned erow, ecol;
	unsigned row, col;

	e_get_coords_from_id(dev->base_coreid, &erow, &ecol);
	e_get_coords_from_id(coreid, &row, &col);

	if ((row >= erow) && (row < (erow + dev->rows)) && (col >= ecol) && (col < (ecol + dev->cols)))
		return true;
	else
		return false;
}


void e_set_host_verbosity(e_hal_diag_t verbose)
{
	fd = stderr;
	e_host_verbose = verbose;

	return;
}




////////////////////////////////////
// ftdi_target wrapper functionality
#include <e-xml/src/epiphany_platform.h>

Epiphany_t Epiphany, *pEpiphany;
DRAM_t     ERAM,     *pERAM;

platform_definition_t* platform;


// Global memory access
ssize_t e_read_abs(unsigned address, void* buf, size_t burst_size)
{
	ssize_t  rcount;
	unsigned isglobal, isexternal, isonchip, isregs, ismems;
	unsigned corenum, coreid;
	unsigned row, col, i;

	diag(H_D1) { fprintf(fd, "e_read_abs(): address = 0x%08x\n", address); }
	isglobal = (address & 0xfff00000) != 0;
	if (isglobal)
	{
		if (((address >= pERAM->phy_base)  && (address < (pERAM->phy_base + pERAM->map_size))) ||
		    ((address >= pERAM->ephy_base) && (address < (pERAM->ephy_base + pERAM->emap_size))))
		{
			isexternal = true;
		} else {
			isexternal = false;
			coreid     = address >> 20;
			isonchip   = e_is_on_chip(pEpiphany, coreid);
			if (isonchip)
			{
				e_get_coords_from_id(coreid, &row, &col);
				corenum = e_get_num_from_coords(pEpiphany, row, col);
				ismems  = (address <  pEpiphany->core[corenum].mems.phy_base + pEpiphany->core[corenum].mems.map_size);
				isregs  = (address >= pEpiphany->core[corenum].regs.phy_base);
			}
		}
	}

	if (isglobal)
	{
		if (isexternal)
		{
			rcount = e_mread_buf(pERAM, address, buf, burst_size);
			diag(H_D1) { fprintf(fd, "e_read_abs(): isexternal -> rcount = %d\n", (int) rcount); }
		} else if (isonchip)
		{
			if (ismems) {
				rcount = e_read_buf(pEpiphany, corenum, address, buf, burst_size);
				diag(H_D1) { fprintf(fd, "e_read_abs(): isonchip/ismems -> rcount = %d\n", (int) rcount); }
			} else if (isregs) {
				for (rcount=0, i=0; i<burst_size; i+=sizeof(unsigned)) {
					*((unsigned *) (buf+i)) = e_read_reg(pEpiphany, corenum, (address+i));
					rcount += sizeof(unsigned);
				}
				diag(H_D1) { fprintf(fd, "e_read_abs(): isonchip/isregs -> rcount = %d\n", (int) rcount); }
			} else {
				rcount = 0;
				diag(H_D1) { fprintf(fd, "e_read_abs(): is a reserved on-chip address -> rcount = %d\n", (int) rcount); }
			}
		} else {
			rcount = 0;
			diag(H_D1) { fprintf(fd, "e_read_abs(): is not a legal address -> rcount = %d\n", (int) rcount); }
		}
	} else {
		rcount = 0;
		diag(H_D1) { fprintf(fd, "e_read_abs(): is not a global address -> rcount = %d\n", (int) rcount); }
	}

	return rcount;
}


ssize_t e_write_abs(unsigned address, void *buf, size_t burst_size)
{
	ssize_t  rcount;
	unsigned isglobal, isexternal, isonchip, isregs, ismems;
	unsigned corenum, coreid;
	unsigned row, col, i;

	diag(H_D1) { fprintf(fd, "e_write_abs(): address = 0x%08x\n", address); }
	isglobal = (address & 0xfff00000) != 0;
	if (isglobal)
	{
		if (((address >= pERAM->phy_base)  && (address < (pERAM->phy_base + pERAM->map_size))) ||
		    ((address >= pERAM->ephy_base) && (address < (pERAM->ephy_base + pERAM->emap_size))))
		{
			isexternal = true;
		} else {
			isexternal = false;
			coreid     = address >> 20;
			isonchip   = e_is_on_chip(pEpiphany, coreid);
			if (isonchip)
			{
				e_get_coords_from_id(coreid, &row, &col);
				corenum = e_get_num_from_coords(pEpiphany, row, col);
				ismems  = (address <  pEpiphany->core[corenum].mems.phy_base + pEpiphany->core[corenum].mems.map_size);
				isregs  = (address >= pEpiphany->core[corenum].regs.phy_base);
			}
		}
	}

	if (isglobal)
	{
		if (isexternal)
		{
			rcount = e_mwrite_buf(pERAM, address, buf, burst_size);
			diag(H_D1) { fprintf(fd, "e_write_abs(): isexternal -> rcount = %d\n", (int) rcount); }
		} else if (isonchip)
		{
			if (ismems) {
				rcount = e_write_buf(pEpiphany, corenum, address, buf, burst_size);
				diag(H_D1) { fprintf(fd, "e_write_abs(): isonchip/ismems -> rcount = %d\n", (int) rcount); }
			} else if (isregs) {
				for (rcount=0, i=0; i<burst_size; i+=sizeof(unsigned)) {
					rcount += e_write_reg(pEpiphany, corenum, address, *((unsigned *)(buf+i)));
				}
				diag(H_D1) { fprintf(fd, "e_write_abs(): isonchip/isregs -> rcount = %d\n", (int) rcount); }
			} else {
				rcount = 0;
				diag(H_D1) { fprintf(fd, "e_write_abs(): is a reserved on-chip address -> rcount = %d\n", (int) rcount); }
			}
		} else {
			rcount = 0;
			diag(H_D1) { fprintf(fd, "e_write_abs(): is not a legal address -> rcount = %d\n", (int) rcount); }
		}
	} else {
		rcount = 0;
		diag(H_D1) { fprintf(fd, "e_write_abs(): is not a global address -> rcount = %d\n", (int) rcount); }
	}

	return rcount;
}



int init_platform(platform_definition_t* platform_arg, unsigned verbose_mode)
{
	int res;

	pEpiphany = &Epiphany;
	pERAM     = &ERAM;
	platform  = platform_arg;

	e_set_host_verbosity(verbose_mode);
	res = e_alloc(pERAM, 0, DRAM_SIZE); // TODO: change HDF to something meaningful
	res = e_open(pEpiphany); // TODO: change HDF to something meaningful

	return res;
}



int close_platform()
{
	int res;

	res = e_close(pEpiphany);
	res = e_free(pERAM);

	return res;
}



int write_to(unsigned address, void *buf, size_t burst_size)
{
	// The readMem() function which calls read_from() driver function always calls with a global address.
	// need to check which region is being called and use the appropriate e-host API.

	ssize_t rcount;

	rcount = e_write_abs(address, buf, burst_size);

	return rcount;
}



int read_from(unsigned address, void* buf, size_t burst_size)
{
	// The readMem() function which calls read_from() driver function always calls with a global address.
	// need to check which region is being called and use the appropriate e-host API.

	ssize_t rcount;

	rcount = e_read_abs(address, buf, burst_size);

	return rcount;
}



int hw_reset()
{
	int sleepTime = 0;

	e_reset(pEpiphany, E_RESET_ESYS);

	sleep(sleepTime);

	return 0;
}


char TargetId[] = "E16G3 based Parallella";
int get_description(char** targetIdp)
{
	*targetIdp = platform->name;

	return 0;
}


// HDF parser

int parse_hdf(E_Platform_t *dev, char *hdf)
{
//	if (file_ext == "hdf")
		parse_simple_hdf(dev, hdf);
//	else if (file_ext == "xml")
//		parse_xml_hdf(dev, hdf);

	return EPI_OK;
}

int parse_simple_hdf(E_Platform_t *dev, char *hdf)
{
	FILE *fp;
	char line[255], etag[255], eval[255];
	int i, l;

	fp = fopen(hdf, "r");
	if (fp == NULL)
	{
		warnx("e_open(): Can't open Hardware Definition File (HDF).");
		return EPI_ERR;
	}

	l = 0;

	while (!feof(fp))
	{
		l++;
		fgets(line, strlen(line), fp);
		sscanf(line, "%s %s", etag, eval);
		fprintf(fd, "%2d: %s %s\n", l, etag, eval);
		if      (!strcmp(hdf_defs[0].name, etag))  // PLATFORM_VERSION
		{
			sscanf(etag, "%x", &(dev->version));
		}
		else if (!strcmp(hdf_defs[1].name, etag))  // NUM_CHIPS
		{
			sscanf(etag, "%x", &(dev->num_chips));
			dev->chip = (Epiphany_t *) calloc(dev->num_chips, sizeof(Epiphany_t));
			dev->chip_num = 0;
		}
		else if (!strcmp(hdf_defs[2].name, etag))  // NUM_EXT_MEMS
		{
			sscanf(etag, "%x", &(dev->num_emems));
			dev->emem = (DRAM_t *) calloc(dev->num_emems, sizeof(DRAM_t));
			dev->emem_num = 0;
		}
		else if (!strcmp(hdf_defs[3].name, etag))  // EPI_BASE_CORE_ID
		{
			sscanf(etag, "%x", &(dev->chip[dev->chip_num].base_coreid));
		}
		else if (!strcmp(hdf_defs[4].name, etag))  // DRAM_BASE_ADDRESS
		{
			sscanf(etag, "%x", &(dev->emem_phy_base));
		}
		else if (!strcmp(hdf_defs[5].name, etag))  // DRAM_SIZE
		{
			sscanf(etag, "%x", &(dev->emem_size));
		}
		else if (!strcmp(hdf_defs[6].name, etag))  // EPI_EXT_MEM_BASE
		{
			sscanf(etag, "%x", &(dev->emem_ephy_base));
		}
		else if (!strcmp(hdf_defs[7].name, etag))  // EPI_EXT_MEM_SIZE
		{
			sscanf(etag, "%x", &(dev->emem_size));
		}
		else if (!strcmp(hdf_defs[8].name, etag))  // EPI_ROWS
		{
			sscanf(etag, "%x", &(dev->chip[dev->chip_num].rows));
		}
		else if (!strcmp(hdf_defs[9].name, etag))  // EPI_COLS
		{
			sscanf(etag, "%x", &(dev->chip[dev->chip_num].cols));
		}
		else if (!strcmp(hdf_defs[10].name, etag)) // ESYS_BASE_REGS
		{
			sscanf(etag, "%x", &(dev->chip[dev->chip_num].esys.phy_base));
		}
		else if (!strcmp(hdf_defs[11].name, etag)) // comment
		{
			;
		}
		else {
			return EPI_ERR;
		}


//		int i;
//		for (i=0; i<_NumVars; i++)
//		{
//			if (!strcmp(hdf_defs[i].name, etag)) {}
//		}
	}

	fclose(fp);

	return EPI_OK;
}
