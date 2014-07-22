/*
  File: epiphany-shm-manager.h

  This file is part of the Epiphany Software Development Kit.

  Copyright (C) 2014 Adapteva, Inc.
  See AUTHORS for list of contributors.
  Support e-mail: <support@adapteva.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License (LGPL)
  as published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  and the GNU Lesser General Public License along with this program,
  see the files COPYING and COPYING.LESSER.	 If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef __EPIPHANY_SHM_MANAGER_H__
#define __EPIPHANY_SHM_MANAGER_H__

/*
** Function prototypes.
*/

/**
 * Initialize the shared memory manager
 * FIXME: this is an internal function - hide it!
 */
int e_shm_init();

/**
 * Teardown the shared memory manager
 * FIXME: this is an internal function - hide it!
 */
void e_shm_finalize(void);

#endif	  /*  __EPIPHANY_SHM_MANAGER_H__ */
