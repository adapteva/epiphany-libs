// Memory range class: Declaration

// This file is part of the Epiphany Software Development Kit.

// Copyright (C) 2013-2014 Adapteva, Inc.

// Contributor: Jeremy Bennett <jeremy.bennett@embecosm.com>

// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.

// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.

// You should have received a copy of the GNU General Public License along
// with this program (see the file COPYING).  If not, see
// <http://www.gnu.org/licenses/>.

#ifndef MEM_RANGE__H
#define MEM_RANGE__H

#include <inttypes.h>


//! A class describing a range of memory.

//! The memory may be associated with a core, in which case it will also have
//! a memory mapped register subrange, or it may be external memory.
class MemRange
{
public:

  // Constructor and destructor
  MemRange (uint32_t _minAddrVal = 0,
	    uint32_t _maxAddrVal = 0,
	    uint32_t _minRegAddrVal = 0,
	    uint32_t _maxRegAddrVal = 0);
  ~MemRange ();

  // Comparator
  bool operator () (const MemRange& lhs,
		    const MemRange& rhs) const;

  // Functions to access memory ranges
  void  minAddr (const uint32_t _minAddrVal);
  uint32_t  minAddr () const;
  void  maxAddr (const uint32_t _maxAddrVal);
  uint32_t  maxAddr () const;
  void  addrRange (const uint32_t _minAddrVal,
		   const uint32_t _maxAddrVal);
  void  minRegAddr (const uint32_t _minRegAddrVal);
  uint32_t  minRegAddr () const;
  void  maxRegAddr (const uint32_t _maxRegAddrVal);
  uint32_t  maxRegAddr () const;
  void  regAddrRange (const uint32_t _minRegAddrVal,
		   const uint32_t _maxRegAddrVal);

private:

  // Address boundaries
  uint32_t  minAddrVal;		//!< Lowest address in this range.
  uint32_t  maxAddrVal;		//!< Highest address in this range.
  uint32_t  minRegAddrVal;	//!< Lowest reg mapped address in this range.
  uint32_t  maxRegAddrVal;	//!< Highest reg mapped address in this range.

};	// MemRange

#endif	// MEM_RANGE__H


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// show-trailing-whitespace: t
// End:
