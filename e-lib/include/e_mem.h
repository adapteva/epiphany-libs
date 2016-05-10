/*
  File: e_mem.h

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

#ifndef E_MEM_H_
#define E_MEM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include "e_common.h"
#include "e_coreid.h"

typedef enum {
	E_RD   = 1,
	E_WR   = 2,
	E_RDWR = 3,
} e_memtype_t;

typedef struct {
	e_objtype_t	 objtype;	  // object type identifier
	off_t		 phy_base;	  // physical global base address of external memory segment as seen by host side
	off_t		 ephy_base;	  // physical global base address of external memory segment as seen by device side
	size_t		 size;		  // size of eDRAM allocated buffer for host side
	e_memtype_t	 type;		  // type of memory RD/WR/RW
} e_memseg_t;

void *e_read(const void *remote, void *dst, unsigned row, unsigned col, const void *src, size_t n);
void *e_write(const void *remote, const void *src, unsigned row, unsigned col, void *dst, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* E_MEM_H_ */
