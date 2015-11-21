// Core ID class: Definition.

// Copyright (C) 2014 Embecosm Limited

// Contributor: Jeremy Bennett <jeremy.bennett@embecosm.com>

// This file is part of the Adapteva RSP server.

// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.

// You should have received a copy of the GNU General Public License along
// with this program.  If not, see <http://www.gnu.org/licenses/>.  */

//-----------------------------------------------------------------------------
// This RSP server for the Adapteva Epiphany was written by Jeremy Bennett on
// behalf of Adapteva Inc.

// Implementation is based on the Embecosm Application Note 4 "Howto: GDB
// Remote Serial Protocol: Writing a RSP Server"
// (http://www.embecosm.com/download/ean4.html).

// Note that the Epiphany is a little endian architecture.

// Commenting is Doxygen compatible.

#include <cctype>
#include <sstream>

#include "CoreId.h"


using std::ostringstream;
using std::string;

//-----------------------------------------------------------------------------
//! Constructor

//! @param[in] coreId  The Core ID to set. Defaults to zero.
//-----------------------------------------------------------------------------
CoreId::CoreId (unsigned int coreId) :
  mRow ((uint8_t) (coreId >> 6)),
  mCol ((uint8_t) (coreId & 0x3f))
{
}	// CoreId ()


//-----------------------------------------------------------------------------
//! Constructor from row and column

//! @param[in] row  The row of the core ID to set.
//! @param[in] col  The column of the core ID to set.
//-----------------------------------------------------------------------------
CoreId::CoreId (unsigned int row,
		unsigned int col) :
  mRow ((uint8_t) row),
  mCol ((uint8_t) col)
{
}	// CoreId ()


//-----------------------------------------------------------------------------
//! Row accessor

//! @return  The row of this CoreId
//-----------------------------------------------------------------------------
unsigned int
CoreId::row () const
{
  return (unsigned int) mRow;

}	// row ()


//-----------------------------------------------------------------------------
//! Column accessor

//! @return  The column of this CoreId
//-----------------------------------------------------------------------------
unsigned int
CoreId::col () const
{
  return (unsigned int) mCol;

}	// col ()


//-----------------------------------------------------------------------------
//! CoreId accessor

//! @return  The 12-bit coreId
//-----------------------------------------------------------------------------
uint16_t
CoreId::coreId () const
{
  return (((uint16_t) mRow) << 6) | ((uint16_t) mCol);

}	// coreId ()


//-----------------------------------------------------------------------------
//! Member string less than comparison operator

//! We compare rows and the columns

//! @param [in]  coreId     The core to compare against
//! @return  True if we are less than the specified CoreID
//-----------------------------------------------------------------------------
bool
CoreId::operator< (const CoreId& coreId) const
{
  if (mRow < coreId.row ())
    return true;
  else if (mRow > coreId.row ())
    return false;
  else if (mCol < coreId.col ())
    return true;
  else
    return false;

}	// operator< ()


//-----------------------------------------------------------------------------
//! Global stream output operator

//! Present coreId as 2 digit decimal row followed by 2 digit decimal column.

//! @param [in] os      The output stream
//! @param [in] coreId  The coreId to output
//! @return  An updated stream
//-----------------------------------------------------------------------------
ostream&
operator<< (ostream& os, const CoreId& coreId)
{
  int width = os.width ();
  char fill = os.fill ();

  os << setfill ('0') << setw (2) <<  coreId.row () << setw (2)
     << coreId.col ();

  os.width (width);
  os.fill (fill);

  return os;

}	// operator<< ()


//-----------------------------------------------------------------------------
//! Global stream input operator

//! Input is 2 digit decimal row immediately followed by 2 digit decimal
//! column.

//! @param [in]  os      The output stream
//! @param [out] coreId  The coreId read in
//! @return  An updated stream
//-----------------------------------------------------------------------------
istream&
operator>> (istream& is, CoreId& coreId)
{
  string val;
  unsigned int row = 0;
  unsigned int col = 0;

  for (int i = 0; i < 2; i++)
    {
      char c;
      is >> c;
      if (is.good () && isdigit (c))
	row = row * 10 + (c - '0');
      else
	{
	  is.clear (is.failbit);
	  return is;
	}
    }

  for (int i = 0; i < 2; i++)
    {
      char c;
      is >> c;
      if (is.good () && isdigit (c))
	col = col * 10 + (c - '0');
      else
	{
	  is.clear (is.failbit);
	  return is;
	}
    }

  coreId = CoreId (row, col);
  return is;

}	// operator>> ()


//-----------------------------------------------------------------------------
//! Member string concatenation operator

//! @param [in]  str     The string to concatenat after the coreId
//! @return  An updated stream
//-----------------------------------------------------------------------------
string
CoreId::operator+ (const string& str)
{
  ostringstream s;
  s << *this << setw (0);

  return s.str () + str;

}	// operator+ ()


//-----------------------------------------------------------------------------
//! Global string concatenation operator

//! @param [in]  str     The string to concatenat before the coreId
//! @param [out] coreId  The coreId to concatenate after the string
//! @return  An updated stream
//-----------------------------------------------------------------------------
string
operator+ (const string& str,
	   const CoreId& coreId)
{
  ostringstream s;
  s << coreId << setw (0);

  return str + s.str ();

}	// operator+ ()


//-----------------------------------------------------------------------------
//! Global string concatenation and assignment operator

//! @param [in, out]  str     The string to concatenat before the coreId and
//!                           for the result.
//! @param [out]      coreId  The coreId to concatenate after the string
//! @return  An updated stream
//-----------------------------------------------------------------------------
const string&
operator+= (string& str,
	    const CoreId& coreId)
{
  ostringstream s;
  s << coreId << setw (0);

  str += s.str ();
  return str;

}	// operator+= ()


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
