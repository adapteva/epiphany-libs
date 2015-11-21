/*
  File: e_types.h

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

#ifndef _E_TYPES_H_
#define _E_TYPES_H_


#ifdef __cplusplus
typedef enum {
	E_FALSE = false,
	E_TRUE  = true,
} e_bool_t;
#else
typedef enum {
	E_FALSE = 0,
	E_TRUE  = 1,
} e_bool_t;
#endif


typedef enum {
	E_OK   =  0,
	E_ERR  = -1,
	E_WARN = -2,
} e_return_stat_t;


#endif /* _E_TYPES_H_ */
