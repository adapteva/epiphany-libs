/*
  File: e_dma_set_desc.c

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

#include "e_dma.h"


void e_dma_set_desc(
		e_dma_id_t chan,
		unsigned config,     e_dma_desc_t *next_desc,
		unsigned strd_i_src, unsigned strd_i_dst,
		unsigned count_i,    unsigned count_o,
		unsigned strd_o_src, unsigned strd_o_dst,
		void     *addr_src,  void *addr_dst,
		e_dma_desc_t *desc)
{
	e_dma_wait(chan);
	desc->config       = (((unsigned) next_desc) << 16) | config;
	desc->inner_stride = (strd_i_dst             << 16) | strd_i_src;
	desc->count        = (count_o                << 16) | count_i;
	desc->outer_stride = (strd_o_dst             << 16) | strd_o_src;
	desc->src_addr     = addr_src;
	desc->dst_addr     = addr_dst;

	return;
}

