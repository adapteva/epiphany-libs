/*
  File: e_mutex.h

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

#ifndef MUTEX_H_
#define MUTEX_H_

#include "e_types.h"

typedef char e_barrier_t;
typedef int  e_mutex_t;
typedef int  e_mutexattr_t;

//-- for user to initialize a mutex structure
#define MUTEX_NULL     (0)
#define MUTEXATTR_NULL (0)
#define MUTEXATTR_DEFAULT MUTEXATTR_NULL


void e_mutex_init (unsigned row, unsigned col, e_mutex_t *mutex, e_mutexattr_t *attr)
	__attribute__((warning("e_mutex_init() is on probation and is currently a no-op. For correctness, ensure that mutex is statically zero-initialized.")));
void e_mutex_lock(unsigned row, unsigned col, e_mutex_t *mutex);
unsigned e_mutex_trylock(unsigned row, unsigned col, e_mutex_t *mutex);
void e_mutex_unlock(unsigned row, unsigned col, e_mutex_t *mutex);
void e_barrier_init(volatile e_barrier_t bar_array[], volatile e_barrier_t *tgt_bar_array[]);
void e_barrier(volatile e_barrier_t *bar_array, volatile e_barrier_t *tgt_bar_array[]);


#endif /* MUTEX_H_ */
