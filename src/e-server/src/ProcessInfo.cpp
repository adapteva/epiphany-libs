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

//! @param[in] pid  The Process ID to associate with this process.
//-----------------------------------------------------------------------------
ProcessInfo::ProcessInfo (const int  pid)
  : mPid (pid)
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
//! Accessor for the process ID

//! @return  The process ID
//-----------------------------------------------------------------------------
int
ProcessInfo::pid () const
{
  return  mPid;

}	// pid ()


//-----------------------------------------------------------------------------
//! Get an iterator for the start of the threads set

//! @return  The begin iterator.
//-----------------------------------------------------------------------------
set <Thread *>::iterator
ProcessInfo::threadBegin () const
{
  return  mThreads.begin ();

}	// threadBegin ()


//-----------------------------------------------------------------------------
//! Get an iterator for the end of the threads set

//! @return  The begin iterator.
//-----------------------------------------------------------------------------
set <Thread *>::iterator
ProcessInfo::threadEnd () const
{
  return  mThreads.end ();

}	// threadBegin ()


//-----------------------------------------------------------------------------
//! Add a thread to the process.

//! @param[in] threadPtr  Pointer to the thread to add
//! @return  TRUE if we successfully add the thread, FALSE if we were already
//!          in the set.
//-----------------------------------------------------------------------------
bool
ProcessInfo::addThread (Thread *threadPtr)
{
  return  mThreads.insert (threadPtr).second;

}	// addThread ()


//-----------------------------------------------------------------------------
//! Remove a thread from the process

//! Also remove it from the set of stopped threads if it is there.

//! @param[in] threadPtr  Pointer to the thread to remove
//! @return  TRUE if we succeed, and FALSE otherwise.
//-----------------------------------------------------------------------------
bool
ProcessInfo::eraseThread (Thread *threadPtr)
{
  mStoppedThreads.erase (threadPtr);
  return  mThreads.erase (threadPtr) == 1;

}	// eraseThread ()


//-----------------------------------------------------------------------------
//! Is this a thread in this process?

//! @param[in] threadPtr  Pointer to the thread to check
//! @return  TRUE if this thread is in this process
//-----------------------------------------------------------------------------
bool
ProcessInfo::hasThread (Thread *threadPtr)
{
  return  mThreads.find (threadPtr) != mThreads.end ();

}	// eraseThread ()


//-----------------------------------------------------------------------------
//! Add a thread to the set to be reported as stopped.

//! @param[in] threadPtr  Pointer to the thread to add
//! @return  TRUE if the thread was added, FALSE if it was already in the set.
//-----------------------------------------------------------------------------
bool
ProcessInfo::addStoppedThread (Thread* threadPtr)
{
  return  mStoppedThreads.insert (threadPtr).second;

}	// addStoppedThread ()


//-----------------------------------------------------------------------------
//! Clear the set of threads to be reported as stopped.
//-----------------------------------------------------------------------------
void
ProcessInfo::clearStoppedThreads ()
{
  mStoppedThreads.clear ();

}	// clearStoppedThreads ()


//-----------------------------------------------------------------------------
//! Get the next thread to report as stopped.

//! Do not remove the thread from the set of those to be reported.

//! @return  The thread to be reported, or NULL if there are none.
//-----------------------------------------------------------------------------
Thread*
ProcessInfo::getStoppedThread ()
{
  if (0 == numStoppedThreads ())
    return  NULL;
  else
    return *(mStoppedThreads.begin ());

}	// getStoppedThread ()


//-----------------------------------------------------------------------------
//! Get the next thread to report as stopped and remove it

//! Remove the thread from the set of those to be reported. The term "pop" is
//! not strictly correct, since it is not a stack.

//! @return  The thread to be reported, or NULL if there are none.
//-----------------------------------------------------------------------------
Thread*
ProcessInfo::popStoppedThread ()
{
  if (0 == numStoppedThreads ())
    return  NULL;
  else
    {
      Thread *t = *(mStoppedThreads.begin ());

      mStoppedThreads.erase (t);
      return  t;
    }
}	// popStoppedThread ()


//-----------------------------------------------------------------------------
//! Report the number of stopped threads to be reported

//! @return  The number of stopped threads to be reported.
//-----------------------------------------------------------------------------
int
ProcessInfo::numStoppedThreads ()
{
  return  mStoppedThreads.size ();

}	// clearStoppedThreads ()


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
