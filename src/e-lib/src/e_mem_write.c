/*
  File: e_mem_write.c

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

#include <string.h>
#include "e_coreid.h"
#include "e_mem.h"

void *e_write(const void *remote, const void *src, unsigned row, unsigned col, void *dst, size_t n)
{
	void *gdst;

	if (*((e_objtype_t *) remote) == E_EPI_GROUP) {
		gdst = e_get_global_address(row, col, dst);
	} else if (*((e_objtype_t *) remote) == E_SHARED_MEM) {
		e_memseg_t *pmem = (e_memseg_t *)remote;
		gdst = (void*) (pmem->ephy_base + (unsigned) dst);
	} else {
		gdst = (void *) (e_emem_config.base + (unsigned) dst);
	}

	memcpy(gdst, src, n);

	return gdst;
}
