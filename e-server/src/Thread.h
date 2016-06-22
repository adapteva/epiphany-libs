// Thread class: Declaration.

// Copyright (C) 2014, 2016 Embecosm Limited
// Copyright (C) 2016 Pedro Alves

// Contributor: Jeremy Bennett <jeremy.bennett@embecosm.com>
// Contributor: Pedro Alves <pedro@palves.net>

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

#ifndef THREAD__H
#define THREAD__H

#include <set>

using std::set;

class CoreId;
class GdbServer;


//-----------------------------------------------------------------------------
//! Class describing an Epiphany GDB thread

//! A thread corresponds to an Epiphany core. This class provides all the
//! functionality a thread needs.

//! @note The thread is referred to externally by a GDB thread ID.
//-----------------------------------------------------------------------------
class Thread
{
public:

  // Constructor and destructor
  Thread (CoreId         coreId,
	  TargetControl* target,
	  ServerInfo*    si,
	  int            tid);
  ~Thread ();

  // Accessors
  CoreId  coreId () const;
  int   tid () const;
  bool  isHalted ();
  bool  isIdle ();
  bool  isInterruptible () const;

  GdbServer::TargetSignal pendingSignal () const { return mPendingSignal; }
  void setPendingSignal (GdbServer::TargetSignal sig) { mPendingSignal = sig; }

  void setLastAction (GdbServer::vContAction last) { mLastAction = last; }
  GdbServer::vContAction lastAction () const { return mLastAction; }

  // Control of the thread
  bool  halt ();
  bool  resume ();
  bool  idle ();
  bool  activate ();

  // Save and restore the Interrupt Vector Table
  bool saveIVT ();
  bool restoreIVT ();

  // Code manipulation
  void insertBkptInstr (uint32_t addr);
  GdbServer::TargetSignal getException ();

  // Main functions for reading and writing memory
  bool readMemBlock (uint32_t  addr,
		     uint8_t* buf,
		     size_t  len) const;
  bool writeMemBlock (uint32_t  addr,
		      uint8_t* buf,
		      size_t  len) const;
  bool  readMem32 (uint32_t  addr,
		   uint32_t& val) const;
  uint32_t  readMem32 (uint32_t  addr) const;
  bool  writeMem32 (uint32_t  addr,
		    uint32_t val) const;
  bool  readMem16 (uint32_t  addr,
		   uint16_t& val) const;
  uint16_t  readMem16 (uint32_t  addr) const;
  bool  writeMem16 (uint32_t  addr,
		    uint16_t val) const;
  bool  readMem8 (uint32_t  addr,
		  uint8_t& val) const;
  uint8_t  readMem8 (uint32_t  addr) const;
  bool  writeMem8 (uint32_t  addr,
		   uint8_t val) const;

  // Main functions for reading and writing registers
  bool readReg (unsigned int regnum,
		uint32_t& regval) const;
  uint32_t  readReg (unsigned int regnum) const;
  bool writeReg (unsigned int regNum,
		 uint32_t value) const;

  // Convenience functions for reading and writing various common registers
  CoreId readCoreId () const;
  uint32_t readStatus () const;
  uint32_t readPc () const;
  void writePc (uint32_t addr);
  uint32_t readLr () const;
  void writeLr (uint32_t addr);
  uint32_t readFp () const;
  void writeFp (uint32_t addr);
  uint32_t readSp () const;
  void writeSp (uint32_t  addr);


private:

  //! Number of entries in IVT table
  static const uint32_t IVT_ENTRIES = 10;

  //! Our core
  CoreId  mCoreId;

  //! A handle on the target
  TargetControl *mTarget;

  //! Local pointer to server info
  ServerInfo *mSi;

  //! Our GDB thread ID
  int  mTid;

  //! A save buffer for the IVT
  uint8_t mIVTSaveBuf[IVT_ENTRIES * TargetControl::E_INSTR_BYTES];

  //! Our debug state
  enum
    {
      DEBUG_RUNNING,
      DEBUG_HALTED
    } mDebugState;

  //! The last vCont action applied to this thread.  Once the thread
  //! stops and the stop is reported to the client, this is set to
  //! ACTION_STOP.
  GdbServer::vContAction mLastAction;

  //! Our run state
  enum
    {
      RUN_UNKNOWN,
      RUN_ACTIVE,
      RUN_IDLE
    } mRunState;

  //! If not TARGET_SIGNAL_NONE, a signal that has not yet been
  //! reported to the client.
  GdbServer::TargetSignal mPendingSignal;

  // Helper routines for target access
  uint32_t regAddr (unsigned int  regnum) const;

};	// Thread ()

#endif // THREAD__H


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
