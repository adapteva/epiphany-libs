/*
  File: e_mutex_barrier_init.c

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

#include <e_coreid.h>
#include <e_mutex.h>

// TODO: use asingle pointer to store barrier addresses

void e_barrier_init(volatile e_barrier_t bar_array[], e_barrier_t *tgt_bar_array[])
{
	int corenum, numcores, i, j;

	numcores = e_group_config.group_rows * e_group_config.group_cols;
	corenum  = e_group_config.core_row * e_group_config.group_cols + e_group_config.core_col;

	for (i=0; i<numcores; i++)
		bar_array[i] = 0;

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
