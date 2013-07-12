/*
  File: e_ctimer_set.s

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

//unsigned int e_ctimer_set(e_ctimer_id_t timer, unsigned val);

.file    "e_ctimer_set.s";


// ------------------------------------------------------------------------
.section .text;
.type    _e_ctimer_set, %function;
.global  _e_ctimer_set;

.balign 4;
_e_ctimer_set:

        and   r0, r0, r0;                    // set the status to check which timer register
    //----
        bne   _ctimer1_set;                  // jump to code for timer1


.balign 4;
_ctimer0_set:

        mov   r0, r1;                        // set the return value
    //----
        movts ctimer0, r1;                   // set the ctimer counter to the desired value
    //----
        rts;                                 // return with the current value of the ctimer


.balign 4;
_ctimer1_set:

        mov   r0, r1;                        // set the return value
    //----
        movts ctimer1, r1;                   // set the ctimer counter to the desired value
    //----
        rts;                                 // return with the current value of the ctimer


.size    _e_ctimer_set, .-_e_ctimer_set;


/* ------------------------------------------------------------------------
   End of File
   ------------------------------------------------------------------------ */

