/*
  File: e_ctimer_stop.c

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

#include "e_ctimers.h"
#include "e_regs.h"

unsigned e_ctimer_stop(e_ctimer_id_t timer)
{
// TODO convert to assembly to eliminate 2 function calls.
	unsigned shift;
	unsigned mask;
	unsigned config;
	unsigned count;

	shift = (timer) ? 8:4;
	mask = 0xf << shift;
	config = e_reg_read(E_CONFIG);
	// stop the timer
	e_reg_write(E_CONFIG, config & (~mask));

	count = e_reg_read(timer ? E_CTIMER1 : E_CTIMER0);

	return count;
}

