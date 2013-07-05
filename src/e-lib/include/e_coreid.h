/*
  File: e_coreid.h

  This file is part of the Epiphany Software Development Kit.

  Copyright (C) 2013 Adapteva, Inc.
  Contributed by Oleg Raikhman, Jim Thomas, Yaniv Sapir <support@adapteva.com>

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

#ifndef _E_COREID_H_
#define _E_COREID_H_

#include "e_types.h"


typedef unsigned int e_coreid_t;

#define E_SELF (-1)

typedef enum
{
/* e_neighbor_id() wrap constants */
	E_GROUP_WRAP =  0,
	E_ROW_WRAP  =  1,
	E_COL_WRAP  =  2,
/* e_neighbor_id() dir constants */
	E_PREV_CORE =  0,
	E_NEXT_CORE =  1,
} e_coreid_wrap_t;

typedef enum {
	E_NULL         = 0,
	E_EPI_PLATFORM    ,
	E_EPI_CHIP        ,
	E_EPI_GROUP       ,
	E_EPI_CORE        ,
	E_EXT_MEM         ,
	E_MAPPING         ,
} e_objtype_t;

typedef enum {
	E_E16G301 = 0,
	E_E64G401 = 1,
} e_chiptype_t;

typedef struct {
	e_objtype_t  objtype;           // 0x28
	e_chiptype_t chiptype;          // 0x2c
	e_coreid_t   group_id;          // 0x30
	unsigned     group_row;         // 0x34
	unsigned     group_col;         // 0x38
	unsigned     group_rows;        // 0x3c
	unsigned     group_cols;        // 0x40
	unsigned     core_row;          // 0x44
	unsigned     core_col;          // 0x48
	unsigned     alignment_padding; // 0x4c
} e_group_config_t;

typedef struct {
	e_objtype_t objtype;            // 0x50
	unsigned    base;               // 0x54
} e_emem_config_t;

extern const e_group_config_t e_group_config;
extern const e_emem_config_t  e_emem_config;


e_coreid_t e_get_coreid();

e_coreid_t e_coreid_from_coords(unsigned row, unsigned col);

void *e_get_global_address(unsigned row, unsigned col, const void *ptr);

void e_coords_from_coreid(e_coreid_t coreid, unsigned *row, unsigned *col);

e_bool_t e_is_on_core(const void *ptr);

void e_neighbor_id(e_coreid_wrap_t dir, e_coreid_wrap_t wrap, unsigned *row, unsigned *col);


#endif /* _E_COREID_H_ */

