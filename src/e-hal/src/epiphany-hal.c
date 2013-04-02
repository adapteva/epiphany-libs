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
#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include "e-hal.h"
#include "epiphany-hal-api-local.h"

typedef unsigned int uint;


int  ee_parse_hdf(e_platform_t *dev, char *hdf);
int  ee_parse_simple_hdf(e_platform_t *dev, char *hdf);
int  ee_parse_xml_hdf(e_platform_t *dev, char *hdf);
void ee_trim(char *a);

#define diag(vN)   if (e_host_verbose >= vN)

//static int e_host_verbose = 0;
//static FILE *fd;
int   e_host_verbose; //__attribute__ ((visibility ("hidden"))) = 0;
FILE *fd             __attribute__ ((visibility ("hidden")));

char *OBJTYPE[64] = {"NULL", "EPI_PLATFORM", "EPI_CHIP", "EPI_GROUP", "EPI_CORE", "EXT_MEM"};

char const hdf_env_var_name[] = "EPIPHANY_HDF";

//static e_platform_t e_platform = { E_EPI_PLATFORM };
e_platform_t e_platform = { E_EPI_PLATFORM };

/////////////////////////////////
// Device communication functions
//
// Platform configuration
//
// Initialize Epiphany platform according to configuration found in the HDF
int e_init(char *hdf)
{
	uid_t UID;
	char *hdf_env;
	int i;

	e_platform.objtype     = E_EPI_PLATFORM;
	e_platform.initialized = e_false;
	e_platform.num_chips   = 0;
	e_platform.num_emems   = 0;

	UID = getuid();
	if (UID != 0)
	{
		warnx("e_init(): Program must be invoked with superuser privilege (sudo).");
		return E_ERR;
	}

	// Parse HDF, get platform configuration
	if (hdf == NULL)
	{
		hdf_env = getenv(hdf_env_var_name);
		diag(H_D2) { fprintf(fd, "e_init(): HDF ENV = %s\n", hdf_env); }
		if (hdf_env == NULL)
		{
			warnx("e_init(): No Hardware Definition File (HDF) is specified.");
			return E_ERR;
		}
		hdf = hdf_env;
	}

	diag(H_D2) { fprintf(fd, "e_init(): opening HDF %s\n", hdf); }
	if (ee_parse_hdf(&e_platform, hdf))
	{
		warnx("e_init(): Error parsing Hardware Definition File (HDF).");
		return E_ERR;
	}

	// Populate the missing chip parameters according to chip version.
	for (i=0; i<e_platform.num_chips; i++)
	{
		ee_set_chip_params(&(e_platform.chip[i]));
	}

	// Find the bounding box of Epiphany chips. This defines the reference frame for core-groups.
	e_platform.row  = 0x3f;
	e_platform.col  = 0x3f;
	e_platform.rows = 0;
	e_platform.cols = 0;
	for (i=0; i<e_platform.num_chips; i++)
	{
		if (e_platform.row > e_platform.chip[i].row)
			e_platform.row = e_platform.chip[i].row;

		if (e_platform.col > e_platform.chip[i].col)
			e_platform.col = e_platform.chip[i].col;

		if (e_platform.rows < (e_platform.chip[i].row + e_platform.chip[i].rows - 1))
			e_platform.rows =  e_platform.chip[i].row + e_platform.chip[i].rows - 1;

		if (e_platform.cols < (e_platform.chip[i].col + e_platform.chip[i].cols - 1))
			e_platform.cols =  e_platform.chip[i].col + e_platform.chip[i].cols - 1;

	}
	e_platform.rows = e_platform.rows - e_platform.row + 1;
	e_platform.cols = e_platform.cols - e_platform.col + 1;
	diag(H_D2) { fprintf(fd, "e_init(): platform.(row,col)   = (%d,%d)\n", e_platform.row, e_platform.col); }
	diag(H_D2) { fprintf(fd, "e_init(): platform.(rows,cols) = (%d,%d)\n", e_platform.rows, e_platform.cols); }

	e_platform.initialized = e_true;

	return E_OK;
}


// Finalize connection with the Epiphany platform; Free allocated resources.
int e_finalize()
{
	if (e_platform.initialized == e_false)
	{
		warnx("e_finalize(): Platform was not initiated.");
		return E_ERR;
	}

	e_platform.initialized = e_false;

	free(e_platform.chip);
	free(e_platform.emem);

	return E_OK;
}


int e_get_platform_info(e_platform_t *platform)
{
	if (e_platform.initialized == e_false)
	{
		warnx("e_get_platform_info(): Platform was not initialized. Use e_init().");
		return E_ERR;
	}

	*platform = e_platform;
	platform->chip = NULL;
	platform->emem = NULL;

	return E_OK;
}


// Epiphany access
//
// Define an e-core workgroup
int e_open(e_epiphany_t *dev, unsigned row, unsigned col, unsigned rows, unsigned cols)
{
	int irow, icol;
	e_core_t *curr_core;

	if (e_platform.initialized == e_false)
	{
		warnx("e_open(): Platform was not initialized. Use e_init().");
		return E_ERR;
	}

	dev->objtype = E_EPI_GROUP;

	// Set device geometry
	// TODO: check if coordinates and size are legal.
	dev->row         = row + e_platform.row;
	dev->col         = col + e_platform.col;
	dev->rows        = rows;
	dev->cols        = cols;
	dev->num_cores   = dev->rows * dev->cols;
	dev->base_coreid = ee_get_id_from_coords(dev, 0, 0);

	diag(H_D2) { fprintf(fd, "e_open(): group.(row,col),id = (%d,%d),0x%03x\n", dev->row, dev->col, dev->base_coreid); }
	diag(H_D2) { fprintf(fd, "e_open(): group.(rows,cols),numcores = (%d,%d), %d\n", dev->rows, dev->cols, dev->num_cores); }

	// Open memory device
	dev->memfd = open("/dev/mem", O_RDWR | O_SYNC);
	if (dev->memfd == 0)
	{
		warnx("e_open(): /dev/mem file open failure.");
		return E_ERR;
	}


	// Map individual cores to virtual memory space
	dev->core = (e_core_t **) malloc(dev->rows * sizeof(e_core_t *));
	if (!dev->core)
	{
		warnx("e_open(): Error while allocating eCore descriptors.");
		return E_ERR;
	}

	for (irow=0; irow<dev->rows; irow++)
	{
		dev->core[irow] = (e_core_t *) malloc(dev->cols * sizeof(e_core_t));
		if (!dev->core[irow])
		{
			warnx("e_open(): Error while allocating eCore descriptors.");
			return E_ERR;
		}

		for (icol=0; icol<dev->cols; icol++)
		{
			diag(H_D2) { fprintf(fd, "e_open(): opening core (%d,%d)\n", irow, icol); }

			curr_core = &(dev->core[irow][icol]);
			curr_core->row = irow;
			curr_core->col = icol;
			curr_core->id  = ee_get_id_from_coords(dev, curr_core->row, curr_core->col);

			diag(H_D2) { fprintf(fd, "e_open(): core (%d,%d), CoreID = 0x%03x\n", curr_core->row, curr_core->col, curr_core->id); }

			// SRAM array
			curr_core->mems.phy_base = (curr_core->id << 20 | e_platform.chip[0].sram_base); // TODO: assumes first chip
			curr_core->mems.map_size = e_platform.chip[0].sram_size; // TODO: this assumes a single chip!
			curr_core->mems.map_mask = curr_core->mems.map_size - 1; // TODO: this assumes a power-of-2 size

			curr_core->mems.mapped_base = mmap(0, curr_core->mems.map_size, PROT_READ|PROT_WRITE, MAP_SHARED,
			                                      dev->memfd, curr_core->mems.phy_base & ~curr_core->mems.map_mask);
			curr_core->mems.base = curr_core->mems.mapped_base + (curr_core->mems.phy_base & curr_core->mems.map_mask);

			diag(H_D2) { fprintf(fd, "e_open(): mems.phy_base = 0x%08x, mems.base = 0x%08x, mems.size = 0x%08x, mems.mask = 0x%08x\n", (uint) curr_core->mems.phy_base, (uint) curr_core->mems.base, (uint) curr_core->mems.map_size, (uint) curr_core->mems.map_mask); }

			// e-core regs
			curr_core->regs.phy_base = (curr_core->id << 20 | e_platform.chip[0].regs_base); // TODO: assumes first chip
			curr_core->regs.map_size = e_platform.chip[0].regs_size; // TODO: this assumes a single chip!
			curr_core->regs.map_mask = curr_core->regs.map_size - 1; // TODO: this assumes a power-of-2 size

			curr_core->regs.mapped_base = mmap(0, curr_core->regs.map_size, PROT_READ|PROT_WRITE, MAP_SHARED,
			                                      dev->memfd, curr_core->regs.phy_base & ~curr_core->regs.map_mask);
			curr_core->regs.base = curr_core->regs.mapped_base + (curr_core->regs.phy_base & curr_core->regs.map_mask);

			diag(H_D2) { fprintf(fd, "e_open(): regs.phy_base = 0x%08x, regs.base = 0x%08x, regs.size = 0x%08x, regs.mask = 0x%08x\n", (uint) curr_core->regs.phy_base, (uint) curr_core->regs.base, (uint) curr_core->regs.map_size, (uint) curr_core->regs.map_mask); }

			if ((curr_core->mems.mapped_base == MAP_FAILED) || (curr_core->regs.mapped_base == MAP_FAILED))
			{
				warnx("e_open(): ECORE[%d,%d] MEM or REG mmap failure.", curr_core->row, curr_core->col);
				return E_ERR;
			}
		}
	}

	return E_OK;
}


// Close an e-core workgroup
int e_close(e_epiphany_t *dev)
{
	int irow, icol;
	e_core_t *curr_core;

	if (!dev)
	{
		warnx("e_close(): Core group was not opened.");
		return E_ERR;
	}

	for (irow=0; irow<dev->rows; irow++)
	{
		for (icol=0; icol<dev->cols; icol++)
		{
			curr_core = &(dev->core[irow][icol]);

			munmap(curr_core->mems.mapped_base, curr_core->mems.map_size);
			munmap(curr_core->regs.mapped_base, curr_core->regs.map_size);
		}

		free(dev->core[irow]);
	}

	free(dev->core);

	close(dev->memfd);

	return E_OK;
}


// Read a memory block from a core in a group
ssize_t e_read(void *dev, unsigned row, unsigned col, off_t from_addr, void *buf, size_t size)
{
	ssize_t     rcount;
	e_epiphany_t *edev;
	e_mem_t     *mdev;

	switch (*((e_objytpe_t *) dev))
	{
	case E_EPI_GROUP:
		diag(H_D2) { fprintf(fd, "e_read(): detected EPI_GROUP object.\n"); }
		edev = (e_epiphany_t *) dev;
		if (from_addr < edev->core[row][col].mems.map_size)
			rcount = ee_read_buf(edev, row, col, from_addr, buf, size);
		else {
			*((unsigned *) (buf)) = ee_read_reg(dev, row, col, from_addr);
			rcount = 4;
		}
		break;

	case E_EXT_MEM:
		diag(H_D2) { fprintf(fd, "e_read(): detected EXT_MEM object.\n"); }
		mdev = (e_mem_t *) dev;
		rcount = ee_mread_buf(mdev, from_addr, buf, size);
		break;

	default:
		diag(H_D2) { fprintf(fd, "e_read(): invalid object type.\n"); }
		rcount = 0;
		return E_ERR;
	}

	return rcount;
}


// Write a memory block to a core in a group
ssize_t e_write(void *dev, unsigned row, unsigned col, off_t to_addr, const void *buf, size_t size)
{
	ssize_t       wcount;
	unsigned int  reg;
	e_epiphany_t *edev;
	e_mem_t      *mdev;

	switch (*((e_objytpe_t *) dev))
	{
	case E_EPI_GROUP:
		diag(H_D2) { fprintf(fd, "e_write(): detected EPI_GROUP object.\n"); }
		edev = (e_epiphany_t *) dev;
		if (to_addr < edev->core[row][col].mems.map_size)
			wcount = ee_write_buf(edev, row, col, to_addr, buf, size);
		else {
			reg = *((unsigned *) (buf));
			ee_write_reg(edev, row, col, to_addr, reg);
			wcount = 4;
		}
		break;

	case E_EXT_MEM:
		diag(H_D2) { fprintf(fd, "e_write(): detected EXT_MEM object.\n"); }
		mdev = (e_mem_t *) dev;
		wcount = ee_mwrite_buf(mdev, to_addr, buf, size);
		break;

	default:
		diag(H_D2) { fprintf(fd, "e_write(): invalid object type.\n"); }
		wcount = 0;
		return E_ERR;
	}

	return wcount;
}


// Read a word from SRAM of a core in a group
int ee_read_word(e_epiphany_t *dev, unsigned row, unsigned col, const off_t from_addr)
{
	volatile int *pfrom;
	int           data;

	pfrom = (int *) (dev->core[row][col].mems.base + (from_addr & dev->core[row][col].mems.map_mask));
	diag(H_D2) { fprintf(fd, "ee_read_word(): reading from from_addr=0x%08x, pfrom=0x%08x\n", (uint) from_addr, (uint) pfrom); }
	data  = *pfrom;

	return data;
}


// Write a word to SRAM of a core in a group
ssize_t ee_write_word(e_epiphany_t *dev, unsigned row, unsigned col, off_t to_addr, int data)
{
	int *pto;

	pto = (int *) (dev->core[row][col].mems.base + (to_addr & dev->core[row][col].mems.map_mask));
	diag(H_D2) { fprintf(fd, "ee_write_word(): writing to to_addr=0x%08x, pto=0x%08x\n", (uint) to_addr, (uint) pto); }
	*pto = data;

	return sizeof(int);
}


// Read a memory block from SRAM of a core in a group
ssize_t ee_read_buf(e_epiphany_t *dev, unsigned row, unsigned col, const off_t from_addr, void *buf, size_t size)
{
	const void *pfrom;
	unsigned int addr_from, addr_to, align;
	int i;

	pfrom = (dev->core[row][col].mems.base + (from_addr & dev->core[row][col].mems.map_mask));
	diag(H_D2) { fprintf(fd, "ee_read_buf(): reading from from_addr=0x%08x, pfrom=0x%08x\n", (uint) from_addr, (uint) pfrom); }

	if ((dev->type ==  E_E64G401) && ((row >= 1) && (row <= 2)))
	{
		// The following code is a fix for the E64G401 anomaly of bursting reads from
		// internal memory to host in rows #1 and #2.
		addr_from = (unsigned int) pfrom;
		addr_to   = (unsigned int) buf;
		align     = (addr_from | addr_to | size) & 0x7;

		switch (align) {
		case 0x0:
			for (i=0; i<size; i+=sizeof(int64_t))
				*((int64_t *) (buf + i)) = *((int64_t *) (pfrom + i));
			break;
		case 0x1:
		case 0x3:
		case 0x5:
		case 0x7:
			for (i=0; i<size; i+=sizeof(int8_t))
				*((int8_t  *) (buf + i)) = *((int8_t  *) (pfrom + i));
			break;
		case 0x2:
		case 0x6:
			for (i=0; i<size; i+=sizeof(int16_t))
				*((int16_t *) (buf + i)) = *((int16_t *) (pfrom + i));
			break;
		case 0x4:
			for (i=0; i<size; i+=sizeof(int32_t))
				*((int32_t *) (buf + i)) = *((int32_t *) (pfrom + i));
			break;
		}
	}
	else
		memcpy(buf, pfrom, size);

	return size;
}


// Write a memory block to SRAM of a core in a group
ssize_t ee_write_buf(e_epiphany_t *dev, unsigned row, unsigned col, off_t to_addr, const void *buf, size_t size)
{
	void *pto;

	pto = (dev->core[row][col].mems.base + (to_addr & dev->core[row][col].mems.map_mask));
	diag(H_D2) { fprintf(fd, "ee_write_buf(): writing to to_addr=0x%08x, pto=0x%08x\n", (uint) to_addr, (uint) pto); }
	memcpy(pto, buf, size);

	return size;
}


// Read a core register from a core in a group
int ee_read_reg(e_epiphany_t *dev, unsigned row, unsigned col, const off_t from_addr)
{
	volatile int *pfrom;
	int           data;

	pfrom = (int *) (dev->core[row][col].regs.base + (from_addr & dev->core[row][col].regs.map_mask));
	diag(H_D2) { fprintf(fd, "ee_read_reg(): reading from from_addr=0x%08x, pfrom=0x%08x\n", (uint) from_addr, (uint) pfrom); }
	data  = *pfrom;

	return data;
}


// Write to a core register of a core in a group
ssize_t ee_write_reg(e_epiphany_t *dev, unsigned row, unsigned col, off_t to_addr, int data)
{
	int *pto;

	pto = (int *) (dev->core[row][col].regs.base + (to_addr & dev->core[row][col].regs.map_mask));
	diag(H_D2) { fprintf(fd, "ee_write_reg(): writing to to_addr=0x%08x, pto=0x%08x\n", (uint) to_addr, (uint) pto); }
	*pto = data;

	return sizeof(int);
}


// External Memory access
//
// Allocate a buffer in external memory
int e_alloc(e_mem_t *mbuf, off_t base, size_t size)
{
	if (e_platform.initialized == e_false)
	{
		warnx("e_open(): Platform was not initialized. Use e_init().");
		return E_ERR;
	}

	mbuf->objtype = E_EXT_MEM;

	mbuf->memfd = open("/dev/mem", O_RDWR | O_SYNC);
	if (mbuf->memfd == 0)
	{
		warnx("e_alloc(): /dev/mem file open failure.");
		return E_ERR;
	}

	diag(H_D2) { fprintf(fd, "e_alloc(): allocating EMEM buffer at offset 0x%08x\n", (uint) base); }

	mbuf->map_size = size;
	mbuf->map_mask = mbuf->map_size - 1; // TODO: this assumes a power-of-2 size

	mbuf->phy_base = (e_platform.emem[0].phy_base + base); // TODO: this takes only the 1st segment into account
	mbuf->mapped_base = mmap(0, mbuf->map_size, PROT_READ|PROT_WRITE, MAP_SHARED,
	                            mbuf->memfd, mbuf->phy_base & ~mbuf->map_mask);
	mbuf->base = mbuf->mapped_base + (mbuf->phy_base & mbuf->map_mask);

	mbuf->ephy_base = (e_platform.emem[0].ephy_base + base); // TODO: this takes only the 1st segment into account
	mbuf->emap_size = size;

	diag(H_D2) { fprintf(fd, "e_alloc(): mbuf.phy_base = 0x%08x, mbuf.base = 0x%08x, mbuf.size = 0x%08x, mbuf.mask = 0x%08x\n", (uint) mbuf->phy_base, (uint) mbuf->base, (uint) mbuf->map_size, (uint) mbuf->map_mask); }

	if (mbuf->mapped_base == MAP_FAILED)
	{
		warnx("e_alloc(): mmap failure.");
		return E_ERR;
	}


	return E_OK;
}


// Free a memory buffer in external memory
int e_free(e_mem_t *mbuf)
{
	munmap(mbuf->mapped_base, mbuf->map_size);
	close(mbuf->memfd);

	return E_OK;
}


// Read a block from an external memory buffer
ssize_t ee_mread(e_mem_t *mbuf, const off_t from_addr, void *buf, size_t size)
{
	ssize_t rcount;

	rcount = ee_mread_buf(mbuf, from_addr, buf, size);

	return rcount;
}


// Write a block to an external memory buffer
ssize_t ee_mwrite(e_mem_t *mbuf, off_t to_addr, const void *buf, size_t size)
{
	ssize_t wcount;

	wcount = ee_mwrite_buf(mbuf, to_addr, buf, size);

	return wcount;
}


// Read a word from an external memory buffer
int ee_mread_word(e_mem_t *mbuf, const off_t from_addr)
{
	volatile int *pfrom;
	int           data;

	pfrom = (int *) (mbuf->base + (from_addr & mbuf->map_mask));
	diag(H_D2) { fprintf(fd, "ee_mread_word(): reading from from_addr=0x%08x, pfrom=0x%08x\n", (uint) from_addr, (uint) pfrom); }
	data  = *pfrom;

	return data;
}


// Write a word to an external memory buffer
ssize_t ee_mwrite_word(e_mem_t *mbuf, off_t to_addr, int data)
{
	int *pto;

	pto = (int *) (mbuf->base + (to_addr & mbuf->map_mask));
	diag(H_D2) { fprintf(fd, "ee_mwrite_word(): writing to to_addr=0x%08x, pto=0x%08x\n", (uint) to_addr, (uint) pto); }
	*pto = data;

	return sizeof(int);
}


// Read a block from an external memory buffer
ssize_t ee_mread_buf(e_mem_t *mbuf, const off_t from_addr, void *buf, size_t size)
{
	const void *pfrom;

	pfrom = (mbuf->base + (from_addr & mbuf->map_mask));
	diag(H_D2) { fprintf(fd, "ee_mread_buf(): reading from from_addr=0x%08x, pfrom=0x%08x\n", (uint) from_addr, (uint) pfrom); }
	memcpy(buf, pfrom, size);

	return size;
}


// Write a block to an external memory buffer
ssize_t ee_mwrite_buf(e_mem_t *mbuf, off_t to_addr, const void *buf, size_t size)
{
	void *pto;

	pto = (mbuf->base + (to_addr & mbuf->map_mask));
	diag(H_D2) { fprintf(fd, "ee_mwrite_buf(): writing to to_addr=0x%08x, pto=0x%08x\n", (uint) to_addr, (uint) pto); }
	memcpy(pto, buf, size);

	return size;
}


//////////////////
// Platform access
//
// Read a word from an address in the platform space
int ee_read_esys(off_t from_addr)
{
	e_mmap_t      esys;
	int           memfd;
	volatile int *pfrom;
	int           data;

	// Open memory device
	memfd = open("/dev/mem", O_RDWR | O_SYNC);
	if (memfd == 0)
	{
		warnx("ee_read_esys(): /dev/mem file open failure.");
		return E_ERR;
	}

	esys.map_size = 0x1000; // map 4KB page
	esys.map_mask = (esys.map_size - 1);
	esys.phy_base = from_addr & (~esys.map_mask);

	esys.mapped_base = mmap(0, esys.map_size, PROT_READ|PROT_WRITE, MAP_SHARED, memfd, esys.phy_base);
	esys.base = esys.mapped_base + (esys.phy_base & esys.map_mask);

	diag(H_D2) { fprintf(fd, "ee_read_esys(): esys.phy_base = 0x%08x, esys.base = 0x%08x, esys.size = 0x%08x, esys.mask = 0x%08x\n", (uint) esys.phy_base, (uint) esys.base, (uint) esys.map_size, (uint) esys.map_mask); }

	if (esys.mapped_base == MAP_FAILED)
	{
		warnx("ee_read_esys(): ESYS mmap failure.");
		return E_ERR;
	}

	pfrom = (int *) (esys.base + (from_addr & esys.map_mask));
	diag(H_D2) { fprintf(fd, "ee_read_esys(): reading from from_addr=0x%08x, pto=0x%08x\n", (uint) from_addr, (uint) pfrom); }
	data  = *pfrom;

	munmap(esys.mapped_base, esys.map_size);
	close(memfd);

	return data;
}


// Write a word to an address in the platform space
ssize_t ee_write_esys(off_t to_addr, int data)
{
	e_mmap_t  esys;
	int       memfd;
	int      *pto;

	// Open memory device
	memfd = open("/dev/mem", O_RDWR | O_SYNC);
	if (memfd == 0)
	{
		warnx("ee_write_esys(): /dev/mem file open failure.");
		return E_ERR;
	}

	esys.map_size = 0x1000; // map 4KB page
	esys.map_mask = (esys.map_size - 1);
	esys.phy_base = to_addr & (~esys.map_mask);

	esys.mapped_base = mmap(0, esys.map_size, PROT_READ|PROT_WRITE, MAP_SHARED, memfd, esys.phy_base);
	esys.base = esys.mapped_base + (esys.phy_base & esys.map_mask);

	diag(H_D2) { fprintf(fd, "ee_write_esys(): esys.phy_base = 0x%08x, esys.base = 0x%08x, esys.size = 0x%08x, esys.mask = 0x%08x\n", (uint) esys.phy_base, (uint) esys.base, (uint) esys.map_size, (uint) esys.map_mask); }

	if (esys.mapped_base == MAP_FAILED)
	{
		warnx("ee_write_esys(): ESYS mmap failure.");
		return E_ERR;
	}

	pto = (int *) (esys.base + (to_addr & esys.map_mask));
	diag(H_D2) { fprintf(fd, "ee_write_esys(): writing to to_addr=0x%08x, pto=0x%08x\n", (uint) to_addr, (uint) pto); }
	*pto = data;

	munmap(esys.mapped_base, esys.map_size);
	close(memfd);

	return sizeof(int);
}



/////////////////////////
// Core control functions
//
// Reset the Epiphany platform
int e_reset_system()
{
	diag(H_D1) { fprintf(fd, "e_reset_system(): resetting full ESYS...\n"); }
	ee_write_esys(e_platform.regs_base + E_SYS_RESET, 0);
	sleep(1);
	diag(H_D1) { fprintf(fd, "e_reset_system(): done.\n"); }

	return E_OK;
}


// Reset an e-core
int e_reset_core(e_epiphany_t *dev, unsigned row, unsigned col)
{
	int RESET0 = 0x0;
	int RESET1 = 0x1;

	diag(H_D1) { fprintf(fd, "e_reset_core(): resetting core (%d,%d) (0x%03x)...\n", row, col, dev->core[row][col].id); }
	ee_write_reg(dev, row, col, E_CORE_RESET, RESET1);
	ee_write_reg(dev, row, col, E_CORE_RESET, RESET0);
	diag(H_D1) { fprintf(fd, "e_reset_core(): done.\n"); }

	return E_OK;
}


// Start a program loaded to an e-core in a group
int e_start(e_epiphany_t *dev, unsigned row, unsigned col)
{
	int  SYNC = (1 << E_SYNC);

	diag(H_D1) { fprintf(fd, "e_start(): SYNC (0x%x) to core (%d,%d)...\n", E_ILATST, row, col); }
	ee_write_reg(dev, row, col, E_ILATST, SYNC);
	diag(H_D1) { fprintf(fd, "e_start(): done.\n"); }

	return E_OK;
}


// Signal a software interrupt to an e-core in a group
int e_signal(e_epiphany_t *dev, unsigned row, unsigned col)
{
	int  SWI = (1 << E_SW_INT);

	diag(H_D1) { fprintf(fd, "e_signal(): SWI (0x%x) to core (%d,%d)...\n", E_ILATST, row, col); }
	ee_write_reg(dev, row, col, E_ILATST, SWI);
	diag(H_D1) { fprintf(fd, "e_signal(): done.\n"); }

	return E_OK;
}


// Halt a core
int e_halt(e_epiphany_t *dev, unsigned row, unsigned col)
{
	warnx("e_halt(): this function is not yet implemented.\n");

	return E_OK;
}


// Resume a core after halt
int e_resume(e_epiphany_t *dev, unsigned row, unsigned col)
{
	warnx("e_resume(): this function is not yet implemented.\n");

	return E_OK;
}


////////////////////
// Utility functions

// Convert core coordinates to core-number within a group. No bounds check is performed.
unsigned e_get_num_from_coords(e_epiphany_t *dev, unsigned row, unsigned col)
{
	unsigned corenum;

	corenum = col + row * dev->cols;
	diag(H_D2) { fprintf(fd, "e_get_num_from_coords(): dev.row=%d, dev.col=%d, row=%d, col=%d, corenum=%d\n", dev->row, dev->col, row, col, corenum); }

	return corenum;
}


// Convert CoreID to core-number within a group.
unsigned ee_get_num_from_id(e_epiphany_t *dev, unsigned coreid)
{
	unsigned row, col, corenum;

	row = (coreid >> 6) & 0x3f;
	col = (coreid >> 0) & 0x3f;
	corenum = (col - dev->col) + (row - dev->row) * dev->cols;
	diag(H_D2) { fprintf(fd, "ee_get_num_from_id(): CoreID=0x%03x, dev.row=%d, dev.col=%d, row=%d, col=%d, corenum=%d\n", coreid, dev->row, dev->col, row, col, corenum); }

	return corenum;
}


// Converts core coordinates to CoreID.
unsigned ee_get_id_from_coords(e_epiphany_t *dev, unsigned row, unsigned col)
{
	unsigned coreid;

	coreid = (dev->col + col) + ((dev->row + row) << 6);
	diag(H_D2) { fprintf(fd, "ee_get_id_from_coords(): dev.row=%d, dev.col=%d, row=%d, col=%d, CoreID=0x%03x\n", dev->row, dev->col, row, col, coreid); }

	return coreid;
}


// Converts core-number within a group to CoreID.
unsigned ee_get_id_from_num(e_epiphany_t *dev, unsigned corenum)
{
	unsigned row, col, coreid;

	row = corenum / dev->cols;
	col = corenum % dev->cols;
	coreid = (dev->col + col) + ((dev->row + row) << 6);
	diag(H_D2) { fprintf(fd, "ee_get_id_from_num(): corenum=%d, dev.row=%d, dev.col=%d, row=%d, col=%d, CoreID=0x%03x\n", corenum, dev->row, dev->col, row, col, coreid); }

	return coreid;
}


// Converts CoreID to core coordinates.
void ee_get_coords_from_id(e_epiphany_t *dev, unsigned coreid, unsigned *row, unsigned *col)
{
	*row = ((coreid >> 6) & 0x3f) - dev->row;
	*col = ((coreid >> 0) & 0x3f) - dev->col;
	diag(H_D2) { fprintf(fd, "ee_get_coords_from_id(): CoreID=0x%03x, dev.row=%d, dev.col=%d, row=%d, col=%d\n", coreid, dev->row, dev->col, *row, *col); }

	return;
}

// Converts core-number within a group to core coordinates.
void e_get_coords_from_num(e_epiphany_t *dev, unsigned corenum, unsigned *row, unsigned *col)
{
	*row = corenum / dev->cols;
	*col = corenum % dev->cols;
	diag(H_D2) { fprintf(fd, "e_get_coords_from_num(): corenum=%d, dev.row=%d, dev.col=%d, row=%d, col=%d\n", corenum, dev->row, dev->col, *row, *col); }

	return;
}


// Check if an address is on a chip region
e_bool_t e_is_addr_on_chip(void *addr)
{
	unsigned  row, col;
	unsigned  coreid, i;
	e_chip_t *curr_chip;

	coreid = ((unsigned) addr) >> 20;
	row = (coreid >> 6) & 0x3f;
	col = (coreid >> 0) & 0x3f;

	for (i=0; i<e_platform.num_chips; i++)
	{
		curr_chip = &(e_platform.chip[i]);
		if ((row >= curr_chip->row) && (row < (curr_chip->row + curr_chip->rows)) &&
		    (col >= curr_chip->col) && (col < (curr_chip->col + curr_chip->cols)))
			return e_true;
	}

	return e_false;
}


// Check if an address is on a core-group region
e_bool_t e_is_addr_on_group(e_epiphany_t *dev, void *addr)
{
	unsigned row, col;
	unsigned coreid;

	coreid = ((unsigned) addr) >> 20;
	ee_get_coords_from_id(dev, coreid, &row, &col);

	if ((row >= 0) && (row < dev->rows) &&
	    (col >= 0) && (col < dev->cols))
		return e_true;

	return e_false;
}


void e_set_host_verbosity(e_hal_diag_t verbose)
{
	fd = stderr;
	e_host_verbose = verbose;

	return;
}



////////////////////////////////////
// HDF parser

int ee_parse_hdf(e_platform_t *dev, char *hdf)
{
	int   ret = E_ERR;
	char *ext;

	if (strlen(hdf) >= 4)
	{
		ext = hdf + strlen(hdf) - 4;
		if (!strcmp(ext, ".hdf"))
			ret = ee_parse_simple_hdf(dev, hdf);
		else if (!strcmp(ext, ".xml"))
			ret = ee_parse_xml_hdf(dev, hdf);
		else
			ret = E_ERR;
	} else {
		ret = E_ERR;
	}

	return ret;
}

int ee_parse_simple_hdf(e_platform_t *dev, char *hdf)
{
	FILE       *fp;
	int         chip_num;
	int         emem_num;
	e_chip_t   *curr_chip;
	e_memseg_t *curr_emem;

	char line[255], etag[255], eval[255];
	int l;

	fp = fopen(hdf, "r");
	if (fp == NULL)
	{
		warnx("ee_parse_simple_hdf(): Can't open Hardware Definition File (HDF) %s.", hdf);
		return E_ERR;
	}

	chip_num = -1;
	emem_num = -1;

	l = 0;

	while (!feof(fp))
	{
		l++;
		fgets(line, sizeof(line), fp);
		ee_trim(line);
		if (!strcmp(line, ""))
			continue;
		sscanf(line, "%s %s", etag, eval);
		diag(H_D3) { fprintf(fd, "ee_parse_simple_hdf(): line %d: %s %s\n", l, etag, eval); }


		// Platform definition
		if      (!strcmp("PLATFORM_VERSION", etag))
		{
			sscanf(eval, "%s", dev->version);
			diag(H_D3) { fprintf(fd, "ee_parse_simple_hdf(): platform version = %s\n", dev->version); }
		}

		else if (!strcmp("NUM_CHIPS", etag))
		{
			sscanf(eval, "%d", &(dev->num_chips));
			dev->chip = (e_chip_t *) calloc(dev->num_chips, sizeof(e_chip_t));
			diag(H_D3) { fprintf(fd, "ee_parse_simple_hdf(): number of chips = %d\n", dev->num_chips); }
		}

		else if (!strcmp("NUM_EXT_MEMS", etag))
		{
			sscanf(eval, "%d", &(dev->num_emems));
			dev->emem = (e_memseg_t *) calloc(dev->num_emems, sizeof(e_memseg_t));
			diag(H_D3) { fprintf(fd, "ee_parse_simple_hdf(): number of ext. memory segments = %d\n", dev->num_emems); }
		}

		else if (!strcmp("ESYS_REGS_BASE", etag))
		{
			sscanf(eval, "%x", &(dev->regs_base));
			diag(H_D3) { fprintf(fd, "ee_parse_simple_hdf(): base address of platform registers = 0x%08x\n", dev->regs_base); }
		}


		// Chip definition
		else if (!strcmp("CHIP", etag))
		{
			chip_num++;
			curr_chip = &(dev->chip[chip_num]);
			sscanf(eval, "%s", curr_chip->version);
			diag(H_D3) { fprintf(fd, "ee_parse_simple_hdf(): processing chip #%d, version = \"%s\"\n", chip_num, curr_chip->version); }
			diag(H_D3) { fprintf(fd, "ee_parse_simple_hdf(): chip version = %s\n", curr_chip->version); }
		}

		else if (!strcmp("CHIP_ROW", etag))
		{
			sscanf(eval, "%d", &(curr_chip->row));
			diag(H_D3) { fprintf(fd, "ee_parse_simple_hdf(): chip row = %d\n", curr_chip->row); }
		}

		else if (!strcmp("CHIP_COL", etag))
		{
			sscanf(eval, "%d", &(curr_chip->col));
			diag(H_D3) { fprintf(fd, "ee_parse_simple_hdf(): chip col = %d\n", curr_chip->col); }
		}


		// External memory definitions
		else if (!strcmp("EMEM", etag))
		{
			emem_num++;
			curr_emem = &(dev->emem[emem_num]);
			diag(H_D3) { fprintf(fd, "ee_parse_simple_hdf(): processing external memory segment #%d\n", emem_num); }
		}

		else if (!strcmp("EMEM_BASE_ADDRESS", etag))
		{
			sscanf(eval, "%x", (unsigned int *) &(curr_emem->phy_base));
			diag(H_D3) { fprintf(fd, "ee_parse_simple_hdf(): base addr. of ext. mem. segment = 0x%08x\n", (uint) curr_emem->phy_base); }
		}

		else if (!strcmp("EMEM_EPI_BASE", etag))
		{
			sscanf(eval, "%x", (unsigned int *) &(curr_emem->ephy_base));
			diag(H_D3) { fprintf(fd, "ee_parse_simple_hdf(): base addr. of ext. mem. segment (device side)= 0x%08x\n", (uint) curr_emem->ephy_base); }
		}

		else if (!strcmp("EMEM_SIZE", etag))
		{
			sscanf(eval, "%x", (unsigned int *) &(curr_emem->size));
			diag(H_D3) { fprintf(fd, "ee_parse_simple_hdf(): size of ext. mem. segment = %x\n", (uint) curr_emem->size); }
		}

		else if (!strcmp("EMEM_TYPE", etag))
		{
			if (!strcmp(etag, "RD"))
				curr_emem->type = E_RD;
			else if (!strcmp(etag, "WR"))
				curr_emem->type = E_WR;
			else if (!strcmp(etag, "RDWR"))
				curr_emem->type = E_RDWR;
			diag(H_D3) { fprintf(fd, "ee_parse_simple_hdf(): type of ext. mem. segment = %x\n", (uint) curr_emem->type); }
		}


		// Other
		else if (!strcmp("//", etag))
		{
			;
			diag(H_D3) { fprintf(fd, "ee_parse_simple_hdf(): comment\n"); }
		}
		else {
			return E_ERR;
		}
	}

	fclose(fp);

	return E_OK;
}


int ee_parse_xml_hdf(e_platform_t *dev, char *hdf)
{
	warnx("e_init(): XML file format is not yet supported. Please use simple HDF format.");

	return E_ERR;
}


// Platform data structures
typedef struct {
	e_objytpe_t      objtype;     // object type identifier
	e_chiptype_t     type;        // Epiphany chip part number
	char             version[32]; // version name of Epiphany chip
	unsigned int     arch;        // architecture generation
	unsigned int     rows;        // number of rows in chip
	unsigned int     cols;        // number of cols in chip
	unsigned int     sram_base;   // base offset of core SRAM
	unsigned int     sram_size;   // size of core SRAM
	unsigned int     regs_base;   // base offset of core registers
	unsigned int     regs_size;   // size of core registers segment
	off_t            ioregs_n;    // base address of north IO register
	off_t            ioregs_e;    // base address of east IO register
	off_t            ioregs_s;    // base address of south IO register
	off_t            ioregs_w;    // base address of west IO register
} e_chip_db_t;

#define NUM_CHIP_VERSIONS 2
e_chip_db_t chip_params_table[NUM_CHIP_VERSIONS] = {
		{E_EPI_CHIP, E_E16G301, "E16G301", 3, 4, 4, 0x00000, 0x08000, 0xf0000, 0x01000, 0x002f0000, 0x0a3f0000, 0x0c2f0000, 0x080f0000},
		{E_EPI_CHIP, E_E64G401, "E64G401", 4, 8, 8, 0x00000, 0x08000, 0xf0000, 0x01000, 0x002f0000, 0x087f0000, 0x1c2f0000, 0x080f0000},
};


int ee_set_chip_params(e_chip_t *dev)
{
	int chip_ver;

	for (chip_ver = (NUM_CHIP_VERSIONS-1); chip_ver > -1; chip_ver--)
		if (!strcmp(dev->version, chip_params_table[chip_ver].version))
		{
			strcpy(dev->version, chip_params_table[chip_ver].version);
			dev->arch = chip_params_table[chip_ver].arch;
			dev->rows = chip_params_table[chip_ver].rows;
			dev->cols = chip_params_table[chip_ver].cols;
			dev->num_cores = dev->rows * dev->cols;
			dev->sram_base = chip_params_table[chip_ver].sram_base;
			dev->sram_size = chip_params_table[chip_ver].sram_size;
			dev->regs_base = chip_params_table[chip_ver].regs_base;
			dev->regs_size = chip_params_table[chip_ver].regs_size;
			dev->ioregs_n = chip_params_table[chip_ver].ioregs_n;
			dev->ioregs_e = chip_params_table[chip_ver].ioregs_e;
			dev->ioregs_s = chip_params_table[chip_ver].ioregs_s;
			dev->ioregs_w = chip_params_table[chip_ver].ioregs_w;

			break;
		}

	return E_OK;
}


void ee_trim(char *a)
{
    char *b = a;
    while (isspace(*b))   ++b;
    while (*b)            *a++ = *b++;
    *a = '\0';
    while (isspace(*--a)) *a = '\0';
}
