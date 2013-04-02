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

#ifndef __E_HAL_API_H__
#define __E_HAL_API_H__

#include <sys/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

/////////////////////////////////
// Device communication functions
//
// Platform configuration
int     e_init(char *hdf);
int     e_get_platform_info(e_platform_t *platform);
int     e_finalize();
// Epiphany access
int     e_open(e_epiphany_t *dev, unsigned row, unsigned col, unsigned rows, unsigned cols);
int     e_close(e_epiphany_t *dev);
// External memory access
int     e_alloc(e_mem_t *mbuf, off_t base, size_t size);
int     e_free(e_mem_t *mbuf);
//
// Data transfer
ssize_t e_read(void *dev, unsigned row, unsigned col, off_t from_addr, void *buf, size_t size);
ssize_t e_write(void *dev, unsigned row, unsigned col, off_t to_addr, const void *buf, size_t size);


///////////////////////////
// System control functions
int     e_reset_system();
int     e_reset_core(e_epiphany_t *dev, unsigned row, unsigned col);
int     e_start(e_epiphany_t *dev, unsigned row, unsigned col);
int     e_signal(e_epiphany_t *dev, unsigned row, unsigned col);
int     e_halt(e_epiphany_t *dev, unsigned row, unsigned col);
int     e_resume(e_epiphany_t *dev, unsigned row, unsigned col);


////////////////////
// Utility functions
unsigned e_get_num_from_coords(e_epiphany_t *dev, unsigned row, unsigned col);
void     e_get_coords_from_num(e_epiphany_t *dev, unsigned corenum, unsigned *row, unsigned *col);
//
e_bool_t e_is_addr_on_chip(void *addr);
e_bool_t e_is_addr_on_group(e_epiphany_t *dev, void *addr);
//
void     e_set_host_verbosity(e_hal_diag_t verbose);

#ifdef __cplusplus
}
#endif

#endif // __E_HAL_API_H__

