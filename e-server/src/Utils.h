// GDB Server Utilties: declaration

// Copyright (C) 2008, 2009, 2014 Embecosm Limited
// Copyright (C) 2009-2014 Adapteva Inc.

// Contributor Jeremy Bennett <jeremy.bennett@embecosm.com>
// Contributor: Oleg Raikhman <support@adapteva.com>
// Contributor: Yaniv Sapir <support@adapteva.com>

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

//-----------------------------------------------------------------------------

#ifndef UTILS_H
#define UTILS_H

#include <ctime>
#include <inttypes.h>
#include <string>


using std::string;


//-----------------------------------------------------------------------------
//! A class offering a number of convenience utilities for the GDB Server.

//! All static functions. This class is not intended to be instantiated.
//-----------------------------------------------------------------------------
class Utils
{
public:

  static uint8_t char2Hex (int c);
  static char hex2Char (uint8_t d);
  static void reg2Hex (uint32_t val, char *buf);
  static uint32_t hex2Reg (char *buf);
  static void ascii2Hex (char *dest, const char *src);
  static void hex2Ascii (char *dest, const char *src);
  static int rspUnescape (char *buf, int len);
  static void microSleep (unsigned long us);

  static string  intStr (int  val,
			 int  base = 10,
			 int  width = 0);

  static string trim (const string &s);

private:

  // Private constructor cannot be instantiated
    Utils ()
  {
  };

};				// class Utils

#endif // UTILS_H


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// show-trailing-whitespace: t
// End:
