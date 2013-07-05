/*
  File: e_coreid_neighbor_id.c

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

void e_neighbor_id(e_coreid_wrap_t dir, e_coreid_wrap_t wrap, unsigned *nrow, unsigned *ncol)
{
	unsigned row_mask, col_mask;
	unsigned row, col;

	/* Indexed by [wrap][dir] */
	static const unsigned row_adjust[3][2] =
	{
		{ 0,  0 }, /* GROUP_WRAP */
		{ 0,  0 }, /* ROW_WRAP  */
		{-1,  1 }  /* COL_WRAP  */
	};

	static const unsigned col_adjust[3][2] =
	{
		{-1,  1 }, /* GROUP_WRAP */
		{-1,  1 }, /* ROW_WRAP  */
		{ 0,  0 }  /* COL_WRAP  */
	};

	/* This only works for Power-Of-Two group dimensions */
	row_mask = e_group_config.group_rows - 1;
	col_mask = e_group_config.group_cols - 1;


	/* Calculate the next core coordinates */
	row = e_group_config.core_row + row_adjust[wrap][dir];
	col = e_group_config.core_col + col_adjust[wrap][dir];

	if (wrap == E_GROUP_WRAP)
		/* when the new col is negative, it is wrapped around due to the unsignedness
		 * of the variable. I any case, an edge core's column gets greater than group
		 * size
		 */
		if (col >= e_group_config.group_cols)
			row = row + (2 * dir - 1);

	*nrow = row & row_mask;
	*ncol = col & col_mask;

	return;
}
