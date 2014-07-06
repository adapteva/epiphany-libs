// Matchpoint hash table: declaration

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

//  2 May 09: Jeremy Bennett. Initial version based on the Embecosm reference
//            implementation.

// Embecosm Subversion Identification
// ==================================

// $Id: MpHash.h 967 2011-12-29 07:07:27Z oraikhman $
//-----------------------------------------------------------------------------

#ifndef MP_HASH__H
#define MP_HASH__H

#include <inttypes.h>
#include <map>


using std::map;


//! Default size of the matchpoint hash table. Largest prime < 2^10
#define DEFAULT_MP_HASH_SIZE  1021


//! Enumeration of different types of matchpoint.

//! These have explicit values matching the second digit of 'z' and 'Z'
//! packets.
enum MpType
{
  BP_MEMORY = 0,
  BP_HARDWARE = 1,
  WP_WRITE = 2,
  WP_READ = 3,
  WP_ACCESS = 4
};


class MpHash;
class Thread;


//-----------------------------------------------------------------------------
//! A hash table for matchpoints

//! We do this as our own open hash table. Our keys are a pair of entities
//! (address and type), so STL map is not trivial to use.
//-----------------------------------------------------------------------------
class MpHash
{
public:

  // Constructor and destructor
  MpHash ();
   ~MpHash ();

  // Accessor methods
  void add (MpType    type,
	    uint32_t  addr,
	    Thread*   thread,
	    uint16_t  instr);
  bool lookup (MpType    type,
	       uint32_t  addr,
	       Thread*   thread);
  bool remove (MpType    type,
	       uint32_t  addr,
	       Thread*   thread,
	       uint16_t* instr = NULL);

private:

  // The key
  struct MpKey
  {
  public:
    MpType    type;		//!< Type of matchpoint
    uint32_t  addr;		//!< Address of the matchpoint
    Thread*   thread;           //!< Thread of the matchpoint

    bool operator < (const MpKey &key) const
    {
      if (type < key.type)
	return true;
      else if (type > key.type)
	return false;
      else if (addr < key.addr)
	return true;
      else if (addr > key.addr)
	return false;
      else if (thread < key.thread)
	return true;
      else
	return false;
    } ;
  };


  //! The hash table
  map <MpKey, uint16_t> mHashTab;
};

#endif // MP_HASH__H


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// show-trailing-whitespace: t
// End:
