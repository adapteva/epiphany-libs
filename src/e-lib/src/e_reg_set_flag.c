/*
  File: e_reg_set_flag.c

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
#include "e_regs.h"

void e_set_flag(e_bool_t state)
{
	e_coreid_t coreid, chipid;
	unsigned   config;
	unsigned  *gdst;

	coreid = e_get_coreid();
	if (e_group_config.chiptype == E_E16G301)
		chipid = coreid & 0xf3c;
	else
		chipid = coreid & 0xe38;

	config = e_reg_read(E_REG_CONFIG);
	config = (config & 0xffff0fff) | 0x00001000;
	e_reg_write(E_REG_CONFIG, config);

	gdst  = (unsigned *) (((chipid + 0x002) << 20) + E_REG_IO_FLAG_CFG);
	*gdst = 0x03ffffff;
	gdst  = (unsigned *) (((chipid + 0x002) << 20) + E_REG_IO_LINK_DEBUG);
	*gdst = (unsigned) state;

	config = config & 0xffff0fff;
	e_reg_write(E_REG_CONFIG, config);

	return;
}
