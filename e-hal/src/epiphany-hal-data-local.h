/*
  File: epiphany-hal-data-local.h

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
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  and the GNU Lesser General Public License along with this program,
  see the files COPYING and COPYING.LESSER.	 If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef __E_HAL_DATA_LOC_H__
#define __E_HAL_DATA_LOC_H__

#include "epiphany-hal-data.h"

#ifdef __cplusplus
extern "C"
{
#else
#endif


typedef enum {
	E_SYNC	   = 0,
	E_USER_INT = 9,
} e_signal_t;


typedef enum {
	E_RD   = 1,
	E_WR   = 2,
	E_RDWR = 3,
} e_memtype_t;


typedef enum {
	E_NULL		   = 0,
	E_EPI_PLATFORM = 1,
	E_EPI_CHIP	   = 2,
	E_EPI_GROUP	   = 3,
	E_EPI_CORE	   = 4,
	E_EXT_MEM	   = 5,
	E_MAPPING	   = 6,
	E_SHARED_MEM   = 7
} e_objtype_t;


typedef enum {
	E_E16G301 = 0,
	E_E64G401 = 1,
	E_ESIM    = 2,
} e_chiptype_t;


typedef enum {
	E_GENERIC		 = 0,
	E_EMEK301		 = 1,
	E_EMEK401		 = 2,
	E_ZEDBOARD1601	 = 3,
	E_ZEDBOARD6401	 = 4,
	E_PARALLELLA1601 = 5,
	E_PARALLELLA6401 = 6,
	E_PARALLELLASIM  = 7,
} e_platformtype_t;


typedef struct {
	e_objtype_t		 objtype;	  // object type identifier
	off_t			 phy_base;	  // physical global base address of memory region
	off_t			 page_base;	  // physical base address of memory page
	off_t			 page_offset; // offset of memory region base to memory page base
	size_t			 map_size;	  // size of mapped region
	void			*mapped_base; // for mmap
	void			*base;		  // application space base address of memory region
} e_mmap_t;


typedef struct {
	e_objtype_t		 objtype;	  // object type identifier
	unsigned int	 id;		  // core ID
	unsigned int	 row;		  // core absolute row number
	unsigned int	 col;		  // core absolute col number
	e_mmap_t		 mems;		  // core's SRAM data structure
	e_mmap_t		 regs;		  // core's e-regs data structure
} e_core_t;


// Platform data structures
typedef struct {
	e_objtype_t		 objtype;	  // object type identifier
	e_chiptype_t	 type;		  // Epiphany chip part number
	char			 version[32]; // version number of Epiphany chip
	unsigned int	 arch;		  // architecture generation
	unsigned int	 base_coreid; // chip base core ID
	unsigned int	 row;		  // chip absolute row number
	unsigned int	 col;		  // chip absolute col number
	unsigned int	 rows;		  // number of rows in chip
	unsigned int	 cols;		  // number of cols in chip
	unsigned int	 num_cores;	  // number of cores in chip
	unsigned int	 sram_base;	  // base offset of core SRAM
	unsigned int	 sram_size;	  // size of core SRAM
	unsigned int	 regs_base;	  // base offset of core registers
	unsigned int	 regs_size;	  // size of core registers segment
	off_t			 ioregs_n;	  // base address of north IO register
	off_t			 ioregs_e;	  // base address of east IO register
	off_t			 ioregs_s;	  // base address of south IO register
	off_t			 ioregs_w;	  // base address of west IO register
} e_chip_t;

typedef struct {
	e_objtype_t		 objtype;	  // object type identifier
	off_t			 phy_base;	  // physical global base address of external memory segment as seen by host
	off_t			 ephy_base;	  // physical global base address of external memory segment as seen by devic
	size_t			 size;		  // size of eDRAM allocated buffer for host side
	e_memtype_t		 type;		  // type of memory RD/WR/RW
} e_memseg_t;

typedef struct es_state_ es_state;

typedef struct {
	e_objtype_t		 objtype;	  // object type identifier
	e_platformtype_t type;		  // platform part number
	char			 version[32]; // version number of platform
	unsigned int	 hal_ver;	  // version number of the E-HAL
	int				 initialized; // platform initialized?

	int				 num_chips;	  // number of Epiphany chips in platform
	e_chip_t		*chip;		  // array of Epiphany chip objects
	unsigned int	 row;		  // platform absolute minimum row number
	unsigned int	 col;		  // platform absolute minimum col number
	unsigned int	 rows;		  // number of rows in platform
	unsigned int	 cols;		  // number of cols in platform

	int				 num_emems;	  // number of external memory segments in platform
	e_memseg_t		*emem;		  // array of external memory segments

	void			*priv;        // Target handle

} e_platform_t;

// Definitions for device workgroup communication object
typedef unsigned int e_coreid_t;

#define SIZEOF_IVT (0x28)

typedef struct {
	e_objtype_t	 objtype;			// 0x28
	e_chiptype_t chiptype;			// 0x2c
	e_coreid_t	 group_id;			// 0x30
	unsigned	 group_row;			// 0x34
	unsigned	 group_col;			// 0x38
	unsigned	 group_rows;		// 0x3c
	unsigned	 group_cols;		// 0x40
	unsigned	 core_row;			// 0x44
	unsigned	 core_col;			// 0x48
	unsigned	 alignment_padding; // 0x4c
} e_group_config_t;

typedef struct {
	e_objtype_t objtype;			// 0x50
	unsigned	base;				// 0x54
} e_emem_config_t;

typedef struct {
	e_objtype_t		 objtype;	  // object type identifier
	e_chiptype_t	 type;		  // Epiphany chip part number
	char			 version[32]; // version name of Epiphany chip
	unsigned int	 arch;		  // architecture generation
	unsigned int	 rows;		  // number of rows in chip
	unsigned int	 cols;		  // number of cols in chip
	unsigned int	 sram_base;	  // base offset of core SRAM
	unsigned int	 sram_size;	  // size of core SRAM
	unsigned int	 regs_base;	  // base offset of core registers
	unsigned int	 regs_size;	  // size of core registers segment
	off_t			 ioregs_n;	  // base address of north IO register
	off_t			 ioregs_e;	  // base address of east IO register
	off_t			 ioregs_s;	  // base address of south IO register
	off_t			 ioregs_w;	  // base address of west IO register
} e_chip_db_t;

#define E_CHIP_DB_NUM_CHIP_VERSIONS 3
extern e_chip_db_t e_chip_params_table[E_CHIP_DB_NUM_CHIP_VERSIONS];

typedef struct e_epiphany_t e_epiphany_t;
typedef struct e_mem_t e_mem_t;

struct e_target_ops {
	int (*ee_read_word) (e_epiphany_t *, unsigned, unsigned, const off_t);
	ssize_t (*ee_write_word) (e_epiphany_t *, unsigned, unsigned, off_t, int);
	ssize_t (*ee_read_buf) (e_epiphany_t *, unsigned, unsigned, const off_t, void *, size_t);
	ssize_t (*ee_write_buf) (e_epiphany_t *, unsigned, unsigned, off_t, const void *, size_t);
	int (*ee_read_reg) (e_epiphany_t *, unsigned, unsigned, const off_t);
	ssize_t (*ee_write_reg) (e_epiphany_t *, unsigned, unsigned, off_t, int);
	int (*ee_mread_word) (e_mem_t *, const off_t);
	ssize_t (*ee_mwrite_word) (e_mem_t *, off_t, int);
	ssize_t (*ee_mread_buf) (e_mem_t *, const off_t, void *, size_t);
	ssize_t (*ee_mwrite_buf) (e_mem_t *, off_t, const void *, size_t);
	int (*e_reset_system) (void);
	int (*populate_platform) (e_platform_t *, char *);
	int (*init) (void);
	void (*finalize) (void);
	int (*open) (e_epiphany_t *, unsigned, unsigned, unsigned, unsigned);
	int (*close) (e_epiphany_t *);
};

#ifdef __cplusplus
}
#endif

#endif // __E_HAL_DATA_LOC_H__
