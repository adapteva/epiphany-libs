/*
  File: e_ic.h

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

#ifndef E_IC_H_
#define E_IC_H_

#include <e_types.h>

typedef void (*sighandler_t)(int);

typedef enum {
	E_SYNC         = 0,
	E_SW_EXCEPTION = 1,
	E_MEM_FAULT    = 2,
	E_TIMER0_INT   = 3,
	E_TIMER1_INT   = 4,
	E_MESSAGE_INT  = 5,
	E_DMA0_INT     = 6,
	E_DMA1_INT     = 7,
	E_USER_INT     = 9,
} e_irq_type_t;


void e_irq_attach(e_irq_type_t irq, sighandler_t handler);
void e_irq_set(unsigned row, unsigned col, e_irq_type_t irq);
void e_irq_clear(unsigned row, unsigned col, e_irq_type_t irq);
void e_irq_global_mask(e_bool_t state);
void e_irq_mask(e_irq_type_t irq, e_bool_t state);


#endif /* E_IC_H_ */
