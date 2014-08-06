/*
  File: memman.h

  This file is part of the Epiphany Software Development Kit.

  Copyright (C) 2014 Adapteva, Inc.
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

#ifndef _MEMMAN_H__
#define _MEMMAN_H__

#include <stddef.h>

/**
 *  Initialize the memory manager
 *
 *  This function initializes the memory manager. The memory managed
 *  begins at the address start and is size bytes long.
 *
 *  Returns 0 on success, -1 if start address is NULL or size is 0
 */
int memman_init(void *start, size_t size);

/**
 * Allocate a block of size bytes of memory.
 */
void* memman_alloc(size_t size);

/**
 * Free a block of memory located at address ptr.
 */
void memman_free(void *ptr);


#endif    /*  _MEMMAN_H__*/
