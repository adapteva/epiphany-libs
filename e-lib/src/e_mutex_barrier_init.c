/*
  File: e_mutex_barrier_init.c

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

#include <e_coreid.h>
#include <e_mutex.h>

// TODO: use asingle pointer to store barrier addresses
void e_barrier_init(volatile e_barrier_t bar_array[],
					volatile e_barrier_t *tgt_bar_array[])
{
	unsigned int corenum, i, j;

	corenum  = e_group_config.core_row * e_group_config.group_cols + e_group_config.core_col;

	/* Previously, bar_array was cleared here. That's incorrect
	 * behavior as it imposes a dead-lock race condition between core0's
	 * completion of e_barrier_init() and any of the other cores' first call to
	 * e_barrier(). To avoid that, it is now a requirement that bar_array is
	 * statically initialized. */

	if (corenum == 0)
	{
		for (i=0; i<e_group_config.group_rows; i++)
			for (j=0; j<e_group_config.group_cols; j++)
				tgt_bar_array[corenum++] = (e_barrier_t *) e_get_global_address(i, j, (void *) &(bar_array[0]));
	} else {
		tgt_bar_array[0] = (e_barrier_t *) e_get_global_address(0, 0, (void *) &(bar_array[corenum]));
	}

	return;
}
