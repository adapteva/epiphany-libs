/*
  File: e_ctimers.h

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

#ifndef CTIMER_H_
#define CTIMER_H_


/**
 * @file e_ctimers.h
 * @brief The timer programming
 *
 * @section DESCRIPTION
 * The timer functions programs the timers for counting clock cycles or certain events of interest such as pipeline stalls
 * or memory bank contention cycle.
 */

/** @enum e_ctimer_id_t
 * The supported ctimer event
 */
typedef enum
{
	E_CTIMER_0 = 0,
	E_CTIMER_1 = 1
} e_ctimer_id_t;

/** @enum e_ctimer_config_t
 * The supported ctimer event
 */
typedef enum
{
	E_CTIMER_OFF              = 0x0,
	E_CTIMER_CLK              = 0x1,
	E_CTIMER_IDLE             = 0x2,
	E_CTIMER_IALU_INST        = 0x4,
	E_CTIMER_FPU_INST         = 0x5,
	E_CTIMER_DUAL_INST        = 0x6,
	E_CTIMER_E1_STALLS        = 0x7,
	E_CTIMER_RA_STALLS        = 0x8,
	E_CTIMER_EXT_FETCH_STALLS = 0xc,
	E_CTIMER_EXT_LOAD_STALLS  = 0xd,
/*	E_CTIMER_EXTN_STALLS      = 0x3, */
} e_ctimer_config_t;

/** @def MAX_CTIMER_VALUE 0xffffffff
 * Defines the maximum timer value  (MAX_UINT)
*/
#define E_CTIMER_MAX (~0)

unsigned e_ctimer_get(e_ctimer_id_t timer);
unsigned e_ctimer_set(e_ctimer_id_t timer, unsigned int val);
unsigned e_ctimer_start(e_ctimer_id_t timer, e_ctimer_config_t config);
unsigned e_ctimer_stop(e_ctimer_id_t timer);
void e_wait(e_ctimer_id_t timer, unsigned int clicks);

#endif /* CTIMER_H_ */
