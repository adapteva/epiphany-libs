// Matchpoint hash table: definition

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

// $Id: MpHash.cpp 967 2011-12-29 07:07:27Z oraikhman $
//-----------------------------------------------------------------------------

#include <cstdlib>

#include "MpHash.h"


//-----------------------------------------------------------------------------
//! Constructor

//! Does nothing. Retained for backwards compatibility.
//-----------------------------------------------------------------------------
MpHash::MpHash ()
{
}	// MpHash()


//-----------------------------------------------------------------------------
//! Destructor

//! Does nothing. Retained for backwards compatibility.
//-----------------------------------------------------------------------------
MpHash::~MpHash ()
{
}	// ~MpHash()


//-----------------------------------------------------------------------------
//! Add an entry to the hash table

//! Add the entry if it wasn't already there. If it was there do nothing. The
//! match just be on type and addr. The instr need not match, since if this is
//! a duplicate insertion (perhaps due to a lost packet) they will be
//! different.

//! @note This method allocates memory. Care must be taken to delete it when
//! done.

//! @param[in] type   The type of matchpoint
//! @param[in] addr   The address of the matchpoint
//! @param[in] thread The thread of the matchpoint
//! @para[in]  instr  The instruction to associate with the address
//-----------------------------------------------------------------------------
void
MpHash::add (MpType    type,
	     uint32_t  addr,
	     Thread*   thread,
	     uint16_t  instr)
{
  MpKey  key = {type, addr, thread};
  mHashTab[key] = instr;

}	// add()


//-----------------------------------------------------------------------------
//!Look up an entry in the matchpoint hash table

//! The match must be on type, address and thread ID.

//! @param[in] type   The type of matchpoint
//! @param[in] addr   The address of the matchpoint
//! @param[in] thread The thread of the matchpoint

//! @return  TRUE if an entry is found, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
MpHash::lookup (MpType    type,
		uint32_t  addr,
		Thread*   thread)
{
  MpKey  key = {type, addr, thread};
  return (mHashTab.find (key) != mHashTab.end ());

}	// lookup()


//-----------------------------------------------------------------------------
//! Delete an entry from the matchpoint hash table

//! If it is there the entry is deleted from the hash table. If it is not
//! there, no action is taken. The match must be on type AND addr. The entry
//! (MpEntry::) is itself deleted

//! The usual fun and games tracking the previous entry, so we can delete
//! things.

//! @param[in]  type   The type of matchpoint
//! @param[in]  addr   The address of the matchpoint
//! @param[in]  thread The thread of the matchpoint
//! @param[out] instr  If non-NULL a location for the instruction found.
//!                    Default NULL.

//! @return  TRUE if an entry was found and deleted
//-----------------------------------------------------------------------------
bool
MpHash::remove (MpType    type,
		uint32_t  addr,
		Thread*   thread,
		uint16_t* instr)
{
  MpKey  key = {type, addr, thread};

  if (NULL != instr)
    {
      map <MpKey, uint16_t>::iterator it = mHashTab.find (key);
      if (it != mHashTab.end ())
	*instr = it->second;
    }

  return 1 == mHashTab.erase (key);

}	// remove()


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// show-trailing-whitespace: t
// End:
