/*
  File: epiphany-hal.c

  This file is part of the Epiphany Software Development Kit.

  Copyright (C) 2013 Adapteva, Inc.
  See AUTHORS for list of contributors
  Support e-mail: <support@adapteva.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License (LGPL)
  as published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  and the GNU Lesser General Public License along with this program,
  see the files COPYING and COPYING.LESSER.	 If not, see
  <http://www.gnu.org/licenses/>.
*/

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

/* Redesigned driver API */
#include "epiphany2.h"

#include "e-hal.h"
#include "epiphany-shm-manager.h"	/* For private APIs */
#include "epiphany-hal-api-local.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"

typedef unsigned int  uint;
typedef unsigned long ulong;

#define diag(vN) if (e_host_verbose >= vN)

//static int e_host_verbose = 0;
int	  e_host_verbose; //__attribute__ ((visibility ("hidden"))) = 0;
FILE *diag_fd			  __attribute__ ((visibility ("hidden")));

char *OBJTYPE[64] = {"NULL", "EPI_PLATFORM", "EPI_CHIP", "EPI_GROUP", "EPI_CORE", "EXT_MEM"};

char const esdk_path[] = "EPIPHANY_HOME";
char const hdf_env_var_name[] = "EPIPHANY_HDF";

#ifdef ESIM_TARGET
extern const struct e_target_ops esim_target_ops;
#endif
#ifdef PAL_TARGET
extern const struct e_target_ops pal_target_ops;
#endif
const struct e_target_ops native_target_ops;

e_platform_t e_platform = {
	.objtype = E_EPI_PLATFORM,
	.target_ops = &native_target_ops,
};

/////////////////////////////////
// Device communication functions
//
// Platform configuration
//
// Initialize Epiphany platform according to configuration found in the HDF
int e_init(char *hdf)
{
	int i;

	// Init global file descriptor
	diag_fd = stderr;

	e_platform.objtype	   = E_EPI_PLATFORM;
	e_platform.hal_ver	   = 0x050d0705; // TODO: update ver
	e_platform.initialized = E_FALSE;
	e_platform.num_chips   = 0;
	e_platform.num_emems   = 0;

#ifndef ESIM_TARGET
	if (ee_esim_target_p()) {
		warnx("e_init(): " EHAL_TARGET_ENV " environment variable set to esim but target not compiled in.");
		return E_ERR;
	}
#endif

#ifndef PAL_TARGET
	if (ee_pal_target_p()) {
		warnx("e_init(): " EHAL_TARGET_ENV " environment variable set to pal but target not compiled in.");
		return E_ERR;
	}
#endif

#ifdef ESIM_TARGET
	if (ee_esim_target_p())
		e_platform.target_ops = &esim_target_ops;
#endif

#ifdef PAL_TARGET
	if (ee_pal_target_p())
		e_platform.target_ops = &pal_target_ops;
#endif

	if (E_OK != e_platform.target_ops->init())
		return E_ERR;

	if (E_OK != e_platform.target_ops->populate_platform(&e_platform, hdf))
		return E_ERR;

	// Populate the missing platform parameters according to platform version.
	for (i=0; i<e_platform.num_chips; i++)
	{
		ee_set_platform_params(&e_platform);
	}

	// Populate the missing chip parameters according to chip version.
	for (i=0; i<e_platform.num_chips; i++)
	{
		ee_set_chip_params(&(e_platform.chip[i]));
	}

	// Find the minimal bounding box of Epiphany chips. This defines the reference frame for core-groups.
	e_platform.row	= 0x3f;
	e_platform.col	= 0x3f;
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
	diag(H_D2) { fprintf(diag_fd, "e_init(): platform.(row,col)	  = (%d,%d)\n", e_platform.row, e_platform.col); }
	diag(H_D2) { fprintf(diag_fd, "e_init(): platform.(rows,cols) = (%d,%d)\n", e_platform.rows, e_platform.cols); }

	e_platform.initialized = E_TRUE;

	return E_OK;
}


// Finalize connection with the Epiphany platform; Free allocated resources.
int e_finalize(void)
{
	if (e_platform.initialized == E_FALSE)
	{
		warnx("e_finalize(): Platform was not initiated.");
		return E_ERR;
	}

	e_shm_finalize();

	e_platform.target_ops->finalize();

	e_platform.initialized = E_FALSE;

	free(e_platform.chip);
	free(e_platform.emem);

	return E_OK;
}


int e_get_platform_info(e_platform_t *platform)
{
	if (e_platform.initialized == E_FALSE)
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

// Define an e-core workgroup

static int ee_open_native(e_epiphany_t *dev, unsigned row, unsigned col,
						  unsigned rows, unsigned cols)
{
	// Open memory device
	dev->memfd = open(EPIPHANY_DEV, O_RDWR | O_SYNC);
	if (dev->memfd == -1) {
		warnx("e_open(): EPIPHANY_DEV file open failure.");
		return E_ERR;
	}

	return E_OK;
}

int e_open(e_epiphany_t *dev, unsigned row, unsigned col, unsigned rows, unsigned cols)
{
	int irow, icol;
	e_core_t *curr_core;

	if (e_platform.initialized == E_FALSE)
	{
		warnx("e_open(): Platform was not initialized. Use e_init().");
		return E_ERR;
	}

	dev->objtype = E_EPI_GROUP;
	dev->type	 = e_platform.chip[0].type; // TODO: assumes one chip type in platform

	// Set device geometry
	// TODO: check if coordinates and size are legal.
	diag(H_D2) { fprintf(diag_fd, "e_open(): platform.(row,col)=(%d,%d)\n", e_platform.row, e_platform.col); }
	dev->row		 = row + e_platform.row;
	dev->col		 = col + e_platform.col;
	dev->rows		 = rows;
	dev->cols		 = cols;
	dev->num_cores	 = dev->rows * dev->cols;
	diag(H_D2) { fprintf(diag_fd, "e_open(): dev.(row,col,rows,cols)=(%d,%d,%d,%d), (row,col)=(%d,%d), num_cores=%d\n", dev->row, dev->col, dev->rows, dev->cols, row, col, dev->num_cores); }
	dev->base_coreid = ee_get_id_from_coords(dev, 0, 0);

	diag(H_D2) { fprintf(diag_fd, "e_open(): group.(row,col),id = (%d,%d), 0x%03x\n", dev->row, dev->col, dev->base_coreid); }
	diag(H_D2) { fprintf(diag_fd, "e_open(): group.(rows,cols),numcores = (%d,%d), %d\n", dev->rows, dev->cols, dev->num_cores); }

	if (e_platform.target_ops->open(dev, row, col, rows, cols) != E_OK)
		return E_ERR;

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
			diag(H_D2) { fprintf(diag_fd, "e_open(): opening core (%d,%d)\n", irow, icol); }

			curr_core = &(dev->core[irow][icol]);
			curr_core->row = irow;
			curr_core->col = icol;
			curr_core->id  = ee_get_id_from_coords(dev, curr_core->row, curr_core->col);

			diag(H_D2) { fprintf(diag_fd, "e_open(): core (%d,%d), CoreID = 0x%03x\n", curr_core->row, curr_core->col, curr_core->id); }

			//	  |-------------|	  req'd map
			// +--0-+--1-+--2-+--3-+  O/S pages
			// |--x-------------|  gen'd map; x = offset

			// SRAM array
			curr_core->mems.phy_base = (curr_core->id << 20 | e_platform.chip[0].sram_base); // TODO: assumes first chip + a single chip type
			curr_core->mems.page_base = ee_rndl_page(curr_core->mems.phy_base);
			curr_core->mems.page_offset = curr_core->mems.phy_base - curr_core->mems.page_base;
			curr_core->mems.map_size = e_platform.chip[0].sram_size + curr_core->mems.page_offset;

			if (ee_native_target_p()) {
				curr_core->mems.mapped_base = mmap(NULL, curr_core->mems.map_size, PROT_READ|PROT_WRITE, MAP_SHARED, dev->memfd, curr_core->mems.page_base);
				curr_core->mems.base = curr_core->mems.mapped_base + curr_core->mems.page_offset;

				diag(H_D2) { fprintf(diag_fd, "e_open(): mems.phy_base = 0x%08x, mems.base = 0x%08x, mems.size = 0x%08x\n", (uint) curr_core->mems.phy_base, (uint) curr_core->mems.base, (uint) curr_core->mems.map_size); }
			}

			// e-core regs
			curr_core->regs.phy_base = (curr_core->id << 20 | e_platform.chip[0].regs_base); // TODO: assumes first chip + a single chip type
			curr_core->regs.page_base = ee_rndl_page(curr_core->regs.phy_base);
			curr_core->regs.page_offset = curr_core->regs.phy_base - curr_core->regs.page_base;
			curr_core->regs.map_size = e_platform.chip[0].regs_size + curr_core->regs.page_offset;

			if (ee_native_target_p()) {
				curr_core->regs.mapped_base = mmap(NULL, curr_core->regs.map_size, PROT_READ|PROT_WRITE, MAP_SHARED, dev->memfd, curr_core->regs.page_base);
				curr_core->regs.base = curr_core->regs.mapped_base + curr_core->regs.page_offset;

				diag(H_D2) { fprintf(diag_fd, "e_open(): regs.phy_base = 0x%08x, regs.base = 0x%08x, regs.size = 0x%08x\n", (uint) curr_core->regs.phy_base, (uint) curr_core->regs.base, (uint) curr_core->regs.map_size); }

				if (curr_core->mems.mapped_base == MAP_FAILED)
				{
					warnx("e_open(): ECORE[%d,%d] MEM mmap failure.", curr_core->row, curr_core->col);
					return E_ERR;
				}

				if (curr_core->regs.mapped_base == MAP_FAILED)
				{
					warnx("e_open(): ECORE[%d,%d] REG mmap failure.", curr_core->row, curr_core->col);
					return E_ERR;
				}
			}
#if 0
			/* Nope, breaks e_read and e_write */
			if (ee_soft_reset_core(dev, irow, icol) != E_OK)
				warnx("%s: ee_soft_reset_core failed", __func__);
#endif
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

	if (ee_pal_target_p())
		return e_platform.target_ops->close(dev);

	for (irow=0; irow<dev->rows; irow++)
	{
		if (ee_native_target_p()) {
			for (icol=0; icol<dev->cols; icol++)
			{
#if 0
				/* Nope, breaks e-read / e-write */
				if (ee_soft_reset_core(dev, irow, icol) != E_OK)
					warnx("%s: ee_soft_reset_core failed", __func__);
#endif

				curr_core = &(dev->core[irow][icol]);

				munmap(curr_core->mems.mapped_base, curr_core->mems.map_size);
				munmap(curr_core->regs.mapped_base, curr_core->regs.map_size);
			}
		}

		free(dev->core[irow]);
	}

	free(dev->core);

	if (ee_native_target_p())
		close(dev->memfd);

	return E_OK;
}


// Read a memory block from a core in a group
ssize_t e_read(void *dev, unsigned row, unsigned col, off_t from_addr, void *buf, size_t size)
{
	ssize_t		  rcount;
	e_epiphany_t *edev;
	e_mem_t		 *mdev;

	switch (*((e_objtype_t *) dev))
	{
	case E_EPI_GROUP:
		diag(H_D2) { fprintf(diag_fd, "e_read(): detected EPI_GROUP object.\n"); }
		edev = (e_epiphany_t *) dev;
		if (from_addr < edev->core[row][col].mems.map_size)
			rcount = ee_read_buf(edev, row, col, from_addr, buf, size);
		else {
			*((unsigned *) (buf)) = ee_read_reg(dev, row, col, from_addr);
			rcount = 4;
		}
		break;

	case E_SHARED_MEM:	// Fall-through
	case E_EXT_MEM:
		diag(H_D2) { fprintf(diag_fd, "e_read(): detected EXT_MEM object.\n"); }
		mdev = (e_mem_t *) dev;
		rcount = ee_mread_buf(mdev, from_addr, buf, size);
		break;

	default:
		diag(H_D2) { fprintf(diag_fd, "e_read(): invalid object type.\n"); }
		rcount = 0;
		return E_ERR;
	}

	return rcount;
}


// Write a memory block to a core in a group
ssize_t e_write(void *dev, unsigned row, unsigned col, off_t to_addr, const void *buf, size_t size)
{
	ssize_t		  wcount;
	unsigned int  reg;
	e_epiphany_t *edev;
	e_mem_t		 *mdev;

	switch (*((e_objtype_t *) dev))
	{
	case E_EPI_GROUP:
		diag(H_D2) { fprintf(diag_fd, "e_write(): detected EPI_GROUP object.\n"); }
		edev = (e_epiphany_t *) dev;
		if (to_addr < edev->core[row][col].mems.map_size)
			wcount = ee_write_buf(edev, row, col, to_addr, buf, size);
		else {
			reg = *((unsigned *) (buf));
			ee_write_reg(edev, row, col, to_addr, reg);
			wcount = 4;
		}
		break;

	case E_SHARED_MEM:	// Fall-through
	case E_EXT_MEM:
		diag(H_D2) { fprintf(diag_fd, "e_write(): detected EXT_MEM object.\n"); }
		mdev = (e_mem_t *) dev;
		wcount = ee_mwrite_buf(mdev, to_addr, buf, size);
		break;

	default:
		diag(H_D2) { fprintf(diag_fd, "e_write(): invalid object type.\n"); }
		wcount = 0;
		return E_ERR;
	}

	return wcount;
}


static int ee_read_word_native(e_epiphany_t *dev, unsigned row, unsigned col, const off_t from_addr)
{
	volatile int *pfrom;
	int			  data;
	ssize_t		  size;

	size = sizeof(int);
	if (((from_addr + size) > dev->core[row][col].mems.map_size) || (from_addr < 0))
	{
		diag(H_D2) { fprintf(diag_fd, "ee_read_word(): writing to from_addr=0x%08x, size=%d, map_size=0x%x\n", (uint) from_addr, (uint) size, (uint) dev->core[row][col].mems.map_size); }
		warnx("ee_read_word(): Buffer range is out of bounds.");
		return E_ERR;
	}

	pfrom = (int *) (dev->core[row][col].mems.base + from_addr);
	diag(H_D2) { fprintf(diag_fd, "ee_read_word(): reading from from_addr=0x%08x, pfrom=0x%08x\n", (uint) from_addr, (uint) pfrom); }
	data  = *pfrom;

	return data;
}

// Read a word from SRAM of a core in a group
int ee_read_word(e_epiphany_t *dev, unsigned row, unsigned col, const off_t from_addr)
{
	return e_platform.target_ops->ee_read_word(dev, row, col, from_addr);
}


static ssize_t ee_write_word_native(e_epiphany_t *dev, unsigned row, unsigned col, off_t to_addr, int data)
{
	int		*pto;
	ssize_t	 size;

	size = sizeof(int);
	if (((to_addr + size) > dev->core[row][col].mems.map_size) || (to_addr < 0))
	{
		diag(H_D2) { fprintf(diag_fd, "ee_write_word(): writing to to_addr=0x%08x, size=%d, map_size=0x%x\n", (uint) to_addr, (uint) size, (uint) dev->core[row][col].mems.map_size); }
		warnx("ee_write_word(): Buffer range is out of bounds.");
		return E_ERR;
	}

	pto = (int *) (dev->core[row][col].mems.base + to_addr);
	diag(H_D2) { fprintf(diag_fd, "ee_write_word(): writing to to_addr=0x%08x, pto=0x%08x\n", (uint) to_addr, (uint) pto); }
	*pto = data;

	return sizeof(int);
}

// Write a word to SRAM of a core in a group
ssize_t ee_write_word(e_epiphany_t *dev, unsigned row, unsigned col, off_t to_addr, int data)
{
	return e_platform.target_ops->ee_write_word(dev, row, col, to_addr, data);
}


static inline void *aligned_memcpy(void *__restrict__ dst,
		const void *__restrict__ src, size_t size)
{
	size_t n, aligned_n;
	uint8_t *d;
	const uint8_t *s;

	n = size;
	d = (uint8_t *) dst;
	s = (const uint8_t *) src;

	if (!(((uintptr_t) d ^ (uintptr_t) s) & 3)) {
		/* dst and src are evenly WORD (un-)aligned */

		/* Align by WORD */
		if (n && (((uintptr_t) d) & 1)) {
			*d++ = *s++; n--;
		}
		if (((uintptr_t) d) & 2) {
			if (n > 1) {
				*((uint16_t *) d) = *((const uint16_t *) s);
				d+=2; s+=2; n-=2;
			} else if (n==1) {
				*d++ = *s++; n--;
			}
		}

		aligned_n = n & (~3);
		memcpy((void *) d, (void *) s, aligned_n);
		d += aligned_n; s += aligned_n; n -= aligned_n;

		/* Copy remainder in largest possible chunks */
		switch (n) {
		case 2:
			*((uint16_t *) d) = *((const uint16_t *) s);
			d+=2; s+=2; n-=2;
			break;
		case 3:
			*((uint16_t *) d) = *((const uint16_t *) s);
			d+=2; s+=2; n-=2;
		case 1:
			*d++ = *s++; n--;
		}
	} else if (!(((uintptr_t) d ^ (uintptr_t) s) & 1)) {
		/* dst and src are evenly half-WORD (un-)aligned */

		/* Align by half-WORD */
		if (n && ((uintptr_t) d) & 1) {
			*d++ = *s++; n--;
		}

		while (n > 1) {
			*((uint16_t *) d) = *((const uint16_t *) s);
			d+=2; s+=2; n-=2;
		}

		/* Copy remaining byte */
		if (n) {
			*d++ = *s++; n--;
		}
	} else {
		/* Resort to single byte copying */
		while (n) {
			*d++ = *s++; n--;
		}
	}

	assert(n == 0);
	assert((uintptr_t) dst + size == (uintptr_t) d);
	assert((uintptr_t) src + size == (uintptr_t) s);

	return dst;
}


static ssize_t ee_read_buf_native(e_epiphany_t *dev, unsigned row, unsigned col, const off_t from_addr, void *buf, size_t size)
{
	const void	 *pfrom;
	unsigned int  addr_from, addr_to, align;
	int			  i;

	if (((from_addr + size) > dev->core[row][col].mems.map_size) || (from_addr < 0))
	{
		diag(H_D2) { fprintf(diag_fd, "ee_read_buf(): reading from from_addr=0x%08x, size=%d, map_size=0x%x\n", (uint) from_addr, (uint) size, (uint) dev->core[row][col].mems.map_size); }
		warnx("ee_read_buf(): Buffer range is out of bounds.");
		return E_ERR;
	}

	pfrom = dev->core[row][col].mems.base + from_addr;
	diag(H_D2) { fprintf(diag_fd, "ee_read_buf(): reading from from_addr=0x%08x, pfrom=0x%08x, size=%d\n", (uint) from_addr, (uint) pfrom, (int) size); }

	if ((dev->type == E_E64G401) && ((row >= 1) && (row <= 2)))
	{
		// The following code is a fix for the E64G401 anomaly of bursting reads from eCore
		// internal memory back to host, from rows #1 and #2.
		addr_from = (unsigned int) pfrom;
		addr_to	  = (unsigned int) buf;
		align	  = (addr_from | addr_to | size) & 0x7;

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

// Read a memory block from SRAM of a core in a group
ssize_t ee_read_buf(e_epiphany_t *dev, unsigned row, unsigned col, const off_t from_addr, void *buf, size_t size)
{
	return e_platform.target_ops->ee_read_buf(dev, row, col, from_addr, buf, size);
}


static ssize_t ee_write_buf_native(e_epiphany_t *dev, unsigned row, unsigned col, off_t to_addr, const void *buf, size_t size)
{
	void *pto;

	if (((to_addr + size) > dev->core[row][col].mems.map_size) || (to_addr < 0))
	{
		diag(H_D2) { fprintf(diag_fd, "ee_write_buf(): writing to to_addr=0x%08x, size=%d, map_size=0x%x\n", (uint) to_addr, (uint) size, (uint) dev->core[row][col].mems.map_size); }
		warnx("ee_write_buf(): Buffer range is out of bounds.");
		return E_ERR;
	}

	pto = dev->core[row][col].mems.base + to_addr;
	diag(H_D2) { fprintf(diag_fd, "ee_write_buf(): writing to to_addr=0x%08x, pto=0x%08x, size=%d\n", (uint) to_addr, (uint) pto, (uint) size); }

	aligned_memcpy(pto, buf, size);

	return size;
}

// Write a memory block to SRAM of a core in a group
ssize_t ee_write_buf(e_epiphany_t *dev, unsigned row, unsigned col, off_t to_addr, const void *buf, size_t size)
{
	return e_platform.target_ops->ee_write_buf(dev, row, col, to_addr, buf, size);
}

static int ee_read_reg_native(e_epiphany_t *dev, unsigned row, unsigned col, const off_t from_addr)
{
	volatile int *pfrom;
	off_t		  addr;
	int			  data;
	ssize_t		  size;

	addr = from_addr;
	if (addr >= E_REG_R0)
		addr = addr - E_REG_R0;

	size = sizeof(int);
	if (((addr + size) > dev->core[row][col].regs.map_size) || (addr < 0))
	{
		diag(H_D2) { fprintf(diag_fd, "ee_read_reg(): from_addr=0x%08x, size=0x%08x, map_size=0x%08x\n", (uint) from_addr, (uint) size, (uint) dev->core[row][col].regs.map_size); }
		warnx("ee_read_reg(): Address is out of bounds.");
		return E_ERR;
	}

	pfrom = (int *) (dev->core[row][col].regs.base + addr);
	diag(H_D2) { fprintf(diag_fd, "ee_read_reg(): reading from from_addr=0x%08x, pfrom=0x%08x\n", (uint) from_addr, (uint) pfrom); }
	data  = *pfrom;

	return data;
}

// Read a core register from a core in a group
int ee_read_reg(e_epiphany_t *dev, unsigned row, unsigned col, const off_t from_addr)
{
	return e_platform.target_ops->ee_read_reg(dev, row, col, from_addr);
}


static ssize_t ee_write_reg_native(e_epiphany_t *dev, unsigned row, unsigned col, off_t to_addr, int data)
{
	int		*pto;
	ssize_t	 size;

	if (to_addr >= E_REG_R0)
		to_addr = to_addr - E_REG_R0;

	size = sizeof(int);
	if (((to_addr + size) > dev->core[row][col].regs.map_size) || (to_addr < 0))
	{
		diag(H_D2) { fprintf(diag_fd, "ee_write_reg(): writing to to_addr=0x%08x, size=%d, map_size=0x%x\n", (uint) to_addr, (uint) size, (uint) dev->core[row][col].regs.map_size); }
		warnx("ee_write_reg(): Address is out of bounds.");
		return E_ERR;
	}

	pto = (int *) (dev->core[row][col].regs.base + to_addr);
	diag(H_D2) { fprintf(diag_fd, "ee_write_reg(): writing to to_addr=0x%08x, pto=0x%08x\n", (uint) to_addr, (uint) pto); }
	*pto = data;

	return sizeof(int);
}

// Write to a core register of a core in a group
ssize_t ee_write_reg(e_epiphany_t *dev, unsigned row, unsigned col, off_t to_addr, int data)
{
	return e_platform.target_ops->ee_write_reg(dev, row, col, to_addr, data);
}

// External Memory access

// Allocate a buffer in external memory
static int shm_alloc_native(e_mem_t *mbuf)
{
	return E_OK;
}

// Allocate a buffer in external memory

static int alloc_native(e_mem_t *mbuf)
{
	mbuf->memfd = open(EPIPHANY_DEV, O_RDWR | O_SYNC);
	if (mbuf->memfd == -1)
	{
		warnx("e_alloc(): EPIPHANY_DEV file open failure.");
		return E_ERR;
	}

	diag(H_D2) { fprintf(diag_fd, "e_alloc(): mbuf.phy_base = 0x%08x, mbuf.ephy_base = 0x%08x, mbuf.base = 0x%08x, mbuf.size = 0x%08x\n", (uint) mbuf->phy_base, (uint) mbuf->ephy_base, (uint) mbuf->base, (uint) mbuf->map_size); }

	mbuf->mapped_base = mmap(NULL, mbuf->map_size, PROT_READ|PROT_WRITE, MAP_SHARED, mbuf->memfd, mbuf->page_base);
	mbuf->base = (void*)(((char*)mbuf->mapped_base) + mbuf->page_offset);

	if (mbuf->mapped_base == MAP_FAILED)
	{
		warnx("e_alloc(): mmap failure.");
		return E_ERR;
	}

	return E_OK;
}

int e_alloc(e_mem_t *mbuf, off_t offset, size_t size)
{
	if (e_platform.initialized == E_FALSE)
	{
		warnx("e_alloc(): Platform was not initialized. Use e_init().");
		return E_ERR;
	}

	mbuf->objtype = E_EXT_MEM;
	mbuf->priv = NULL;

	diag(H_D2) { fprintf(diag_fd, "e_alloc(): allocating EMEM buffer at offset 0x%08x\n", (uint) offset); }

	mbuf->phy_base = (e_platform.emem[0].phy_base + offset); // TODO: this takes only the 1st segment into account
	mbuf->page_base = ee_rndl_page(mbuf->phy_base);
	mbuf->page_offset = mbuf->phy_base - mbuf->page_base;
	mbuf->map_size = size + mbuf->page_offset;

	mbuf->ephy_base = (e_platform.emem[0].ephy_base + offset); // TODO: this takes only the 1st segment into account
	mbuf->emap_size = size;

	return e_platform.target_ops->alloc(mbuf);
}

// Free a memory buffer in external memory

static int free_native(e_mem_t *mbuf)
{
	munmap(mbuf->mapped_base, mbuf->map_size);
	close(mbuf->memfd);

	return E_OK;
}

int e_free(e_mem_t *mbuf)
{
	if (!mbuf)
		return E_ERR;

	if (E_SHARED_MEM == mbuf->objtype) {
		// The shared memory mapping is persistent - don't unmap
		return E_OK;
	}

	return e_platform.target_ops->free(mbuf);
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


static int ee_mread_word_native(e_mem_t *mbuf, const off_t from_addr)
{
	volatile int *pfrom;
	int			  data;
	ssize_t		  size;

	size = sizeof(int);
	if (((from_addr + size) > mbuf->map_size) || (from_addr < 0))
	{
		diag(H_D2) { fprintf(diag_fd, "ee_mread_word(): writing to from_addr=0x%08x, size=%d, map_size=0x%x\n", (uint) from_addr, (uint) size, (uint) mbuf->map_size); }
		warnx("ee_mread_word(): Address is out of bounds.");
		return E_ERR;
	}

	pfrom = (int *) (mbuf->base + from_addr);
	diag(H_D2) { fprintf(diag_fd, "ee_mread_word(): reading from from_addr=0x%08x, pfrom=0x%08x\n", (uint) from_addr, (uint) pfrom); }
	data  = *pfrom;

	return data;
}

// Read a word from an external memory buffer
int ee_mread_word(e_mem_t *mbuf, const off_t from_addr)
{
	return e_platform.target_ops->ee_mread_word(mbuf, from_addr);
}


static ssize_t ee_mwrite_word_native(e_mem_t *mbuf, off_t to_addr, int data)
{
	int		*pto;
	ssize_t	 size;

	size = sizeof(int);
	if (((to_addr + size) > mbuf->map_size) || (to_addr < 0))
	{
		diag(H_D2) { fprintf(diag_fd, "ee_mwrite_word(): writing to to_addr=0x%08x, size=%d, map_size=0x%x\n", (uint) to_addr, (uint) size, (uint) mbuf->map_size); }
		warnx("ee_mwrite_word(): Address is out of bounds.");
		return E_ERR;
	}

	pto = (int *) (mbuf->base + to_addr);
	diag(H_D2) { fprintf(diag_fd, "ee_mwrite_word(): writing to to_addr=0x%08x, pto=0x%08x\n", (uint) to_addr, (uint) pto); }
	*pto = data;

	return sizeof(int);
}

// Write a word to an external memory buffer
ssize_t ee_mwrite_word(e_mem_t *mbuf, off_t to_addr, int data)
{
	return e_platform.target_ops->ee_mwrite_word(mbuf, to_addr, data);
}


static ssize_t ee_mread_buf_native(e_mem_t *mbuf, const off_t from_addr, void *buf, size_t size)
{
	const void *pfrom;

	if (((from_addr + size) > mbuf->map_size) || (from_addr < 0))
	{
		warnx("ee_mread_buf(): Address is out of bounds.");
		return E_ERR;
	}

	pfrom = mbuf->base + from_addr;

	diag(H_D1) {
		fprintf(diag_fd, "ee_mread_buf(): reading from from_addr=0x%08x, "
				"offset=0x%08x, size=%d, map_size=0x%x\n", (uint) pfrom,
				(uint)from_addr, (uint) size, (uint) mbuf->map_size);
	}

	memcpy(buf, pfrom, size);

	return size;
}

// Read a block from an external memory buffer
ssize_t ee_mread_buf(e_mem_t *mbuf, const off_t from_addr, void *buf, size_t size)
{
	return e_platform.target_ops->ee_mread_buf(mbuf, from_addr, buf, size);
}


static ssize_t ee_mwrite_buf_native(e_mem_t *mbuf, off_t to_addr, const void *buf, size_t size)
{
	void *pto;

	if (((to_addr + size) > mbuf->map_size) || (to_addr < 0))
	{
		warnx("ee_mwrite_buf(): Address is out of bounds.");
		return E_ERR;
	}

	pto = mbuf->base + to_addr;

	if ( E_SHARED_MEM == mbuf->objtype ) {
		diag(H_D1) {
		  fprintf(diag_fd, "ee_mwrite_buf(): writing to to_addr=0x%08x, "
				  "offset=0x%08x, size=%d, map_size=0x%x\n", (uint) pto,
				  (uint)to_addr, (uint) size, (uint) mbuf->map_size);
		}
	}
	memcpy(pto, buf, size);

	return size;
}

// Write a block to an external memory buffer
ssize_t ee_mwrite_buf(e_mem_t *mbuf, off_t to_addr, const void *buf, size_t size)
{
	return e_platform.target_ops->ee_mwrite_buf(mbuf, to_addr, buf, size);
}



/////////////////////////
// Core control functions


int e_reset_system_native(void)
{
	int ret = E_OK;
	int memfd;

	// Open memory device
	memfd = open(EPIPHANY_DEV, O_RDWR | O_SYNC);
	if (memfd == -1)
	{
		warnx("e_reset_system(): EPIPHANY_DEV file open failure.");
		return E_ERR;
	}

	if (ioctl(memfd, E_IOCTL_RESET)) {
		warnx("e_reset_system(): EPIPHANY_DEV reset ioctl failure.");
		ret = E_ERR;
	}

	close(memfd);
	return ret;
}


// Reset the Epiphany platform
int e_reset_system(void)
{
	return e_platform.target_ops->e_reset_system();
}


// Reset the Epiphany chip
int e_reset_chip(void)
{
	diag(H_D1) { fprintf(diag_fd, "e_reset_chip(): This operation is not implemented!\n"); }

	return E_OK;
}

int ee_soft_reset_dma(e_epiphany_t *dev, unsigned row, unsigned col)
{
	uint32_t config;
	bool fail0, fail1;
	int i;

	/* pause DMA */
	config = ee_read_reg(dev, row, col, E_REG_CONFIG) | 0x01000000;
	ee_write_reg(dev, row, col, E_REG_CONFIG, config);

	ee_write_reg(dev, row, col, E_REG_DMA0CONFIG, 0);
	ee_write_reg(dev, row, col, E_REG_DMA0STRIDE, 0);
	ee_write_reg(dev, row, col, E_REG_DMA0COUNT, 0);
	ee_write_reg(dev, row, col, E_REG_DMA0SRCADDR, 0);
	ee_write_reg(dev, row, col, E_REG_DMA0DSTADDR, 0);
	ee_write_reg(dev, row, col, E_REG_DMA0STATUS, 0);
	ee_write_reg(dev, row, col, E_REG_DMA1CONFIG, 0);
	ee_write_reg(dev, row, col, E_REG_DMA1STRIDE, 0);
	ee_write_reg(dev, row, col, E_REG_DMA1COUNT, 0);
	ee_write_reg(dev, row, col, E_REG_DMA1SRCADDR, 0);
	ee_write_reg(dev, row, col, E_REG_DMA1DSTADDR, 0);
	ee_write_reg(dev, row, col, E_REG_DMA1STATUS, 0);

	/* unpause DMA */
	config &= ~0x01000000;
	ee_write_reg(dev, row, col, E_REG_CONFIG, config);

	fail0 = true;
	for (i = 0; i < 1000; i++) {
		if (!(ee_read_reg(dev, row, col, E_REG_DMA0STATUS) & 7)) {
			fail0 = false;
			break;
		}
		usleep(10);
	}
	if (fail0)
		warnx("%s(): (%d, %d) DMA0 NOT IDLE after dma reset", __func__, row, col);

	fail1 = true;
	for (i = 0; i < 1000; i++) {
		if (!(ee_read_reg(dev, row, col, E_REG_DMA1STATUS) & 7)) {
			fail1 = false;
			break;
		}
		usleep(10);
	}
	if (fail1)
		warnx("%s(): (%d, %d) DMA1 NOT IDLE after dma reset", __func__, row, col);

	return (fail0 || fail1) ? E_ERR : E_OK;
}

int ee_reset_regs(e_epiphany_t *dev, unsigned row, unsigned col, bool reset_dma)
{
	unsigned i;

	for (i = E_REG_R0; i <= E_REG_R63; i += 4)
		ee_write_reg(dev, row, col, i, 0);

	if (reset_dma)
		if (ee_soft_reset_dma(dev, row, col) != E_OK)
			return E_ERR;

	/* Enable clock gating */
	ee_write_reg(dev, row, col, E_REG_CONFIG, 0x00400000);
	ee_write_reg(dev, row, col, E_REG_FSTATUS, 0);
	ee_write_reg(dev, row, col, E_REG_PC, 0);
	ee_write_reg(dev, row, col, E_REG_LC, 0);
	ee_write_reg(dev, row, col, E_REG_LS, 0);
	ee_write_reg(dev, row, col, E_REG_LE, 0);
	ee_write_reg(dev, row, col, E_REG_IRET, 0);
	/* Mask all but SYNC irq */
	ee_write_reg(dev, row, col, E_REG_IMASK, ~(1 << E_SYNC));
	ee_write_reg(dev, row, col, E_REG_ILATCL, ~0);
	ee_write_reg(dev, row, col, E_REG_CTIMER0, 0);
	ee_write_reg(dev, row, col, E_REG_CTIMER1, 0);
	ee_write_reg(dev, row, col, E_REG_MEMSTATUS, 0);
	ee_write_reg(dev, row, col, E_REG_MEMPROTECT, 0);
	/* Enable clock gating */
	ee_write_reg(dev, row, col, E_REG_MESHCONFIG, 2);

	return E_OK;
}


uint8_t soft_reset_payload[] = {
	0xe8, 0x16, 0x00, 0x00, 0xe8, 0x14, 0x00, 0x00, 0xe8, 0x12, 0x00, 0x00,
	0xe8, 0x10, 0x00, 0x00, 0xe8, 0x0e, 0x00, 0x00, 0xe8, 0x0c, 0x00, 0x00,
	0xe8, 0x0a, 0x00, 0x00, 0xe8, 0x08, 0x00, 0x00, 0xe8, 0x06, 0x00, 0x00,
	0xe8, 0x04, 0x00, 0x00, 0xe8, 0x02, 0x00, 0x00, 0x1f, 0x15, 0x02, 0x04,
	0x7a, 0x00, 0x00, 0x03, 0xd2, 0x01, 0xe0, 0xfb, 0x92, 0x01, 0xb2, 0x01,
	0xe0, 0xfe
};

/*
 *        ivt:
 *   0:              b.l     clear_ipend
 *   4:              b.l     clear_ipend
 *   8:              b.l     clear_ipend
 *   c:              b.l     clear_ipend
 *  10:              b.l     clear_ipend
 *  14:              b.l     clear_ipend
 *  18:              b.l     clear_ipend
 *  1c:              b.l     clear_ipend
 *  20:              b.l     clear_ipend
 *  24:              b.l     clear_ipend
 *  28:              b.l     clear_ipend
 *        clear_ipend:
 *  2c:              movfs   r0, ipend
 *  30:              orr     r0, r0, r0
 *  32:              beq     1f
 *  34:              rti
 *  36:              b       clear_ipend
 *        1:
 *  38:              gie
 *  3a:              idle
 *  3c:              b       1b
 */

int ee_soft_reset_core(e_epiphany_t *dev, unsigned row, unsigned col)
{
	int i;
	bool fail;

	if (!(ee_read_reg(dev, row, col, E_REG_DEBUGSTATUS) & 1)) {
		diag(H_D1) { fprintf(diag_fd, "%s(): No clean previous exit\n", __func__); }
		e_halt(dev, row, col);
	}

	/* Wait for external fetch */
	fail = true;
	for (i = 0; i < 1000; i++) {
		if (!(ee_read_reg(dev, row, col, E_REG_DEBUGSTATUS) & 2)) {
			fail = false;
			break;
		}
		usleep(10);
	}
	if (fail) {
		warnx("%s(): (%d, %d) stuck. Full system reset needed", __func__, row, col);
		return E_ERR;
	}

	if (ee_read_reg(dev, row, col, E_REG_DMA0STATUS) & 7)
		warnx("%s(): (%d, %d) DMA0 NOT IDLE", __func__, row, col);

	if (ee_read_reg(dev, row, col, E_REG_DMA1STATUS) & 7)
		warnx("%s(): (%d, %d) DMA1 NOT IDLE", __func__, row, col);

	/* Abort DMA transfers */
	if (ee_soft_reset_dma(dev, row, col) != E_OK)
		return E_ERR;

	/* Disable timers */
	ee_write_reg(dev, row, col, E_REG_CONFIG, 0);

	ee_write_reg(dev, row, col, E_REG_ILATCL, ~0);

	ee_write_reg(dev, row, col, E_REG_IMASK, 0);

	ee_write_reg(dev, row, col, E_REG_IRET, 0x2c); /* clear_ipend */

	ee_write_reg(dev, row, col, E_REG_PC, 0x2c); /* clear_ipend */

	ee_write_buf(dev, row, col, 0, soft_reset_payload, sizeof(soft_reset_payload));

	/* Set active bit */
	ee_write_reg(dev, row, col, E_REG_FSTATUS, 1);

	e_resume(dev, row, col);

	fail = true;
	for (i = 0; i < 10000; i++) {
		if (!ee_read_reg(dev, row, col, E_REG_IPEND) &&
			!ee_read_reg(dev, row, col, E_REG_ILAT) &&
			!(ee_read_reg(dev, row, col, E_REG_STATUS) & 1)) {
			fail = false;
			break;
		}
		usleep(10);
	}
	if (fail) {
		warnx("%s: (%d, %d) Not idle", __func__, row, col);
		return E_ERR;
	}

	/* Reset regs, excluding DMA (already done above) */
	ee_reset_regs(dev, row, col, false);

	return E_OK;
}


// Reset a group
int ee_reset_group(e_epiphany_t *dev, unsigned row, unsigned col, unsigned rows,
		unsigned cols)
{
	int RESET0 = 0x0;
	int RESET1 = 0x1;
	int CONFIG = 0x01000000;
	int i, j;

	diag(H_D1) { fprintf(diag_fd, "ee_reset_group(): halting cores...\n"); }
	for (i = row; i < row + rows; i++)
		for (j = col; j < col + cols; j++)
			e_halt(dev, i, j);

	diag(H_D1) { fprintf(diag_fd, "ee_reset_group(): halting cores...\n"); }
	for (i = row; i < row + rows; i++) {
		for (j = col; j < col + cols; j++) {
			/* Refuse to reset if core is in the middle of an external read */
			if (ee_read_reg(dev, i, j, E_REG_DEBUGSTATUS) & 2) {
				usleep(100000);
				if (ee_read_reg(dev, i, j, E_REG_DEBUGSTATUS) & 2) {
					warnx("ee_reset_group(): (%d, %d) stuck. Full system reset needed", i, j);
					return E_ERR;
				}
			}
		}
	}

	diag(H_D1) { fprintf(diag_fd, "ee_reset_group(): pausing DMAs.\n"); }

	for (i = row; i < row + rows; i++)
		for (j = col; j < col + cols; j++)
			e_write(dev, i, j, E_REG_CONFIG, &CONFIG, sizeof(unsigned));

#if 0
	if (!ee_esim_target_p())
		usleep(100000);
#endif

	diag(H_D1) { fprintf(diag_fd, "ee_reset_group(): resetting cores...\n"); }
	for (i = row; i < row + rows; i++) {
		for (j = col; j < col + cols; j++) {
			ee_write_reg(dev, i, j, E_REG_RESETCORE, RESET1);
			ee_write_reg(dev, i, j, E_REG_RESETCORE, RESET0);
		}
	}

	diag(H_D1) { fprintf(diag_fd, "ee_reset_group(): done.\n"); }

	return E_OK;
}

// Reset an e-core
int ee_reset_core(e_epiphany_t *dev, unsigned row, unsigned col)
{
	return ee_reset_group(dev, row, col, 1, 1);
}


// Reset a workgroup
int e_reset_group(e_epiphany_t *dev)
{
	return ee_reset_group(dev, 0, 0, dev->rows, dev->cols);
}

static bool gdbserver_attached_p()
{
	static bool initialized = false;
	static bool gdbserver = false;
	const char *p;

	if (!initialized) {
		p = getenv("EHAL_GDBSERVER");
		gdbserver = (p && p[0] != '\0');
	}

	return gdbserver;
}

static int e_halt_group(e_epiphany_t *dev, unsigned row, unsigned col,
						unsigned rows, unsigned cols);

int _e_default_start_group(e_epiphany_t *dev,
						   unsigned row, unsigned col,
						   unsigned rows, unsigned cols)
{
	int				i, j;
	e_return_stat_t retval;
	int SYNC = (1 << E_SYNC);

	retval = E_OK;

	if (gdbserver_attached_p()) {
		diag(H_D1) { fprintf(diag_fd, "%s(): EHAL_GDBSERVER set. Setting DEBUGCMD haltbit\n", __func__); }
		e_halt_group(dev, row, col, rows, cols);
	}

	diag(H_D1) { fprintf(diag_fd, "%s(): SYNC (0x%x) to workgroup...\n", __func__, E_REG_ILATST); }
	for (i = row; i < row + rows; i++) {
		for (j = col; j < col + cols; j++) {
			if (ee_write_reg(dev, i, j, E_REG_ILATST, SYNC) == E_ERR)
				retval = E_ERR;
		}
	}
	diag(H_D1) { fprintf(diag_fd, "%s(): done.\n", __func__); }

	return retval;
}


// Start a program loaded on an e-core in a group
int e_start(e_epiphany_t *dev, unsigned row, unsigned col)
{
	return e_platform.target_ops->start_group(dev, row, col, 1, 1);
}


// Start all programs loaded on a workgroup
int e_start_group(e_epiphany_t *dev)
{
	return e_platform.target_ops->start_group(dev, 0, 0, dev->rows, dev->cols);
}


// Signal a software interrupt to an e-core in a group
int e_signal(e_epiphany_t *dev, unsigned row, unsigned col)
{
	int SWI = (1 << E_USER_INT);

	diag(H_D1) { fprintf(diag_fd, "e_signal(): SWI (0x%x) to core (%d,%d)...\n", E_REG_ILATST, row, col); }
	ee_write_reg(dev, row, col, E_REG_ILATST, SWI);
	diag(H_D1) { fprintf(diag_fd, "e_signal(): done.\n"); }

	return E_OK;
}


// Halt a core
int e_halt(e_epiphany_t *dev, unsigned row, unsigned col)
{
	int cmd;

	cmd = 0x1;
	e_write(dev, row, col, E_REG_DEBUGCMD, &cmd, sizeof(int));

	return E_OK;
}

static int e_halt_group(e_epiphany_t *dev, unsigned row, unsigned col,
						unsigned rows, unsigned cols)
{
	for (unsigned r = row; r < row + rows; r++)
		for (unsigned c = col; c < col + cols; c++)
			e_halt(dev, r, c);
}

// Resume a core after halt
int e_resume(e_epiphany_t *dev, unsigned row, unsigned col)
{
	int cmd;

	cmd = 0x0;
	e_write(dev, row, col, E_REG_DEBUGCMD, &cmd, sizeof(int));

	return E_OK;
}


////////////////////
// Utility functions

// Convert core coordinates to core-number within a group. No bounds check is performed.
unsigned e_get_num_from_coords(e_epiphany_t *dev, unsigned row, unsigned col)
{
	unsigned corenum;

	corenum = col + row * dev->cols;
	diag(H_D2) { fprintf(diag_fd, "e_get_num_from_coords(): dev.(row,col,rows,cols)=(%d,%d,%d,%d), (row,col)=(%d,%d), corenum=%d\n", dev->row, dev->col, dev->rows, dev->cols, row, col, corenum); }

	return corenum;
}


// Convert CoreID to core-number within a group.
unsigned ee_get_num_from_id(e_epiphany_t *dev, unsigned coreid)
{
	unsigned row, col, corenum;

	row = (coreid >> 6) & 0x3f;
	col = (coreid >> 0) & 0x3f;
	corenum = (col - dev->col) + (row - dev->row) * dev->cols;
	diag(H_D2) { fprintf(diag_fd, "ee_get_num_from_id(): CoreID=0x%03x, dev.(row,col,rows,cols)=(%d,%d,%d,%d), (row,col)=(%d,%d), corenum=%d\n", coreid, dev->row, dev->col, dev->rows, dev->cols, row, col, corenum); }

	return corenum;
}


// Converts core coordinates to CoreID.
unsigned ee_get_id_from_coords(e_epiphany_t *dev, unsigned row, unsigned col)
{
	unsigned coreid;

	coreid = (dev->col + col) + ((dev->row + row) << 6);
	diag(H_D2) { fprintf(diag_fd, "ee_get_id_from_coords(): dev.(row,col,rows,cols)=(%d,%d,%d,%d), (row,col)=(%d,%d), CoreID=0x%03x\n", dev->row, dev->col, dev->rows, dev->cols, row, col, coreid); }

	return coreid;
}


// Converts core-number within a group to CoreID.
unsigned ee_get_id_from_num(e_epiphany_t *dev, unsigned corenum)
{
	unsigned row, col, coreid;

	row = corenum / dev->cols;
	col = corenum % dev->cols;
	coreid = (dev->col + col) + ((dev->row + row) << 6);
	diag(H_D2) { fprintf(diag_fd, "ee_get_id_from_num(): corenum=%d, dev.(row,col,rows,cols)=(%d,%d,%d,%d), (row,col)=(%d,%d), CoreID=0x%03x\n", corenum, dev->row, dev->col, dev->rows, dev->cols, row, col, coreid); }

	return coreid;
}


// Converts CoreID to core coordinates.
void ee_get_coords_from_id(e_epiphany_t *dev, unsigned coreid, unsigned *row, unsigned *col)
{
	*row = ((coreid >> 6) & 0x3f) - dev->row;
	*col = ((coreid >> 0) & 0x3f) - dev->col;
	diag(H_D2) { fprintf(diag_fd, "ee_get_coords_from_id(): CoreID=0x%03x, dev.(row,col,rows,cols)=(%d,%d,%d,%d), (row,col)=(%d,%d)\n", coreid, dev->row, dev->col, dev->rows, dev->col, *row, *col); }

	return;
}

// Converts core-number within a group to core coordinates.
void e_get_coords_from_num(e_epiphany_t *dev, unsigned corenum, unsigned *row, unsigned *col)
{
	*row = corenum / dev->cols;
	*col = corenum % dev->cols;
	diag(H_D2) { fprintf(diag_fd, "e_get_coords_from_num(): corenum=%d, dev.(row,col,rows,cols)=(%d,%d,%d,%d), (row,col)=(%d,%d)\n", corenum, dev->row, dev->col, dev->col, dev->rows, *row, *col); }

	return;
}


// Check if an address is on a chip region
// Take uintptr_t or uint64_t instead of void.
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
			return E_TRUE;
	}

	return E_FALSE;
}

// Check if an address is in an external memory region
e_bool_t e_is_addr_in_emem(uintptr_t addr)
{
	unsigned  i;
	e_memseg_t  *mem;

	for (i = 0; i < e_platform.num_emems; i++) {
		mem = &e_platform.emem[i];
		if (mem->ephy_base <= addr && addr - mem->ephy_base <= mem->size)
			return E_TRUE;
	}
	return E_FALSE;
}


// Check if an address is on a core-group region
e_bool_t e_is_addr_on_group(e_epiphany_t *dev, void *addr)
{
	unsigned row, col;
	unsigned coreid;

	coreid = ((unsigned) addr) >> 20;
	ee_get_coords_from_id(dev, coreid, &row, &col);

	if ((row < dev->rows) && (col < dev->cols))
		return E_TRUE;

	return E_FALSE;
}

e_hal_diag_t e_set_host_verbosity(e_hal_diag_t verbose)
{
	e_hal_diag_t old_host_verbose;

	old_host_verbose = e_host_verbose;
	diag_fd = stderr;
	e_host_verbose = verbose;

	return old_host_verbose;
}



////////////////////////////////////
// HDF parser

int ee_parse_hdf(e_platform_t *dev, char *hdf)
{
	int	  ret = E_ERR;
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
int ee_parse_simple_hdf(e_platform_t *dev, char *hdf)
{
	FILE	   *fp;
	int			chip_num;
	int			emem_num;
	e_chip_t   *curr_chip = NULL;
	e_memseg_t *curr_emem = NULL;

	char line[255], etag[255], eval[255], *dummy;
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
		dummy = fgets(line, sizeof(line), fp);
		ee_trim_str(line);
		if (!strcmp(line, ""))
			continue;
		sscanf(line, "%s %s", etag, eval);
		diag(H_D3) { fprintf(diag_fd, "ee_parse_simple_hdf(): line %d: %s %s\n", l, etag, eval); }


		// Platform definition
		if		(!strcmp("PLATFORM_VERSION", etag))
		{
			sscanf(eval, "%s", dev->version);
			diag(H_D3) { fprintf(diag_fd, "ee_parse_simple_hdf(): platform version = %s\n", dev->version); }
		}

		else if (!strcmp("NUM_CHIPS", etag))
		{
			sscanf(eval, "%d", &(dev->num_chips));
			dev->chip = (e_chip_t *) calloc(dev->num_chips, sizeof(e_chip_t));
			diag(H_D3) { fprintf(diag_fd, "ee_parse_simple_hdf(): number of chips = %d\n", dev->num_chips); }
		}

		else if (!strcmp("NUM_EXT_MEMS", etag))
		{
			sscanf(eval, "%d", &(dev->num_emems));
			dev->emem = (e_memseg_t *) calloc(dev->num_emems, sizeof(e_memseg_t));
			diag(H_D3) { fprintf(diag_fd, "ee_parse_simple_hdf(): number of ext. memory segments = %d\n", dev->num_emems); }
		}

		else if (!strcmp("ESYS_REGS_BASE", etag)) {
			diag(H_D3) { fprintf(diag_fd, "Ignoring deprecated ESYS_REGS_BASE\n"); }
		}


		// Chip definition
		else if (!strcmp("CHIP", etag))
		{
			chip_num++;
			curr_chip = &(dev->chip[chip_num]);
			sscanf(eval, "%s", curr_chip->version);
			diag(H_D3) { fprintf(diag_fd, "ee_parse_simple_hdf(): processing chip #%d, version = \"%s\"\n", chip_num, curr_chip->version); }
			diag(H_D3) { fprintf(diag_fd, "ee_parse_simple_hdf(): chip version = %s\n", curr_chip->version); }
		}

		else if (!strcmp("CHIP_ROW", etag))
		{
			sscanf(eval, "%d", &(curr_chip->row));
			diag(H_D3) { fprintf(diag_fd, "ee_parse_simple_hdf(): chip row = %d\n", curr_chip->row); }
		}

		else if (!strcmp("CHIP_COL", etag))
		{
			sscanf(eval, "%d", &(curr_chip->col));
			diag(H_D3) { fprintf(diag_fd, "ee_parse_simple_hdf(): chip col = %d\n", curr_chip->col); }
		}


		// External memory definitions
		else if (!strcmp("EMEM", etag))
		{
			emem_num++;
			curr_emem = &(dev->emem[emem_num]);
			diag(H_D3) { fprintf(diag_fd, "ee_parse_simple_hdf(): processing external memory segment #%d\n", emem_num); }
		}

		else if (!strcmp("EMEM_BASE_ADDRESS", etag))
		{
			sscanf(eval, "%x", (unsigned int *) &(curr_emem->phy_base));
			diag(H_D3) { fprintf(diag_fd, "ee_parse_simple_hdf(): base addr. of ext. mem. segment = 0x%08x\n", (uint) curr_emem->phy_base); }
		}

		else if (!strcmp("EMEM_EPI_BASE", etag))
		{
			sscanf(eval, "%x", (unsigned int *) &(curr_emem->ephy_base));
			diag(H_D3) { fprintf(diag_fd, "ee_parse_simple_hdf(): base addr. of ext. mem. segment (device side)= 0x%08x\n", (uint) curr_emem->ephy_base); }
		}

		else if (!strcmp("EMEM_SIZE", etag))
		{
			sscanf(eval, "%x", (unsigned int *) &(curr_emem->size));
			diag(H_D3) { fprintf(diag_fd, "ee_parse_simple_hdf(): size of ext. mem. segment = %x\n", (uint) curr_emem->size); }
		}

		else if (!strcmp("EMEM_TYPE", etag))
		{
			if (!strcmp(etag, "RD"))
				curr_emem->type = E_RD;
			else if (!strcmp(etag, "WR"))
				curr_emem->type = E_WR;
			else if (!strcmp(etag, "RDWR"))
				curr_emem->type = E_RDWR;
			diag(H_D3) { fprintf(diag_fd, "ee_parse_simple_hdf(): type of ext. mem. segment = %x\n", (uint) curr_emem->type); }
		}


		// Other
		else if (!strcmp("//", etag))
		{
			;
			diag(H_D3) { fprintf(diag_fd, "ee_parse_simple_hdf(): comment\n"); }
		}
		else {
			return E_ERR;
		}
	}

	fclose(fp);

	return E_OK;
}
#pragma GCC diagnostic pop

int ee_parse_xml_hdf(e_platform_t *dev, char *hdf)
{
	(void)dev;
	(void)hdf;
	warnx("ee_parse_xml_hdf(): XML file format is not yet supported. "
		  "Please use simple HDF format.");

	return E_ERR;
}


// Platform data structures
typedef struct {
	e_objtype_t		 objtype;	  // object type identifier
	e_platformtype_t type;		  // Epiphany platform part number
	char			 version[32]; // version name of Epiphany chip
} e_platform_db_t;

#define NUM_PLATFORM_VERSIONS 8
e_platform_db_t platform_params_table[NUM_PLATFORM_VERSIONS] = {
//		 objtype		 type			   version
		{E_EPI_PLATFORM, E_GENERIC,		   "GENERIC"},
		{E_EPI_PLATFORM, E_EMEK301,		   "EMEK301"},
		{E_EPI_PLATFORM, E_EMEK401,		   "EMEK401"},
		{E_EPI_PLATFORM, E_ZEDBOARD1601,   "ZEDBOARD1601"},
		{E_EPI_PLATFORM, E_ZEDBOARD6401,   "ZEDBOARD6401"},
		{E_EPI_PLATFORM, E_PARALLELLA1601, "PARALLELLA1601"},
		{E_EPI_PLATFORM, E_PARALLELLA6401, "PARALLELLA6401"},
		{E_EPI_PLATFORM, E_PARALLELLASIM,  "PARALLELLASIM"},
};


int ee_set_platform_params(e_platform_t *platform)
{
	int platform_ver;

	for (platform_ver = 0; platform_ver < NUM_PLATFORM_VERSIONS; platform_ver++)
		if (!strcmp(platform->version, platform_params_table[platform_ver].version))
		{
			diag(H_D2) { fprintf(diag_fd, "ee_set_platform_params(): found platform version \"%s\"\n", platform->version); }
			break;
		}

	if (platform_ver == NUM_PLATFORM_VERSIONS)
	{
		diag(H_D2) { fprintf(diag_fd, "ee_set_platform_params(): platform version \"%s\" not found, setting to \"%s\" type\n", platform->version, platform_params_table[0].version); }
		platform_ver = 0;
	}

	platform->type = platform_params_table[platform_ver].type;

	return E_OK;
}

e_chip_db_t e_chip_params_table[E_CHIP_DB_NUM_CHIP_VERSIONS] = {
//		 objtype	 type		version	 arch r	 c sram_base sram_size regs_base regs_size io_n		io_e		io_s		io_w
		{E_EPI_CHIP, E_E16G301, "E16G301", 3, 4, 4, 0x00000, 0x08000, 0xf0000, 0x01000, 0x002f0000, 0x083f0000, 0x0c2f0000, 0x080f0000},
		{E_EPI_CHIP, E_E64G401, "E64G401", 4, 8, 8, 0x00000, 0x08000, 0xf0000, 0x01000, 0x002f0000, 0x087f0000, 0x1c2f0000, 0x080f0000},
		{E_EPI_CHIP, E_ESIM,    "ESIM",    0, 4, 4, 0x00000, 0x08000, 0xf0000, 0x01000, 0x002f0000, 0x087f0000, 0x1c2f0000, 0x080f0000},
};


int ee_set_chip_params(e_chip_t *chip)
{
	int chip_ver;

	for (chip_ver = 0; chip_ver < E_CHIP_DB_NUM_CHIP_VERSIONS; chip_ver++)
		if (!strcmp(chip->version, e_chip_params_table[chip_ver].version))
		{
			diag(H_D2) { fprintf(diag_fd, "ee_set_chip_params(): found chip version \"%s\"\n", chip->version); }
			break;
		}

	if (chip_ver == E_CHIP_DB_NUM_CHIP_VERSIONS)
	{
		diag(H_D2) { fprintf(diag_fd, "ee_set_chip_params(): chip version \"%s\" not found, setting to \"%s\"\n", chip->version, e_chip_params_table[0].version); }
		chip_ver = 0;
	}

	chip->type		= e_chip_params_table[chip_ver].type;
	chip->arch		= e_chip_params_table[chip_ver].arch;
	chip->rows		= e_chip_params_table[chip_ver].rows;
	chip->cols		= e_chip_params_table[chip_ver].cols;
	chip->num_cores = chip->rows * chip->cols;
	chip->sram_base = e_chip_params_table[chip_ver].sram_base;
	chip->sram_size = e_chip_params_table[chip_ver].sram_size;
	chip->regs_base = e_chip_params_table[chip_ver].regs_base;
	chip->regs_size = e_chip_params_table[chip_ver].regs_size;
	chip->ioregs_n	= e_chip_params_table[chip_ver].ioregs_n;
	chip->ioregs_e	= e_chip_params_table[chip_ver].ioregs_e;
	chip->ioregs_s	= e_chip_params_table[chip_ver].ioregs_s;
	chip->ioregs_w	= e_chip_params_table[chip_ver].ioregs_w;

	return E_OK;
}

#if ESIM_TARGET
#endif


void ee_trim_str(char *a)
{
	char *b = a;
	while (isspace(*b))	  ++b;
	while (*b)			  *a++ = *b++;
	*a = '\0';
	while (isspace(*--a)) *a = '\0';
}


unsigned long ee_rndu_page(unsigned long size)
{
	unsigned long page_size;
	unsigned long rsize;

	// Get OS memory page size
	page_size = sysconf(_SC_PAGE_SIZE);

	// Find upper integral number of pages
	rsize = ((size + (page_size - 1)) / page_size) * page_size;

	return rsize;
}


unsigned long ee_rndl_page(unsigned long size)
{
	unsigned long page_size;
	unsigned long rsize;

	// Get OS memory page size
	page_size = sysconf(_SC_PAGE_SIZE);

	// Find lower integral number of pages
	rsize = (size / page_size) * page_size;

	return rsize;
}


// Target code
bool ee_native_target_p()
{
	static bool initialized = false;
	static bool native = false;

	if (!initialized)
		native = (!ee_esim_target_p() && !ee_pal_target_p());

	return native;
}

bool ee_esim_target_p()
{
	static bool initialized = false;
	static bool esim = false;
	const char *p;

	if (!initialized) {
		p = getenv(EHAL_TARGET_ENV);
		esim = (p && strncmp(p, "sim", sizeof("sim")) == 0) ||
			   (p && strncmp(p, "esim", sizeof("esim")) == 0);
        initialized = true;
	}

	return esim;
}

bool ee_pal_target_p()
{
	static bool initialized = false;
	static bool pal = false;
	const char *p;

	if (!initialized) {
		p = getenv(EHAL_TARGET_ENV);
		pal = (p && strncmp(p, "pal", sizeof("pal")) == 0);
        initialized = true;
	}

	return pal;
}


static int populate_platform_native(e_platform_t *platform, char *hdf)
{
	char *hdf_env, *esdk_env, hdf_dfl[1024];

	// Parse HDF, get platform configuration
	if (hdf == NULL)
	{
		// Try getting HDF from EPIPHANY_HDF environment variable
		hdf_env = getenv(hdf_env_var_name);
		diag(H_D2) { fprintf(diag_fd, "e_init(): HDF ENV = %s\n", hdf_env); }
		if (hdf_env != NULL)
			hdf = hdf_env;
		else
		{
			// Try opening .../bsps/current/platform.hdf
			warnx("e_init(): No Hardware Definition File (HDF) is specified. Trying \"platform.hdf\".");
			esdk_env = getenv(esdk_path);
			strncpy(hdf_dfl, esdk_env, sizeof(hdf_dfl));
			strncat(hdf_dfl, "/bsps/current/platform.hdf", sizeof(hdf_dfl));
			hdf = hdf_dfl;
		}
	}

	diag(H_D2) { fprintf(diag_fd, "e_init(): opening HDF %s\n", hdf); }
	if (ee_parse_hdf(&e_platform, hdf))
	{
		warnx("e_init(): Error parsing Hardware Definition File (HDF).");
		return E_ERR;
	}
}

static int init_native()
{
	return E_OK;
}

static void finalize_native()
{
}

extern int _e_default_load_group(const char *executable, e_epiphany_t *dev,
								 unsigned row, unsigned col,
								 unsigned rows, unsigned cols);
/* Native target ops */
const struct e_target_ops native_target_ops = {
	.alloc = alloc_native,
	.shm_alloc = shm_alloc_native,
	.free = free_native,
	.ee_read_word = ee_read_word_native,
	.ee_write_word = ee_write_word_native,
	.ee_read_buf = ee_read_buf_native,
	.ee_write_buf = ee_write_buf_native,
	.ee_read_reg = ee_read_reg_native,
	.ee_write_reg = ee_write_reg_native,
	.ee_mread_word = ee_mread_word_native,
	.ee_mwrite_word = ee_mwrite_word_native,
	.ee_mread_buf = ee_mread_buf_native,
	.ee_mwrite_buf = ee_mwrite_buf_native,
	.e_reset_system = e_reset_system_native,
	.populate_platform = populate_platform_native,
	.init = init_native,
	.finalize = finalize_native,
	.open = ee_open_native,
	.load_group = _e_default_load_group,
	.start_group = _e_default_start_group,
};

#pragma GCC diagnostic pop
