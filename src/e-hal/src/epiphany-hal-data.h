/*
  File: epiphany-hal-data.h

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

#ifndef __E_HAL_DATA_H__
#define __E_HAL_DATA_H__

#include "epiphany-hal-data-local.h"

#ifdef __cplusplus
extern "C"
{
typedef enum {
	e_false = false,
	e_true  = true,
} e_bool_t;
#else
typedef enum {
	e_false = 0,
	e_true  = 1,
} e_bool_t;
#endif


typedef enum {
	H_D0 = 0,
	H_D1 = 1,
	H_D2 = 2,
	H_D3 = 3,
	H_D4 = 4,
} e_hal_diag_t;


typedef enum {
	E_OK   =  0,
	E_ERR  = -1,
	E_WARN = -2,
} e_return_stat_t;


// eCore registers
typedef enum {
	E_CORE_REG_BASE = 0xf0000,
	E_CONFIG        = E_CORE_REG_BASE + 0x0400,
	E_STATUS        = E_CORE_REG_BASE + 0x0404,
	E_PC            = E_CORE_REG_BASE + 0x0408,
	E_IRET          = E_CORE_REG_BASE + 0x0420,
	E_IMASK         = E_CORE_REG_BASE + 0x0424,
	E_ILAT          = E_CORE_REG_BASE + 0x0428,
	E_ILATST        = E_CORE_REG_BASE + 0x042C,
	E_ILATCL        = E_CORE_REG_BASE + 0x0430,
	E_IPEND         = E_CORE_REG_BASE + 0x0434,
	E_CTIMER0       = E_CORE_REG_BASE + 0x0438,
	E_CTIMER1       = E_CORE_REG_BASE + 0x043C,
	E_FSTATUS       = E_CORE_REG_BASE + 0x0440,
	E_HWSTATUS      = E_CORE_REG_BASE + 0x0444,
	E_DEBUGCMD      = E_CORE_REG_BASE + 0x0448,
	E_DMA0CONFIG    = E_CORE_REG_BASE + 0x0500,
	E_DMA0STRIDE    = E_CORE_REG_BASE + 0x0504,
	E_DMA0COUNT     = E_CORE_REG_BASE + 0x0508,
	E_DMA0SRCADDR   = E_CORE_REG_BASE + 0x050C,
	E_DMA0DSTADDR   = E_CORE_REG_BASE + 0x0510,
	E_DMA0AUTODMA0  = E_CORE_REG_BASE + 0x0514,
	E_DMA0AUTODMA1  = E_CORE_REG_BASE + 0x0518,
	E_DMA0STATUS    = E_CORE_REG_BASE + 0x051C,
	E_DMA1CONFIG    = E_CORE_REG_BASE + 0x0520,
	E_DMA1STRIDE    = E_CORE_REG_BASE + 0x0524,
	E_DMA1COUNT     = E_CORE_REG_BASE + 0x0528,
	E_DMA1SRCADDR   = E_CORE_REG_BASE + 0x052C,
	E_DMA1DSTADDR   = E_CORE_REG_BASE + 0x0530,
	E_DMA1AUTODMA0  = E_CORE_REG_BASE + 0x0534,
	E_DMA1AUTODMA1  = E_CORE_REG_BASE + 0x0538,
	E_DMA1STATUS    = E_CORE_REG_BASE + 0x053C,
	E_COREID        = E_CORE_REG_BASE + 0x0704,
	E_CORE_RESET    = E_CORE_REG_BASE + 0x070c,
} e_core_regs_t;


// Chip registers
typedef enum {
	E_CHIP_REG_BASE = 0xf0000,
	E_IO_LINK_CFG   = E_CHIP_REG_BASE + 0x0300,
	E_IO_TX_CFG     = E_CHIP_REG_BASE + 0x0304,
	E_IO_RX_CFG     = E_CHIP_REG_BASE + 0x0308,
	E_IO_FLAG_CFG   = E_CHIP_REG_BASE + 0x030c,
	E_IO_RESET      = E_CHIP_REG_BASE + 0x0324,
} e_chip_regs_t;


// Epiphany system registers
typedef enum {
	E_SYS_REG_BASE  = 0x00000000,
	E_SYS_CONFIG    = E_SYS_REG_BASE + 0x0000,
	E_SYS_RESET     = E_SYS_REG_BASE + 0x0004,
	E_SYS_VERSION   = E_SYS_REG_BASE + 0x0008,
	E_SYS_FILTERL   = E_SYS_REG_BASE + 0x000c,
	E_SYS_FILTERH   = E_SYS_REG_BASE + 0x0010,
	E_SYS_FILTERC   = E_SYS_REG_BASE + 0x0014,
} e_sys_regs_t;


// Core group data structures
typedef struct {
	e_objytpe_t      objtype;     // object type identifier
	e_chiptype_t     type;        // Epiphany chip part number
	unsigned int     num_cores;   // number of cores group
	unsigned int     base_coreid; // group base core ID
	unsigned int     row;         // group absolute row number
	unsigned int     col;         // group absolute col number
	unsigned int     rows;        // number of rows group
	unsigned int     cols;        // number of cols group
	e_core_t       **core;        // e-cores data structures array
	int              memfd;       // for mmap
} e_epiphany_t;


typedef struct {
	e_objytpe_t      objtype;     // object type identifier
	off_t            phy_base;    // physical global base address of external memory buffer as seen by host side
	size_t           map_size;    // size of eDRAM allocated buffer for host side
	off_t            ephy_base;   // physical global base address of external memory buffer as seen by device side
	size_t           emap_size;   // size of eDRAM allocated buffer for device side
	off_t            map_mask;    // for mmap
	void            *mapped_base; // for mmap
	void            *base;        // application space base address of external memory buffer
	int              memfd;       // for mmap
} e_mem_t;


#ifdef __cplusplus
}
#endif

#endif // __E_HAL_DATA_H__
