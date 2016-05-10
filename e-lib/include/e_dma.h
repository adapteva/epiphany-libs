/*
  File: e_dma.h

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

#ifndef _EPIPHANY_DMA_H_
#define _EPIPHANY_DMA_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <e_common.h>

/*
  These defs can be or'd together to form a value suitable for
  the dma config reg.
*/
typedef enum
{
	E_DMA_ENABLE        = (1<<0),
	E_DMA_MASTER        = (1<<1),
	E_DMA_CHAIN         = (1<<2),
	E_DMA_STARTUP       = (1<<3),
	E_DMA_IRQEN         = (1<<4),
	E_DMA_BYTE          = (0<<5),
	E_DMA_HWORD         = (1<<5),
	E_DMA_WORD          = (2<<5),
	E_DMA_DWORD         = (3<<5),
	E_DMA_MSGMODE       = (1<<10),
	E_DMA_SHIFT_SRC_IN  = (1<<12),
	E_DMA_SHIFT_DST_IN  = (1<<13),
	E_DMA_SHIFT_SRC_OUT = (1<<14),
	E_DMA_SHIFT_DST_OUT = (1<<15),
} e_dma_config_t;

typedef enum
{
	E_DMA_0 = 0,
	E_DMA_1 = 1,
} e_dma_id_t;

typedef struct
{
	unsigned config;
	unsigned inner_stride;
	unsigned count;
	unsigned outer_stride;
	void    *src_addr;
	void    *dst_addr;
} ALIGN(8) e_dma_desc_t;


int  e_dma_start(e_dma_desc_t *descriptor, e_dma_id_t chan);
int  e_dma_busy(e_dma_id_t chan);
void e_dma_wait(e_dma_id_t chan);
int  e_dma_copy(void *dst, void *src, size_t n);
void e_dma_set_desc(e_dma_id_t chan,
		unsigned config,     e_dma_desc_t *next_desc,
		unsigned strd_i_src, unsigned strd_i_dst,
		unsigned count_i,    unsigned count_o,
		unsigned strd_o_src, unsigned strd_o_dst,
		void     *addr_src,  void *addr_dst,
		e_dma_desc_t *desc);

#ifdef __cplusplus
}
#endif

#endif /* _EPIPHANY_DMA_H_ */
