/*
  File: epiphany-hal-data-local.h

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

#ifndef __E_HAL_DATA_LOC_H__
#define __E_HAL_DATA_LOC_H__


#ifdef __cplusplus
extern "C"
{
#else
#endif


typedef enum {
	E_SYNC   = 0,
	E_SW_INT = 9,
} e_signal_t;


typedef enum {
	E_RD   = 1,
	E_WR   = 2,
	E_RDWR = 3,
} e_memtype_t;


// Core Regions
//#define LOC_BASE_MEMS       0x00000
//#define LOC_BASE_REGS       0xf0000
//#define MAP_SIZE_REGS       0x01000


typedef enum {
	E_NULL         = 0,
	E_EPI_PLATFORM    ,
	E_EPI_CHIP        ,
	E_EPI_GROUP       ,
	E_EPI_CORE        ,
	E_EXT_MEM         ,
	E_MAPPING         ,
} e_objytpe_t;


typedef enum {
	E_E16G301 = 0,
	E_E64G401 = 1,
} e_chiptype_t;


typedef struct {
	e_objytpe_t      objtype;     // object type identifier
	off_t            phy_base;    // physical global base address of memory region
	size_t           map_size;    // size of mapped region
	off_t            map_mask;    // for mmap
	void            *mapped_base; // for mmap
	void            *base;        // application space base address of memory region
} e_mmap_t;


typedef struct {
	e_objytpe_t      objtype;     // object type identifier
	unsigned int     id;          // core ID
	unsigned int     row;         // core absolute row number
	unsigned int     col;         // core absolute col number
	e_mmap_t         mems;        // core's SRAM data structure
	e_mmap_t         regs;        // core's e-regs data structure
} e_core_t;


// Platform data structures
typedef struct {
	e_objytpe_t      objtype;     // object type identifier
	e_chiptype_t     type;        // Epiphany chip part number
	char             version[32]; // version number of Epiphany chip
	unsigned int     arch;        // architecture generation
	unsigned int     base_coreid; // chip base core ID
	unsigned int     row;         // chip absolute row number
	unsigned int     col;         // chip absolute col number
	unsigned int     rows;        // number of rows in chip
	unsigned int     cols;        // number of cols in chip
	unsigned int     num_cores;   // number of cores in chip
	unsigned int     sram_base;   // base offset of core SRAM
	unsigned int     sram_size;   // size of core SRAM
	unsigned int     regs_base;   // base offset of core registers
	unsigned int     regs_size;   // size of core registers segment
	off_t            ioregs_n;    // base address of north IO register
	off_t            ioregs_e;    // base address of east IO register
	off_t            ioregs_s;    // base address of south IO register
	off_t            ioregs_w;    // base address of west IO register
} e_chip_t;


typedef struct {
	e_objytpe_t      objtype;     // object type identifier
	off_t            phy_base;    // physical global base address of external memory segment as seen by host side
	off_t            ephy_base;   // physical global base address of external memory segment as seen by device side
	size_t           size;        // size of eDRAM allocated buffer for host side
	e_memtype_t      type;        // type of memory RD/WR/RW
} e_memseg_t;


typedef struct {
	e_objytpe_t      objtype;     // object type identifier
	char             version[32]; // version number of this structure
	int              initialized; // platform initialized?

	unsigned int     regs_base;   // base address of platform registers

	int              num_chips;   // number of Epiphany chips in platform
	e_chip_t        *chip;        // array of Epiphany chip objects
	unsigned int     row;         // platform absolute minimum row number
	unsigned int     col;         // platform absolute minimum col number
	unsigned int     rows;        // number of rows in platform
	unsigned int     cols;        // number of cols in platform

	int              num_emems;   // number of external memory segments in platform
	e_memseg_t      *emem;        // array of external memory segments
} e_platform_t;


#ifdef __cplusplus
}
#endif

#endif // __E_HAL_DATA_LOC_H__
