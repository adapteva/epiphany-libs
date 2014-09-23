// RSP packet: declaration

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

//  2 May 09: Jeremy Bennett. Initial version based on the Embedosm
//                            reference implementation


// Embecosm Subversion Identification
// ==================================

// $Id: RspPacket.cpp 967 2011-12-29 07:07:27Z oraikhman $
//-----------------------------------------------------------------------------

#include <iomanip>
#include <iostream>
#include <cstring>
#include <cerrno>
#include <cstdio>

#include "RspPacket.h"
#include "Utils.h"


using std::ostream;
using std::cerr;
using std::dec;
using std::endl;
using std::hex;
using std::setfill;
using std::setw;


//-----------------------------------------------------------------------------
//! Constructor

//! Allocate the new data buffer

//! @param[in]  _rspConnection  The RSP connection we will use
//! @param[in]  _bufSize        Size of data buffer to allocate
//-----------------------------------------------------------------------------
RspPacket::RspPacket (int _bufSize):bufSize (_bufSize)
{
  data = new char[_bufSize];

}				// RspPacket();


//-----------------------------------------------------------------------------
//! Destructor

//! Give back the data buffer
//-----------------------------------------------------------------------------
RspPacket::~RspPacket ()
{
  delete[]data;

}				// ~RspPacket()


//-----------------------------------------------------------------------------
//! Pack a string into a packet.

//! A convenience version of this method.

//! @param  str  The string to copy into the data packet before sending
//-----------------------------------------------------------------------------
void
RspPacket::packStr (const char *str)
{
  int slen = strlen (str);

  // Construct the packet to send, so long as string is not too big, otherwise
  // truncate. Add EOS at the end for convenient debug printout
  if (slen >= bufSize)
    {
      cerr << "Warning: String \"" << str
	<< "\" too large for RSP packet: truncated\n" << endl;
      slen = bufSize - 1;
    }

  strncpy (data, str, slen);
  data[slen] = 0;
  len = slen;

}				// packStr()


//-----------------------------------------------------------------------------
//! Pack a part of a string into a packet.

//! A convenience version of this method.

//! @param  str     The string to copy into the data packet before sending
//! @param  n       The maximum number of bytes to copy.
//! @param  prefix  A character to prefix the packet
//-----------------------------------------------------------------------------
void
RspPacket::packNStr (const char *str,
		     int         n,
		     char        prefix)
{
  // Construct the packet to send, so long as string is not too big, otherwise
  // truncate. Add prefix at the start and EOS at the end for convenient debug
  // printout
  if ((n + 1) >= bufSize)
    {
      cerr << "Warning: String \"" << str
		<< "\" too large for RSP packet: truncated\n" << endl;
      n = bufSize - 2;
    }

  data[0] = prefix;
  strncpy (&(data[1]), str, n);
  data[n + 1] = 0;
  len = n + 1;

}	// packNStr ()


//-----------------------------------------------------------------------------
//! Pack a const string as a hex encoded string into a packet for qRcmd.

//! The reply to qRcmd packets can be O followed by hex encoded ASCII.

//! @param  str  The string to copy into the data packet before sending
//-----------------------------------------------------------------------------
void
RspPacket::packHexstr (const char *str)
{
  int  slen = strlen (str);

  // Construct the packet to send, so long as string is not too big, otherwise
  // truncate. Add EOS at the end for convenient debug printout
  if (slen >= (bufSize / 2 - 1))
    {
      cerr << "Warning: String \"" << str
		<< "\" too large for RSP packet: truncated\n" << endl;
      slen = bufSize / 2 - 1;
    }

  // Construct the string the hard way
  data[0] = 'O';
  for (int i = 0; i < slen; i++)
    {
      int nybble_hi = str[i] >> 4;
      int nybble_lo = str[i] & 0x0f;

      data[i * 2 + 1] = nybble_hi + (nybble_hi > 9 ? 'a' - 10 : '0');
      data[i * 2 + 2] = nybble_lo + (nybble_lo > 9 ? 'a' - 10 : '0');
    }
  len       = slen * 2 + 1;
  data[len] = 0;

}	// packHexstr ()


//-----------------------------------------------------------------------------
//! Get the data buffer size

//! @return  The data buffer size
//-----------------------------------------------------------------------------
int
RspPacket::getBufSize ()
{
  return bufSize;
}				// getBufSize()


//-----------------------------------------------------------------------------
//! Get the current number of chars in the data buffer

//! @return  The number of chars in the data buffer
//-----------------------------------------------------------------------------
int
RspPacket::getLen ()
{
  return len;
}				// getLen()


//-----------------------------------------------------------------------------
//! Set the number of chars in the data buffer

//! @param[in] _len  The number of chars to be set
//-----------------------------------------------------------------------------
void
RspPacket::setLen (int _len)
{
  len = _len;
}				// setLen()


//-----------------------------------------------------------------------------
//! Output stream operator

//! We have a special case if this is an X packet, which has the form
//! "X<addr>,<length>:<binary data>. We don't want to print the binary
//! data. We match for everything up to the : and then print it out
//! specially.

//! This could be fooled by an output packet starting with the same string,
//! but I don't think that is currently possible.

//! @param[out] s  Stream to output to
//! @param[in]  p  Packet to output
//-----------------------------------------------------------------------------
ostream & operator << (ostream & s, RspPacket & p)
{
  unsigned int addr;
  unsigned int len;

  // See if we have an X packet, with a special version if it is X with a zero
  // length.
  if (2 == sscanf (p.data, "X%x,%x:", &addr, &len))
    {
      if (0 == len)
	{
	  return s << "RSP packet: " << dec << setw (3) << p.getLen ()
	    << setw (0) << " chars, \"X" << hex << addr << "," << hex
	    << len << ":\"";
	}
      else
	{
	  return s << "RSP packet: " << dec << setw (3) << p.getLen ()
	    << setw (0) << " chars, \"X" << hex << addr << "," << hex
	    << len << ":<binary data>\"";
	}
    }
  else
    {
      return s << "RSP packet: " << dec << setw (3) << p.getLen () << setw (0)
	<< " chars, \"" << p.data << "\"";
    }
}				// operator << ()


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// show-trailing-whitespace: t
// End:
