/*
  File: e_ic.h

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

#ifndef E_IC_H_
#define E_IC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "e_types.h"


/* This is the type the compiler expect for functions with the
 * interrupt attribute  */
typedef void (*e_irqhandler_t)(void);

/* "Legacy" support. Argument will be bogus. signalhandler_t is defined in
 * <signal.h> so we couldn't change the prototype even if we wanted to.  */
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


void e_irq_attach(e_irq_type_t irq, e_irqhandler_t handler);
/* Support sighandler_t without giving type warnings */
#define e_irq_attach(i, h) e_irq_attach((i), (e_irqhandler_t) (h))

void e_irq_set(unsigned row, unsigned col, e_irq_type_t irq);
void e_irq_clear(unsigned row, unsigned col, e_irq_type_t irq);
void e_irq_global_mask(e_bool_t state);
void e_irq_mask(e_irq_type_t irq, e_bool_t state);

#ifdef __cplusplus
}
#endif

#endif /* E_IC_H_ */
