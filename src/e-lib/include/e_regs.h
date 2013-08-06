/*
  File: e_regs.h

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

#ifndef E_REGS_H_
#define E_REGS_H_

#include "e_types.h"


// eCore General Purpose Registers
typedef enum {
	E_CORE_GP_REG_BASE     = 0xf0000,
	E_REG_R0               = E_CORE_GP_REG_BASE + 0x0000,
	E_REG_R1               = E_CORE_GP_REG_BASE + 0x0004,
	E_REG_R2               = E_CORE_GP_REG_BASE + 0x0008,
	E_REG_R3               = E_CORE_GP_REG_BASE + 0x000c,
	E_REG_R4               = E_CORE_GP_REG_BASE + 0x0010,
	E_REG_R5               = E_CORE_GP_REG_BASE + 0x0014,
	E_REG_R6               = E_CORE_GP_REG_BASE + 0x0018,
	E_REG_R7               = E_CORE_GP_REG_BASE + 0x001c,
	E_REG_R8               = E_CORE_GP_REG_BASE + 0x0020,
	E_REG_R9               = E_CORE_GP_REG_BASE + 0x0024,
	E_REG_R10              = E_CORE_GP_REG_BASE + 0x0028,
	E_REG_R11              = E_CORE_GP_REG_BASE + 0x002c,
	E_REG_R12              = E_CORE_GP_REG_BASE + 0x0030,
	E_REG_R13              = E_CORE_GP_REG_BASE + 0x0034,
	E_REG_R14              = E_CORE_GP_REG_BASE + 0x0038,
	E_REG_R15              = E_CORE_GP_REG_BASE + 0x003c,
	E_REG_R16              = E_CORE_GP_REG_BASE + 0x0040,
	E_REG_R17              = E_CORE_GP_REG_BASE + 0x0044,
	E_REG_R18              = E_CORE_GP_REG_BASE + 0x0048,
	E_REG_R19              = E_CORE_GP_REG_BASE + 0x004c,
	E_REG_R20              = E_CORE_GP_REG_BASE + 0x0050,
	E_REG_R21              = E_CORE_GP_REG_BASE + 0x0054,
	E_REG_R22              = E_CORE_GP_REG_BASE + 0x0058,
	E_REG_R23              = E_CORE_GP_REG_BASE + 0x005c,
	E_REG_R24              = E_CORE_GP_REG_BASE + 0x0060,
	E_REG_R25              = E_CORE_GP_REG_BASE + 0x0064,
	E_REG_R26              = E_CORE_GP_REG_BASE + 0x0068,
	E_REG_R27              = E_CORE_GP_REG_BASE + 0x006c,
	E_REG_R28              = E_CORE_GP_REG_BASE + 0x0070,
	E_REG_R29              = E_CORE_GP_REG_BASE + 0x0074,
	E_REG_R30              = E_CORE_GP_REG_BASE + 0x0078,
	E_REG_R31              = E_CORE_GP_REG_BASE + 0x007c,
	E_REG_R32              = E_CORE_GP_REG_BASE + 0x0080,
	E_REG_R33              = E_CORE_GP_REG_BASE + 0x0084,
	E_REG_R34              = E_CORE_GP_REG_BASE + 0x0088,
	E_REG_R35              = E_CORE_GP_REG_BASE + 0x008c,
	E_REG_R36              = E_CORE_GP_REG_BASE + 0x0090,
	E_REG_R37              = E_CORE_GP_REG_BASE + 0x0094,
	E_REG_R38              = E_CORE_GP_REG_BASE + 0x0098,
	E_REG_R39              = E_CORE_GP_REG_BASE + 0x009c,
	E_REG_R40              = E_CORE_GP_REG_BASE + 0x00a0,
	E_REG_R41              = E_CORE_GP_REG_BASE + 0x00a4,
	E_REG_R42              = E_CORE_GP_REG_BASE + 0x00a8,
	E_REG_R43              = E_CORE_GP_REG_BASE + 0x00ac,
	E_REG_R44              = E_CORE_GP_REG_BASE + 0x00b0,
	E_REG_R45              = E_CORE_GP_REG_BASE + 0x00b4,
	E_REG_R46              = E_CORE_GP_REG_BASE + 0x00b8,
	E_REG_R47              = E_CORE_GP_REG_BASE + 0x00bc,
	E_REG_R48              = E_CORE_GP_REG_BASE + 0x00c0,
	E_REG_R49              = E_CORE_GP_REG_BASE + 0x00c4,
	E_REG_R50              = E_CORE_GP_REG_BASE + 0x00c8,
	E_REG_R51              = E_CORE_GP_REG_BASE + 0x00cc,
	E_REG_R52              = E_CORE_GP_REG_BASE + 0x00d0,
	E_REG_R53              = E_CORE_GP_REG_BASE + 0x00d4,
	E_REG_R54              = E_CORE_GP_REG_BASE + 0x00d8,
	E_REG_R55              = E_CORE_GP_REG_BASE + 0x00dc,
	E_REG_R56              = E_CORE_GP_REG_BASE + 0x00e0,
	E_REG_R57              = E_CORE_GP_REG_BASE + 0x00e4,
	E_REG_R58              = E_CORE_GP_REG_BASE + 0x00e8,
	E_REG_R59              = E_CORE_GP_REG_BASE + 0x00ec,
	E_REG_R60              = E_CORE_GP_REG_BASE + 0x00f0,
	E_REG_R61              = E_CORE_GP_REG_BASE + 0x00f4,
	E_REG_R62              = E_CORE_GP_REG_BASE + 0x00f8,
	E_REG_R63              = E_CORE_GP_REG_BASE + 0x00fc,
} e_gp_reg_id_t;

// eCore Special Registers
typedef enum {
	E_CORE_SP_REG_BASE     = 0xf0000,
	// Control registers
	E_REG_CONFIG           = E_CORE_SP_REG_BASE + 0x0400,
	E_REG_STATUS           = E_CORE_SP_REG_BASE + 0x0404,
	E_REG_PC               = E_CORE_SP_REG_BASE + 0x0408,
	E_REG_DEBUGSTATUS      = E_CORE_SP_REG_BASE + 0x040c,
	E_REG_LC               = E_CORE_SP_REG_BASE + 0x0414,
	E_REG_LS               = E_CORE_SP_REG_BASE + 0x0418,
	E_REG_LE               = E_CORE_SP_REG_BASE + 0x041c,
	E_REG_IRET             = E_CORE_SP_REG_BASE + 0x0420,
	E_REG_IMASK            = E_CORE_SP_REG_BASE + 0x0424,
	E_REG_ILAT             = E_CORE_SP_REG_BASE + 0x0428,
	E_REG_ILATST           = E_CORE_SP_REG_BASE + 0x042C,
	E_REG_ILATCL           = E_CORE_SP_REG_BASE + 0x0430,
	E_REG_IPEND            = E_CORE_SP_REG_BASE + 0x0434,
	E_REG_CTIMER0          = E_CORE_SP_REG_BASE + 0x0438,
	E_REG_CTIMER1          = E_CORE_SP_REG_BASE + 0x043C,
	E_REG_FSTATUS          = E_CORE_SP_REG_BASE + 0x0440,
	E_REG_DEBUGCMD         = E_CORE_SP_REG_BASE + 0x0448,

	// DMA Registers
	E_REG_DMA0CONFIG       = E_CORE_SP_REG_BASE + 0x0500,
	E_REG_DMA0STRIDE       = E_CORE_SP_REG_BASE + 0x0504,
	E_REG_DMA0COUNT        = E_CORE_SP_REG_BASE + 0x0508,
	E_REG_DMA0SRCADDR      = E_CORE_SP_REG_BASE + 0x050C,
	E_REG_DMA0DSTADDR      = E_CORE_SP_REG_BASE + 0x0510,
	E_REG_DMA0AUTODMA0     = E_CORE_SP_REG_BASE + 0x0514,
	E_REG_DMA0AUTODMA1     = E_CORE_SP_REG_BASE + 0x0518,
	E_REG_DMA0STATUS       = E_CORE_SP_REG_BASE + 0x051C,
	E_REG_DMA1CONFIG       = E_CORE_SP_REG_BASE + 0x0520,
	E_REG_DMA1STRIDE       = E_CORE_SP_REG_BASE + 0x0524,
	E_REG_DMA1COUNT        = E_CORE_SP_REG_BASE + 0x0528,
	E_REG_DMA1SRCADDR      = E_CORE_SP_REG_BASE + 0x052C,
	E_REG_DMA1DSTADDR      = E_CORE_SP_REG_BASE + 0x0530,
	E_REG_DMA1AUTODMA0     = E_CORE_SP_REG_BASE + 0x0534,
	E_REG_DMA1AUTODMA1     = E_CORE_SP_REG_BASE + 0x0538,
	E_REG_DMA1STATUS       = E_CORE_SP_REG_BASE + 0x053C,

	// Memory Protection Registers
	E_REG_MEMPROTECT       = E_CORE_SP_REG_BASE + 0x0608,

	// Node Registers
	E_REG_MESHCFG          = E_CORE_SP_REG_BASE + 0x0700,
	E_REG_COREID           = E_CORE_SP_REG_BASE + 0x0704,
	E_REG_CORE_RESET       = E_CORE_SP_REG_BASE + 0x070c,
} e_core_reg_id_t;

// Chip registers
typedef enum {
	E_CHIP_REG_BASE        = 0xf0000,
	E_REG_IO_LINK_MODE_CFG = E_CHIP_REG_BASE + 0x0300,
	E_REG_IO_LINK_TX_CFG   = E_CHIP_REG_BASE + 0x0304,
	E_REG_IO_LINK_RX_CFG   = E_CHIP_REG_BASE + 0x0308,
	E_REG_IO_GPIO_CFG      = E_CHIP_REG_BASE + 0x030c,
	E_REG_IO_FLAG_CFG      = E_CHIP_REG_BASE + 0x0318,
	E_REG_IO_SYNC_CFG      = E_CHIP_REG_BASE + 0x031c,
	E_REG_IO_HALT_CFG      = E_CHIP_REG_BASE + 0x0320,
	E_REG_IO_RESET         = E_CHIP_REG_BASE + 0x0324,
	E_REG_IO_LINK_DEBUG    = E_CHIP_REG_BASE + 0x0328,
} e_chip_reg_id_t;


unsigned e_reg_read(e_core_reg_id_t reg_id);
void e_reg_write(e_core_reg_id_t reg_id, unsigned val);

#endif /* E_REGS_H_ */
