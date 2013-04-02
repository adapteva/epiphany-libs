/*
  File: epiphany-hal-api.h

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

#ifndef __E_HAL_API_LOC_H__
#define __E_HAL_API_LOC_H__

#include <sys/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

/////////////////////////////////
// Device communication functions
//
int     ee_read_word(e_epiphany_t *dev, unsigned row, unsigned col, const off_t from_addr);
ssize_t ee_write_word(e_epiphany_t *dev, unsigned row, unsigned col, off_t to_addr, int data);
ssize_t ee_read_buf(e_epiphany_t *dev, unsigned row, unsigned col, const off_t from_addr, void *buf, size_t size);
ssize_t ee_write_buf(e_epiphany_t *dev, unsigned row, unsigned col, off_t to_addr, const void *buf, size_t size);
int     ee_read_reg(e_epiphany_t *dev, unsigned row, unsigned col, const off_t from_addr);
ssize_t ee_write_reg(e_epiphany_t *dev, unsigned row, unsigned col, off_t to_addr, int data);
int     ee_read_esys(off_t from_addr);
ssize_t ee_write_esys(off_t to_addr, int data);
//
// For legacy code support
ssize_t ee_read_abs(unsigned address, void *buf, size_t size);
ssize_t ee_write_abs(unsigned address, void *buf, size_t size);
//
ssize_t ee_mread(e_mem_t *mbuf, const off_t from_addr, void *buf, size_t size);
ssize_t ee_mwrite(e_mem_t *mbuf, off_t to_addr, const void *buf, size_t size);
int     ee_mread_word(e_mem_t *mbuf, const off_t from_addr);
ssize_t ee_mwrite_word(e_mem_t *mbuf, off_t to_addr, int data);
ssize_t ee_mread_buf(e_mem_t *mbuf, const off_t from_addr, void *buf, size_t size);
ssize_t ee_mwrite_buf(e_mem_t *mbuf, off_t to_addr, const void *buf, size_t size);


/////////////////////////
// Core control functions


////////////////////
// Utility functions
unsigned ee_get_num_from_id(e_epiphany_t *dev, unsigned coreid);
unsigned ee_get_id_from_coords(e_epiphany_t *dev, unsigned row, unsigned col);
unsigned ee_get_id_from_num(e_epiphany_t *dev, unsigned corenum);
void     ee_get_coords_from_id(e_epiphany_t *dev, unsigned coreid, unsigned *row, unsigned *col);
int      ee_set_chip_params(e_chip_t *dev);

#ifdef __cplusplus
}
#endif

#endif // __E_HAL_API_LOC_H__

