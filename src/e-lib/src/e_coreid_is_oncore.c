/*
  File: e_coreid_is_oncore.c

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

#include "e_coreid.h"

/* Is address on this core? */
e_bool_t e_is_on_core(const void *ptr)
{
	unsigned coreid;
	e_bool_t res;

	coreid = (((unsigned) ptr) & 0xfff00000) >> 20;

	if (coreid == e_get_coreid())
		res = E_TRUE;
	else if (coreid == 0)
		res = E_TRUE;
	else
		res = E_FALSE;

	return res;
}
