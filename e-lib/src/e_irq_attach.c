/*
  File: e_irq_attach.c

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

#include <e_ic.h>

#define B_OPCODE 0x000000e8 // OpCode of the B<*> instruction

#undef _USE_SIGNAL_
#ifndef _USE_SIGNAL_


#undef e_irq_attach

void e_irq_attach(e_irq_type_t irq, e_irqhandler_t handler)
{
	unsigned *ivt;

	// The event vector is a relative branch instruction to the IRS.
	// To use the direct ISR dispatch, we need to re-program the
	// IVT entry with the new branch instruction.

	// Set TIMER0 IVT entry address
	ivt  = (unsigned *) (irq << 2);
	// Set the relative branch offset.
	*ivt = (((unsigned) handler - (unsigned) ivt) >> 1) << 8;
	// Add the instruction opcode.
	*ivt = *ivt | B_OPCODE;

	return;
}

#else
#include <machine/epiphany_config.h>
#include <signal.h>

void e_irq_attach(e_irq_type_t irq, sighandler_t handler)
{
	signal(((irq - E_SYNC) + SIG_RESET), handler);

	return;
}

#endif
