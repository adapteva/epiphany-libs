// Memory range class: Definition

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

#include <iostream>

#include "MemRange.h"


using std::cerr;
using std::endl;


//! Constructor.

//! @param[in] _minAddrVal     Lowest address in this range.
//! @param[in] _maxAddrVal     Highest address in this range.
//! @param[in] _minRegAddrVal  Lowest register mapped address in this
//!                            range.  Default 0.
//! @param[in] _maxRegAddrVal  Highest register mapped address in this
//!                            range.  Default 0.
//! 
MemRange::MemRange (uint32_t _minAddrVal,
		    uint32_t _maxAddrVal,
		    uint32_t _minRegAddrVal,
		    uint32_t _maxRegAddrVal) :
  minAddrVal (_minAddrVal),
  maxAddrVal (_maxAddrVal),
  minRegAddrVal (_minRegAddrVal),
  maxRegAddrVal (_maxRegAddrVal)
{
}	// MemRange ()


//! Destructor
MemRange::~MemRange ()
{
}	// ~MemRange ()


//! Comparator for the map.

//! This provides a weak ordering. If the test fails reflexively, the keys are
//! equal. For this to be useful for us, we consider ranges equal if one is
//! whole enclosed within the other. Thus:

//! - (2,4) and (1,5) are equal
//! - (1,4) and (1,5) are equal
//! - (2,6) and (1,5) are not equal

//! We still need an ordering when they are not equal. For non overlapping
//! ranges the one with the lower minimum address is considered the lesser.
bool
MemRange::operator () (const MemRange& lhs,
		       const MemRange& rhs) const
{
  if ((lhs.minAddr () <= rhs.minAddr ())
      && (lhs.maxAddr () >= rhs.maxAddr ()))
    {
      return false;
    }
  else if ((lhs.minAddr () >= rhs.minAddr ())
	   && (lhs.maxAddr () <= rhs.maxAddr ()))
      {
	return false;
      }
  else
    {
      // Note that we cannot have equal min addresses by this point
      return lhs.minAddr () < rhs.minAddr ();
    }
}	// operator ()


//! Set the minimum address
void
MemRange::minAddr (const uint32_t  _minAddrVal)
{
  minAddrVal = _minAddrVal;

}	// minAddr ()


//! Get the minimum address
const uint32_t
MemRange::minAddr () const
{
  return  minAddrVal;

}	// minAddr ()


//! Set the maximum address
void
MemRange::maxAddr (const uint32_t  _maxAddrVal)
{
  maxAddrVal = _maxAddrVal;

}	// maxAddr ()


//! Get the maximum address
const uint32_t
MemRange::maxAddr () const
{
  return  maxAddrVal;

}	// maxAddr ()


//! Set the address range
void
MemRange::addrRange (const uint32_t  _minAddrVal,
		     const uint32_t  _maxAddrVal)
{
  minAddr (_minAddrVal);
  maxAddr (_maxAddrVal);

}	// addRange ()


//! Set the minimum register mappped address
void
MemRange::minRegAddr (const uint32_t  _minRegAddrVal)
{
  minRegAddrVal = _minRegAddrVal;

}	// minRegAddr ()


//! Get the minimum register mapped address
const uint32_t
MemRange::minRegAddr () const
{
  return  minRegAddrVal;

}	// minRegAddr ()


//! Set the maximum register mapped address
void
MemRange::maxRegAddr (const uint32_t  _maxRegAddrVal)
{
  maxRegAddrVal = _maxRegAddrVal;

}	// maxRegAddr ()


//! Get the maximum register mapped address
const uint32_t
MemRange::maxRegAddr () const
{
  return  maxRegAddrVal;

}	// maxRegAddr ()


//! Set the register mapped address range
void
MemRange::regAddrRange (const uint32_t  _minRegAddrVal,
			const uint32_t  _maxRegAddrVal)
{
  minRegAddr (_minRegAddrVal);
  maxRegAddr (_maxRegAddrVal);

}	// regAddrange ()


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// show-trailing-whitespace: t
// End:
