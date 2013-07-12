/*
  File: e_coreid_get_global_address.c

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

#include "e_coreid.h"

void *e_get_global_address(unsigned row, unsigned col, const void *ptr)
{
	unsigned   uptr;
	e_coreid_t coreid;

	/* If the address is global, return the pointer unchanged */
	if (((unsigned) ptr) & 0xfff00000)
		return ptr;
	else if ((row == E_SELF) || (col == E_SELF))
		coreid = e_get_coreid();
	else
		coreid = (row * 0x40 + col) + e_group_config.group_id;

	/* Get the 20 ls bits of the pointer and add coreid. */
//	uptr = ((unsigned) ptr) & 0x000fffff; // not needed because of the 1st condition above
	uptr = (unsigned) ptr;
	uptr = (coreid << 20) | uptr;

	return (void *) uptr;
}
