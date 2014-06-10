#ifndef EPIPHANY_XML_C__H
#define EPIPHANY_XML_C__H
/*
  File: epiphany_xml_c.h

  This file is part of the Epiphany Software Development Kit.

  Copyright (C) 2014 Adapteva, Inc.
  Contributed by Ola Jeppsson.
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

/**
 *
 * Desc: C API for e-xml
 *
**/

#include "epiphany_platform.h"

typedef void* e_xml_t;

e_xml_t                e_xml_new(char *filename);
void                   e_xml_delete(e_xml_t handle);
int                    e_xml_parse(e_xml_t handle);
platform_definition_t* e_xml_get_platform(e_xml_t handle);
void                   e_xml_print_platform(e_xml_t handle);
unsigned               e_xml_version(e_xml_t handle);

#endif /* EPIPHANY_XML_C__H */
