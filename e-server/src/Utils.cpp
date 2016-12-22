// GDB Server Utilties: definition

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

// Change Management
// =================

//  4 May 09: Jeremy Bennett. Initial version based on the Embecosm GDB server
//                            for Verilator implementation.

// Embecosm Subversion Identification
// ==================================

// $Id: Utils.cpp 967 2011-12-29 07:07:27Z oraikhman $
//-----------------------------------------------------------------------------

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "Utils.h"


using std::cerr;
using std::endl;
using std::ostringstream;
using std::setbase;
using std::setfill;
using std::setw;


//-----------------------------------------------------------------------------
//!Utility to give the value of a hex char

//! @param[in] ch  A character representing a hexadecimal digit. Done as -1,
//!                for consistency with other character routines, which can
//!                use -1 as EOF.

//! @return  The value of the hex character, or -1 if the character is
//!          invalid.
//-----------------------------------------------------------------------------
uint8_t Utils::char2Hex (int c)
{
  return ((c >= 'a') && (c <= 'f')) ? c - 'a' + 10 :
    ((c >= '0') && (c <= '9')) ? c - '0' :
    ((c >= 'A') && (c <= 'F')) ? c - 'A' + 10 : -1;
}				// char2Hex()


//-----------------------------------------------------------------------------
//! Utility mapping a value to hex character

//! @param[in] d  A hexadecimal digit. Any non-hex digit returns a NULL char
//-----------------------------------------------------------------------------
char
Utils::hex2Char (uint8_t d)
{
  static const char map[] = "0123456789abcdef"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

  return map[d];
}				// hex2Char()


//-----------------------------------------------------------------------------
//! Convert a register to a hex digit string

//! The supplied 32-bit value is converted to an 8 digit hex string. The
//! string is null terminated for convenience. The hex string is bytes (pairs
//! of characters) in target-endian order.

//! @param[in]  val  The value to convert
//! @param[out] buf  The buffer for the text string
//-----------------------------------------------------------------------------
void
Utils::reg2Hex (uint32_t val, char *buf)
{
  for (int n = 0; n < 8; n += 2)
    {
      buf[n] = hex2Char ((val / 16) & 0xf);
      buf[n + 1] = hex2Char (val & 0xf);
      val /= 256;
    }

  buf[8] = 0;			// Useful to terminate as string
}				// reg2hex()


//-----------------------------------------------------------------------------
//! Convert a hex digit string to a register value

//! The supplied 8 digit hex string is bytes (pairs of characters) in
//! target-endian order (little endian in this case) and converted to a 32-bit
//! value

//! @param[in] buf  The buffer with the hex string

//! @return  The value to convert
//-----------------------------------------------------------------------------
uint32_t Utils::hex2Reg (char *buf)
{
  uint32_t
    val = 0;			// The result

  for (int n = 6; n >= 0; n -= 2)
    {
      val = val * 256 + char2Hex (buf[n]) * 16 + char2Hex (buf[n + 1]);
    }

  return val;
}				// hex2reg()


//-----------------------------------------------------------------------------
//! Convert an ASCII character string to pairs of hex digits

//! Both source and destination are null terminated.

//! @param[out] dest  Buffer to store the hex digit pairs (null terminated)
//! @param[in]  src   The ASCII string (null terminated)                      */
//-----------------------------------------------------------------------------
void
Utils::ascii2Hex (char *dest, const char *src)
{
  int i;

  // Step through converting the source string
  for (i = 0; src[i] != '\0'; i++)
    {
      char ch = src[i];

      dest[i * 2] = hex2Char (ch >> 4 & 0xf);
      dest[i * 2 + 1] = hex2Char (ch & 0xf);
    }

  dest[i * 2] = '\0';
}				// ascii2hex()


//-----------------------------------------------------------------------------
//! Convert pairs of hex digits to an ASCII character string

//! Both source and destination are null terminated.

//! @param[out] dest  The ASCII string (null terminated)
//! @param[in]  src   Buffer holding the hex digit pairs (null terminated)
//-----------------------------------------------------------------------------
void
Utils::hex2Ascii (char *dest, const char *src)
{
  int i;

  // Step through convering the source hex digit pairs
  for (i = 0; src[i * 2] != '\0' && src[i * 2 + 1] != '\0'; i++)
    {
      dest[i] =
	((char2Hex (src[i * 2]) & 0xf) << 4) | (char2Hex (src[i * 2 + 1]) &
						0xf);
    }

  dest[i] = '\0';
}				// hex2ascii()


//-----------------------------------------------------------------------------
//! "Unescape" RSP binary data

//! '#', '$' and '}' are escaped by preceding them by '}' and oring with 0x20.

//! This function reverses that, modifying the data in place.

//! @param[in] buf  The array of bytes to convert
//! @para[in]  len   The number of bytes to be converted

//! @return  The number of bytes AFTER conversion
//-----------------------------------------------------------------------------
int
Utils::rspUnescape (char *buf, int len)
{
  int fromOffset = 0;		// Offset to source char
  int toOffset = 0;		// Offset to dest char

  while (fromOffset < len)
    {
      // Is it escaped
      if ('}' == buf[fromOffset])
	{
	  fromOffset++;
	  buf[toOffset] = buf[fromOffset] ^ 0x20;
	}
      else
	{
	  buf[toOffset] = buf[fromOffset];
	}

      fromOffset++;
      toOffset++;
    }

  return toOffset;

}	// rspUnescape()


//-----------------------------------------------------------------------------
//! Microsecond sleep with interrupt handling

//! Replaces the old NanoSleepThread function, since we never want to sleep
//! less than 1 us, and we also want to deal with interruptions.

//! Repeat the sleep if interrupted. Any failure is a disaster and we give
//! up.

//! @param[in] us  Number of microseconds to sleep
//-----------------------------------------------------------------------------
void
Utils::microSleep (unsigned long int  us)
{
  struct timespec sleepTime;
  struct timespec remainingSleepTime;

  sleepTime.tv_sec  = us / 1000000;
  sleepTime.tv_nsec = us % 1000000;

  while (0 != nanosleep (&sleepTime, &remainingSleepTime))
    {
      if (EINTR == errno)
	sleepTime = remainingSleepTime;
      else
	{
	  cerr << "ERROR: Unexpected failure in microsleep: "
	       << strerror (errno) << ": Exiting." << endl;
	  exit (EXIT_FAILURE);
	}
    }
}	// microSleep ()


//-----------------------------------------------------------------------------
//! Convenience function to turn an integer into a string

//! @param[in] val    The value to convert
//! @param[in] base   The base for conversion. Default 10, valid values 8, 10
//!                   or 16. Other values will reset the iostream flags.
//! @param[in] width  The width to pad (with zeros).
//-----------------------------------------------------------------------------
string
Utils::intStr (int  val,
	       int  base,
	       int  width)
{
  ostringstream  os;

  os << setbase (base) << setfill ('0') << setw (width) << val;
  return os.str ();

}	// intStr ()


//-----------------------------------------------------------------------------
//! Convenience function that trims a string

//! @param[in] s      The string to trim
//-----------------------------------------------------------------------------
string
Utils::trim (const string &s)
{
    auto it = s.cbegin ();
    while (it != s.end () && isspace (*it))
        it++;

    auto rit = s.crbegin ();
    while (rit.base () != it && isspace (*rit))
        rit++;

    return string (it, rit.base ());
}


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// show-trailing-whitespace: t
// End:
