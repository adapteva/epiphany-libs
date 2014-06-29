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

#ifndef PROCESS_INFO__H
#define PROCESS_INFO__H

#include <set>

using std::set;

class Thread;


//-----------------------------------------------------------------------------
//! Class describing an Epiphany GDB process

//! A process corresponds to an Epiphany workgroup, and the cores to threads
//! within the process.

//! Each process has an integer identifier, which is the positive value
//! associated with this process by GDB.
//-----------------------------------------------------------------------------
class ProcessInfo
{
public:

  // Constructor and destructor
  ProcessInfo (const int  pid);
  ~ProcessInfo ();

  // Accessors
  int  pid () const;

  set <Thread *>::iterator threadBegin () const;
  set <Thread *>::iterator threadEnd () const;
  bool addThread (Thread* threadPtr);
  bool eraseThread (Thread* threadPtr);
  bool hasThread (Thread* threadPtr);

private:

  //! Our process ID as supplied by the GDB client
  int  mPid;

  //! The threads making up the process
  set <Thread *> mThreads;

};	// ProcessInfo ()

#endif // PROCESS_INFO__H


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
