// Core ID class: Declaration.

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

#ifndef CORE_ID__H
#define CORE_ID__H

#include <iomanip>
#include <iostream>
#include <string>

//! @todo We would prefer to use <cstdint> here, but that requires ISO C++ 2011.
#include <stdint.h>


using std::istream;
using std::ostream;
using std::setfill;
using std::setw;
using std::string;


//-----------------------------------------------------------------------------
//! Class describing an Epiphany Core ID

//! The Epiphany processor arranges its cores in a square. The Core ID is a
//! 12-bit value, with the MS 6 bits being the row and the LS 6 bits being the
//! column.

//! We provide constructors, accessors and stream handling.
//-----------------------------------------------------------------------------
class CoreId
{
public:

  // Constructors. Default copy constructor and destructor are fine
  CoreId (unsigned int coreId = 0);
  CoreId (unsigned int row,
	  unsigned int col);

  // Accessors
  unsigned int  row () const;
  unsigned int  col () const;
  uint16_t      coreId () const;

  // Member operators
  bool operator< (const CoreId& coreId) const;
  string operator+ (const string& str);

private:

  //! The row
  uint8_t mRow;

  //! The column
  uint8_t mCol;

};	// CoreId ()


// Global Operators
ostream& operator<< (ostream& os, const CoreId &coreId);
istream& operator>> (istream& is, CoreId &coreId);
string operator+ (const string& str, const CoreId &coreId);
const string& operator+= (string& str, const CoreId &coreId);

#endif // CORE_ID__H


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
