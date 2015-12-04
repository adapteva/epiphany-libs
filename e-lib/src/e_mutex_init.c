/*
  File: e_mutex_init.c

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
#include "e_mutex.h"


void e_mutex_init(unsigned row, unsigned col, e_mutex_t *mutex, e_mutexattr_t *attr)
{
	/* Unused */
	(void) row;
	(void) col;
	(void) mutex;
	(void) attr;

	/* This function is on probation and is currently a no-op.
	 * For correctness, ensure that mutex is statically zero-initialized. */

	/* Previously, mutex was cleared here. That's incorrect behavior as it
	 * imposes a race condition between any core's completion of e_mutex_init()
	 * and any other cores' call to e_mutex_lock(). To avoid that, it is now a
	 * requirement that mutex is statically initialized to 0. */

	return;
}
