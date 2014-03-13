// Process Info class: Declaration.

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

#include <iostream>

#include "ProcessInfo.h"


using std::cerr;
using std::endl;


//-----------------------------------------------------------------------------
//! Constructor.

//! @param[in] pid  The process ID. This is fixed and can't subsequently be
//                  changed.
//-----------------------------------------------------------------------------
ProcessInfo::ProcessInfo (int  pid) :
  mPid (pid)
{
}	// ProcessInfo ()


//-----------------------------------------------------------------------------
//! Destructor.

//! Currently empty. Here as a placeholder for future development.
//-----------------------------------------------------------------------------
ProcessInfo::~ProcessInfo ()
{
}	// ProcessInfo ()


//-----------------------------------------------------------------------------
//! Get the process ID

//! @return  The Process ID.
//-----------------------------------------------------------------------------
int
ProcessInfo::pid () const
{
  return mPid;

}	// pid ()


//-----------------------------------------------------------------------------
//! Get an iterator for the start of the threads set

//! @return  The begin iterator.
//-----------------------------------------------------------------------------
set <int>::iterator
ProcessInfo::threadBegin () const
{
  return  mThreads.begin ();

}	// threadBegin ()


//-----------------------------------------------------------------------------
//! Get an iterator for the end of the threads set

//! @return  The begin iterator.
//-----------------------------------------------------------------------------
set <int>::iterator
ProcessInfo::threadEnd () const
{
  return  mThreads.end ();

}	// threadBegin ()


//-----------------------------------------------------------------------------
//! Add a thread to the process.

//! @param[in] thread  The thread to add
//! @return  TRUE if we successfully add the thread, false otherwise
//-----------------------------------------------------------------------------
bool
ProcessInfo::addThread (int thread)
{
  return  mThreads.insert (thread).second;

}	// addThread ()


//-----------------------------------------------------------------------------
//! Remove a thread from the process

//! @param[in] thread  The thread to remove
//! @return  TRUE if we succeed, and FALSE otherwise.
//-----------------------------------------------------------------------------
bool
ProcessInfo::eraseThread (int thread)
{
  return  mThreads.erase (thread) == 1;

}	// eraseThread ()


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
