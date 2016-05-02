/*
  File: e_reg_read.c

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

#include "e_regs.h"
#include "e_coreid.h"

unsigned e_reg_read(e_core_reg_id_t reg_id)
{
	unsigned reg_val, coreid;
	volatile unsigned *addr;

	// TODO: function affects integer flags. Add special API for STATUS
	switch (reg_id)
	{
	case E_REG_CONFIG:
		__asm__ ("MOVFS %0, CONFIG" : "=r" (reg_val));
		return reg_val;
	case E_REG_STATUS:
		__asm__ ("MOVFS %0, STATUS" : "=r" (reg_val));
		return reg_val;
	default:
		__asm__ ("movfs %0, coreid" : "=r" (coreid));
		addr = (unsigned *) (coreid << 20 | reg_id);
		return *addr;
	}
}
