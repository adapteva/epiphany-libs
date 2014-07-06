// Thread class: Declaration.

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

#include "CoreId.h"
#include "GdbServer.h"
#include "Thread.h"
#include "Utils.h"

using std::cerr;
using std::endl;


//-----------------------------------------------------------------------------
//! Constructor.

//! @param[in] coreId  The core ID we are associated with.
//! @param[in] target  Handle for the target.
//-----------------------------------------------------------------------------
Thread::Thread (CoreId         coreId,
		TargetControl* target,
		ServerInfo*    si,
		const int      tid) :
  mCoreId (coreId),
  mTarget (target),
  mSi (si),
  mTid (tid),
  mIVTSavedP (false),
  mDebugState (DEBUG_RUNNING),
  mRunState (RUN_UNKNOWN),
  mReportedState (UNREPORTED)
{
  // Some sanity checking that numbering has not got misaligned! This is a
  // consequence of our desire to have properly typed constants.
  assert (regAddr (GdbServer::R0_REGNUM)      == TargetControl::R0);
  assert (regAddr (GdbServer::R0_REGNUM + 63) == TargetControl::R63);

  assert (regAddr (GdbServer::CONFIG_REGNUM)      == TargetControl::CONFIG);
  assert (regAddr (GdbServer::STATUS_REGNUM)      == TargetControl::STATUS);
  assert (regAddr (GdbServer::PC_REGNUM)          == TargetControl::PC);
  assert (regAddr (GdbServer::DEBUGSTATUS_REGNUM)
	  == TargetControl::DEBUGSTATUS);
  assert (regAddr (GdbServer::IRET_REGNUM)        == TargetControl::IRET);
  assert (regAddr (GdbServer::IMASK_REGNUM)       == TargetControl::IMASK);
  assert (regAddr (GdbServer::ILAT_REGNUM)        == TargetControl::ILAT);
  assert (regAddr (GdbServer::FSTATUS_REGNUM)     == TargetControl::FSTATUS);
  assert (regAddr (GdbServer::DEBUGCMD_REGNUM)    == TargetControl::DEBUGCMD);
  assert (regAddr (GdbServer::RESETCORE_REGNUM)   == TargetControl::RESETCORE);
  assert (regAddr (GdbServer::COREID_REGNUM)      == TargetControl::COREID);

}	// Thread ()


//-----------------------------------------------------------------------------
//! Destructor.

//! Currently empty. Here as a placeholder for future development.
//-----------------------------------------------------------------------------
Thread::~Thread ()
{
}	// Thread ()


//-----------------------------------------------------------------------------
//! Get the core ID

//! @return  The core ID
//-----------------------------------------------------------------------------
CoreId
Thread::coreId () const
{
  return  mCoreId;

}	// coreId ()


//-----------------------------------------------------------------------------
//! Get the thraed ID

//! @return  The thead ID
//-----------------------------------------------------------------------------
int
Thread::tid () const
{
  return  mTid;

}	// tid ()


//-----------------------------------------------------------------------------
//! Are we halted?

//! If we are already halted, no need to inquire again. If we were previously
//! running, we need to check.

//! @todo The old code used to worry about pending loads and fetches. We have
//!       left that out for now. Does this matter?

//! @return  TRUE if we are halted, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::isHalted ()
{
  if (mDebugState == DEBUG_HALTED)
    return  true;

  uint32_t debugstatus = readReg (GdbServer::DEBUGSTATUS_REGNUM);
  uint32_t haltStatus = debugstatus & TargetControl::DEBUGSTATUS_HALT_MASK;

  if (haltStatus == TargetControl::DEBUGSTATUS_HALT_HALTED)
    {
      mDebugState = DEBUG_HALTED;
      mReportedState = UNREPORTED;	// Must be when we change state
      return true;
    }
  else
    {
      assert (mReportedState == UNREPORTED);	// Sanity check
      mDebugState = DEBUG_RUNNING;
      return false;
    }
}	// isHalted ()


//-----------------------------------------------------------------------------
//! Are we idle?

//! If we are already idle, no need to inquire again. If we were previously
//! active, we need to check.

//! @return  TRUE if we are idle, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::isIdle ()
{
  if (mRunState == RUN_IDLE)
    return  true;

  uint32_t status = readReg (GdbServer::STATUS_REGNUM);
  uint32_t idleStatus = status & TargetControl::STATUS_ACTIVE_MASK;

  if (idleStatus == TargetControl::STATUS_ACTIVE_IDLE)
    {
      mRunState = RUN_IDLE;
      return true;
    }
  else
    {
      mRunState = RUN_ACTIVE;
      return false;
    }
}	// isIdle ()


//-----------------------------------------------------------------------------
//! Check if global interrupts are enabled for thread.

//! @return  TRUE if the thread has global interrupts enabled, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::isInterruptible () const
{
  uint32_t status = readReg (GdbServer::STATUS_REGNUM);
  uint32_t gidStatus = status & TargetControl::STATUS_GID_MASK;

  return gidStatus == TargetControl::STATUS_GID_ENABLED;

}	// isInterruptible ()


//-----------------------------------------------------------------------------
//! Check if this thread has been reported

//! @return  TRUE if the thread was resumed for stepping, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::isReported () const
{
  return mReportedState == REPORTED;

}	// isReported ()


//-----------------------------------------------------------------------------
//! Mark this thread as having been reported
//-----------------------------------------------------------------------------
void
Thread::markReported ()
{
  mReportedState = REPORTED;

}	// isInterruptible ()


//-----------------------------------------------------------------------------
//! Force the thread to halt

//! @return  TRUE if we halt successfully, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::halt ()
{
  // No need to do anything if we are already halted
  if (DEBUG_HALTED == mDebugState)
    return true;

  if (!writeReg (GdbServer::DEBUGCMD_REGNUM,
		 TargetControl::DEBUGCMD_COMMAND_HALT))
    cerr << "Warning: failed to write HALT to DEBUGCMD." << endl;

  if (mSi->debugStopResume ())
    cerr << "DebugStopResume: Wrote HALT to DEBUGCMD for core " << mCoreId
	 << endl;

  if (!isHalted ())
    {
      Utils::microSleep (1);

      // Try again, then give up
      if (!isHalted ())
	{
	  cerr << "Warning: core " << mCoreId << "has not halted after 1 us "
	       << endl;
	  uint32_t val;
	  if (readReg (GdbServer::DEBUGSTATUS_REGNUM, val))
	    cerr << "         - DEBUGSTATUS = 0x" << Utils::intStr (val, 16, 8)
		 << endl;
	  else
	    cerr << "         - unable to access DEBUGSTATUS register."
		 << endl;

	  mDebugState = DEBUG_RUNNING;
	  mReportedState = UNREPORTED;		// Sane thing to do?
	  return false;
	}
    }

  mDebugState = DEBUG_HALTED;
  mReportedState = UNREPORTED;		// Must be when we change state
  return true;

}	// halt ();


//-----------------------------------------------------------------------------
//! Force the thread to resume

//! We record whether this is resumed for stepping or continuing. This does
//! not really matter to the thread, but it helps the calling routines to know
//! what to do when the thread halts.

//! @param[in] setpping  TRUE if we are resuming for a single step
//! @return  TRUE if we resume successfully, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::resume ()
{
  // Whatever happens this will be the state. Even if we fail, we cannot be
  // sure we are still halted.
  mDebugState = DEBUG_RUNNING;
  mReportedState = UNREPORTED;	// Must be the case when we change state

  // We need to do this, even if we were previously running, in case we have
  // since halted.
  if (!writeReg (GdbServer::DEBUGCMD_REGNUM,
		 TargetControl::DEBUGCMD_COMMAND_RUN))
    {
      cerr << "Warning: Failed to resume core" << mCoreId << "." << endl;
      return false;
    }

  if (mSi->debugStopResume ())
    cerr << "DebugStopResume: Wrote RUN to DEBUGCMD for core " << mCoreId
	 << endl;

  return true;

}	// resume ();


//-----------------------------------------------------------------------------
//! Force the thread to idle execution state.

//! @return  TRUE if we successfully become idle, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::idle ()
{
  // No need to do anything if we are already idle
  if (RUN_IDLE == mRunState)
    return true;

  // A bit dangerous to do this if we aren't halted. For now just warn
  // @todo Should we force a temporary halt if necessary?
  if (!isHalted ())
    cerr << "Warning: Forcing IDLE run state for core " << mCoreId
	 << "when not halted." << endl;

  uint32_t status;

  if (!readReg (GdbServer::STATUS_REGNUM, status))
    {
      cerr << "Warnign: Failed to read status when forcing IDLE for core "
	   << mCoreId << "." << endl;
      mRunState = RUN_ACTIVE;
      return false;
    }

  status &= ~TargetControl::STATUS_ACTIVE_MASK;
  status |= TargetControl::STATUS_ACTIVE_IDLE;

  if (!writeReg (GdbServer::FSTATUS_REGNUM, status))
    {
      cerr << "Warnign: Failed to write status when forcing IDLE for core "
	   << mCoreId << "." << endl;
      mRunState = RUN_ACTIVE;
      return false;
    }

  if (mSi->debugStopResume ())
    cerr << "DebugStopResume: Wrote IDLE to FSTATUS for core " << mCoreId
	 << endl;

  mRunState = RUN_IDLE;
  return true;

}	// idle ();


//-----------------------------------------------------------------------------
//! Force the thread to active execution state

//! @return  TRUE if we resume successfully, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::activate ()
{
  // Whatever happens this will be the state. Even if we fail, we cannot be
  // sure we are still idle.
  mRunState = RUN_ACTIVE;

  // A bit dangerous to do this if we aren't halted. For now just warn
  // @todo Should we force a temporary halt if necessary?
  if (!isHalted ())
    cerr << "Warning: Forcing ACTIVE run state for core " << mCoreId
	 << "when not halted." << endl;

  // We need to do this, even if we were previously active, in case we have
  // since hit an IDLE opcode.
  uint32_t status;

  if (!readReg (GdbServer::STATUS_REGNUM, status))
    {
      cerr << "Warnign: Failed to read status when forcing ACTIVE for core "
	   << mCoreId << "." << endl;
      return false;
    }

  status &= ~TargetControl::STATUS_ACTIVE_MASK;
  status |= TargetControl::STATUS_ACTIVE_ACTIVE;

  if (!writeReg (GdbServer::FSTATUS_REGNUM, status))
    {
      cerr << "Warnign: Failed to write status when forcing ACTIVE for core "
	   << mCoreId << "." << endl;
      return false;
    }

  if (mSi->debugStopResume ())
    cerr << "DebugStopResume: Wrote ACTIVE to FSTATUS for core " << mCoreId
	 << endl;

  return true;

}	// activate ();


//-----------------------------------------------------------------------------
//! Put breakpoints throughout the IVT

//! This should not happen twice at the same time for one thread. The only
//! place we don't put a breakpoint is at the PC itself. If we are already in
//! the IVT, we have dealt with the interrupt.

//! @note The result is an indication of whether the IVT was saved, not if the
//!       breakpoints were successfully inserted. So it is possible that some
//!       breakpoints may not be inserted.
//! @return  TRUE if we save the IVT successfully, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::breakpointIVT ()
{
  assert (!mIVTSavedP);			// Sanity check

  // First make sure we can save the IVT
  if (!readMemBlock (TargetControl::IVT_SYNC, mIVTSaveBuf,
		     sizeof (mIVTSaveBuf)))
    {
      cerr << "Warning: Failed to save IVT: exceptions not caught" << endl;
      return false;
    }

  mIVTSavedP = true;

  // Now breakpoint all the addresses EXCEPT the current PC and SYNC (aka
  // RESET).
  static uint32_t IVTAddresses [] =
    {
      TargetControl::IVT_SYNC,
      TargetControl::IVT_SWE,
      TargetControl::IVT_PROT,
      TargetControl::IVT_TIMER0,
      TargetControl::IVT_TIMER1,
      TargetControl::IVT_MSG,
      TargetControl::IVT_DMA0,
      TargetControl::IVT_DMA1,
      TargetControl::IVT_WAND,
      TargetControl::IVT_USER
    };
  static int IVTAddressesSize= sizeof (IVTAddresses) / sizeof (IVTAddresses[0]);

  uint32_t pc = readPc ();

  for (int i = 0; i < IVTAddressesSize; i++)
    {
      uint32_t  entry = IVTAddresses[i];

      if ((entry != TargetControl::IVT_SYNC) && (entry != pc))
	insertBreakpoint (entry);
    }

  return true;

}	// breakpointIVT ()


//-----------------------------------------------------------------------------
//! Restore instructions to IVT

//! We might be asked to restore the same thread several times, but we ignore
//! subsequent occasions.

//! @return  TRUE if we restored successfully of we were already restored,
//!          FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::restoreIVT ()
{
  if (mIVTSavedP)
    {
      if (writeMemBlock (TargetControl::IVT_SYNC, mIVTSaveBuf,
			 sizeof (mIVTSaveBuf)))
	{
	  mIVTSavedP = false;
	  return  true;
	}
      else
	{
	  cerr << "Warning: failed to restore IVT: IVT may be corrupted."
	       << endl;
	  return  false;
	}
    }
  else
    return  true;		// Already done

}	// restoreIVT ()


//---------------------------------------------------------------------------
//! Insert a breakpoint instruction.

//! @param [in] addr  Where to put the breakpoint
//-----------------------------------------------------------------------------
void
Thread::insertBreakpoint (uint32_t addr)
{
  writeMem16 (addr, GdbServer::BKPT_INSTR);

  if (mSi->debugStopResumeDetail ())
    cerr << "DebugStopResumeDetail: insert breakpoint for core " << mCoreId
	 << "at " << addr << endl;

}	// insertBreakpoint ()


//-----------------------------------------------------------------------------
//! Get the current exception

//! @param[in] version  Version of Epiphany to check.
//! @return  The GDB signal corresponding to any exception.
//-----------------------------------------------------------------------------
GdbServer::TargetSignal
Thread::getException (int  version)
{
  uint32_t coreStatus = readStatus ();
  uint32_t exbits = coreStatus & TargetControl::STATUS_EXCAUSE_MASK;

  switch (version)
    {
    case 3:

      switch (exbits)
	{
	case TargetControl::STATUS_EXCAUSE3_UNIMPL:
	  return GdbServer::TARGET_SIGNAL_ILL;

	case TargetControl::STATUS_EXCAUSE3_SWI:
	  return GdbServer::TARGET_SIGNAL_INT;

	case TargetControl::STATUS_EXCAUSE3_UNALIGN:
	  return GdbServer::TARGET_SIGNAL_BUS;

	case TargetControl::STATUS_EXCAUSE3_ILLEGAL:
	  return GdbServer::TARGET_SIGNAL_SEGV;

	case TargetControl::STATUS_EXCAUSE3_FPU:
	  return GdbServer::TARGET_SIGNAL_FPE;

	default:
	  // @todo Can we get this?
	  cerr << "Warning: Unexpected software exception cause 0x"
	       << Utils::intStr (exbits, 16) << ": treated as ABORT signal."
	       << endl;
	  return GdbServer::TARGET_SIGNAL_ABRT;
	}

    case 4:

      switch (exbits)
	{
	case TargetControl::STATUS_EXCAUSE4_UNIMPL:
	  return GdbServer::TARGET_SIGNAL_ILL;

	case TargetControl::STATUS_EXCAUSE4_SWI:
	  return GdbServer::TARGET_SIGNAL_INT;

	case TargetControl::STATUS_EXCAUSE4_UNALIGN:
	  return GdbServer::TARGET_SIGNAL_BUS;

	case TargetControl::STATUS_EXCAUSE4_ILLEGAL:
	  return GdbServer::TARGET_SIGNAL_SEGV;

	case TargetControl::STATUS_EXCAUSE4_FPU:
	  return GdbServer::TARGET_SIGNAL_FPE;

	default:
	  // @todo Can we get this?
	  cerr << "Warning: Unexpected software exception cause 0x"
	       << Utils::intStr (exbits, 16) << ": treated as ABORT signal."
	       << endl;
	  return GdbServer::TARGET_SIGNAL_ABRT;
	}

    default:
      assert (false);				// Sanity check
      return  GdbServer::TARGET_SIGNAL_NONE;	// Keep compiler check happy.
    }

}	// getException ()


//-----------------------------------------------------------------------------
//! Read a block of memory from the target

//! @param[in]  addr    The address to read from
//! @param[out] buf     Where to put the data read
//! @param[in]  len     The number of bytes to read
//! @return  TRUE on success, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::readMemBlock (uint32_t  addr,
		      uint8_t* buf,
		      size_t  len) const
{
  return mTarget->readBurst (mCoreId, addr, buf, len);

}	// readMemBlock ()


//-----------------------------------------------------------------------------
//! Write a block of memory to the target

//! @param[in] addr    The address to write to
//! @param[in] buf     Where to get the data to be written
//! @param[in] len     The number of bytes to read
//! @return  TRUE on success, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::writeMemBlock (uint32_t  addr,
		       uint8_t* buf,
		       size_t  len) const
{
  return mTarget->writeBurst (mCoreId, addr, buf, len);

}	// writeMemBlock ()


//-----------------------------------------------------------------------------
//! Read a 32-bit value from memory in the target

//! In this version the caller is responsible for error handling.

//! @param[in]  addr    The address to read from.
//! @param[out] val     The value read.
//! @return  TRUE on success, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::readMem32 (uint32_t  addr,
		   uint32_t& val) const
{
  return mTarget->readMem32 (mCoreId, addr, val);

}	// readMem32 ()


//-----------------------------------------------------------------------------
//! Read a 32-bit value from memory in the target

//! In this version we print a warning if the read fails.

//! @param[in]  addr    The address to read from.
//! @return  The value read, undefined if there is a failure.
//-----------------------------------------------------------------------------
uint32_t
Thread::readMem32 (uint32_t  addr) const
{
  uint32_t val;
  if (!mTarget->readMem32 (mCoreId, addr, val))
    cerr << "Warning: readMem32 failed." << endl;
  return val;

}	// readMem32 ()


//-----------------------------------------------------------------------------
//! Write a 32-bit value to memory in the target

//! The caller is responsible for error handling.

//! @param[in]  addr   The address to write to.
//! @param[out] val    The value to write.
//! @return  TRUE on success, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::writeMem32 (uint32_t  addr,
		    uint32_t  val) const
{
  return mTarget->writeMem32 (mCoreId, addr, val);

}	// writeMem32 ()


//-----------------------------------------------------------------------------
//! Read a 16-bit value from memory in the target

//! In this version the caller is responsible for error handling.

//! @param[in]  addr    The address to read from.
//! @param[out] val     The value read.
//! @return  TRUE on success, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::readMem16 (uint32_t  addr,
		   uint16_t& val) const
{
  return mTarget->readMem16 (mCoreId, addr, val);

}	// readMem16 ()


//-----------------------------------------------------------------------------
//! Read a 16-bit value from memory in the target

//! In this version we print a warning if the read fails.

//! @param[in]  addr    The address to read from.
//! @return  The value read, undefined if there is a failure.
//-----------------------------------------------------------------------------
uint16_t
Thread::readMem16 (uint32_t  addr) const
{
  uint16_t val;
  if (!mTarget->readMem16 (mCoreId, addr, val))
    cerr << "Warning: readMem16 failed." << endl;
  return val;

}	// readMem16 ()


//-----------------------------------------------------------------------------
//! Write a 16-bit value to memory in the target

//! The caller is responsible for error handling.

//! @param[out] val    The value to write.
//! @return  TRUE on success, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::writeMem16 (uint32_t  addr,
		    uint16_t  val) const
{
  return mTarget->writeMem16 (mCoreId, addr, val);

}	// writeMem16 ()


//-----------------------------------------------------------------------------
//! Read a 8-bit value from memory in the target

//! In this version the caller is responsible for error handling.

//! @param[in]  addr    The address to read from.
//! @param[out] val     The value read.
//! @return  TRUE on success, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::readMem8 (uint32_t  addr,
		  uint8_t& val) const
{
  return mTarget->readMem8 (mCoreId, addr, val);

}	// readMem8 ()


//-----------------------------------------------------------------------------
//! Read a 8-bit value from memory in the target

//! In this version we print a warning if the read fails.

//! @param[in]  addr    The address to read from.
//! @return  The value read, undefined if there is a failure.
//-----------------------------------------------------------------------------
uint8_t
Thread::readMem8 (uint32_t  addr) const
{
  uint8_t val;
  if (!mTarget->readMem8 (mCoreId, addr, val))
    cerr << "Warning: readMem8 failed." << endl;
  return val;

}	// readMem8 ()


//-----------------------------------------------------------------------------
//! Write a 8-bit value to memory in the target

//! The caller is responsible for error handling.

//! @param[in]  addr   The address to write to.
//! @return  TRUE on success, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
Thread::writeMem8 (uint32_t  addr,
		   uint8_t   val) const
{
  return mTarget->writeMem8 (mCoreId, addr, val);

}	// writeMem8 ()


//-----------------------------------------------------------------------------
//! Read the value of an Epiphany register from hardware

//! This is just a wrapper for reading memory, since the GPR's are mapped into
//! core memory. In this version the user is responsible for error handling.

//! @param[in]   regnum  The GDB register number
//! @param[out]  regval  The value read
//! @return  True on success, false otherwise
//-----------------------------------------------------------------------------
bool
Thread::readReg (unsigned int regnum,
		 uint32_t&    regval) const
{
  return mTarget->readMem32 (mCoreId, regAddr (regnum), regval);

}	// readReg ()


//-----------------------------------------------------------------------------
//! Read the value of an Epiphany register from hardware

//! This is just a wrapper for reading memory, since the GPR's are mapped into
//! core memory. In this version, we print a warning if the read fails.

//! Overloaded version to return the value directly.

//! @param[in]   regnum  The GDB register number
//! @return  The value read
//-----------------------------------------------------------------------------
uint32_t
Thread::readReg (unsigned int regnum) const
{
  uint32_t regval;
  if (!mTarget->readMem32 (mCoreId, regAddr (regnum), regval))
    cerr << "Warning: readReg failed." << endl;
  return regval;

}	// readReg ()


//-----------------------------------------------------------------------------
//! Write the value of an Epiphany register to hardware

//! This is just a wrapper for writing memory, since the GPR's are mapped into
//! core memory

//! @param[in]  regnum  The GDB register number
//! @param[in]  regval  The value to write
//! @return  True on success, false otherwise
//-----------------------------------------------------------------------------
bool
Thread::writeReg (unsigned int regnum,
		  uint32_t value) const
{
  return  mTarget->writeMem32 (mCoreId, regAddr (regnum), value);

}	// writeReg ()


//-----------------------------------------------------------------------------
//! Read the value of the Core ID

//! A convenience routine and internal to external conversion

//! @return  The value of the Core ID
//-----------------------------------------------------------------------------
CoreId
Thread::readCoreId () const
{
  CoreId *res = new CoreId (readReg (GdbServer::COREID_REGNUM));
  return *res;

}	// readCoreId ()


//-----------------------------------------------------------------------------
//! Read the value of the Core Status (a SCR)

//! A convenience routine and internal to external conversion

//! @return  The value of the Status
//-----------------------------------------------------------------------------
uint32_t
Thread::readStatus () const
{
  return readReg (GdbServer::STATUS_REGNUM);

}	// readStatus ()


//-----------------------------------------------------------------------------
//! Read the value of the Program Counter (a SCR)

//! A convenience routine and internal to external conversion

//! @return  The value of the PC
//-----------------------------------------------------------------------------
uint32_t
Thread::readPc () const
{
  return readReg (GdbServer::PC_REGNUM);

}	// readPc ()


//-----------------------------------------------------------------------------
//! Write the value of the Program Counter (a SCR)

//! A convenience function and internal to external conversion

//! @param[in] addr    The address to write into the PC
//-----------------------------------------------------------------------------
void
Thread::writePc (uint32_t addr)
{
  writeReg (GdbServer::PC_REGNUM, addr);

}	// writePc ()


//-----------------------------------------------------------------------------
//! Read the value of the Link register (a GR)

//! A convenience routine and internal to external conversion

//! @return  The value of the link register
//-----------------------------------------------------------------------------
uint32_t
Thread::readLr () const
{
  return readReg (GdbServer::LR_REGNUM);

}	// readLr ()


//-----------------------------------------------------------------------------
//! Write the value of the Link register (GR)

//! A convenience function and internal to external conversion

//! @param[in] addr    The address to write into the Link register
//-----------------------------------------------------------------------------
void
Thread::writeLr (uint32_t  addr)
{
  writeReg (GdbServer::LR_REGNUM, addr);

}	// writeLr ()


//-----------------------------------------------------------------------------
//! Read the value of the FP register (a GR)

//! A convenience routine and internal to external conversion

//! @return  The value of the frame pointer register
//-----------------------------------------------------------------------------
uint32_t
Thread::readFp () const
{
  return readReg (GdbServer::FP_REGNUM);

}	// readFp ()


//-----------------------------------------------------------------------------
//! Write the value of the Frame register (GR)

//! A convenience function and internal to external conversion

//! @param[in] addr    The address to write into the Frame pointer register
//-----------------------------------------------------------------------------
void
Thread::writeFp (uint32_t addr)
{
  writeReg (GdbServer::FP_REGNUM, addr);

}	// writeFp ()


//-----------------------------------------------------------------------------
//! Read the value of the SP register (a GR)

//! A convenience routine and internal to external conversion

//! @return  The value of the frame pointer register
//-----------------------------------------------------------------------------
uint32_t
Thread::readSp () const
{
  return readReg (GdbServer::SP_REGNUM);

}	// readSp ()


//-----------------------------------------------------------------------------
//! Write the value of the Stack register (GR)

//! A convenience function and internal to external conversion

//! @param[in] addr    The address to write into the Stack pointer register
//-----------------------------------------------------------------------------
void
Thread::writeSp (uint32_t addr)
{
  writeReg (GdbServer::SP_REGNUM, addr);

}	// writeSp ()


//-----------------------------------------------------------------------------
//! Map GDB register number to hardware register memory address

//! @param[in] regnum  GDB register number to look up
//! @return the (local) hardware address in memory of the register
//-----------------------------------------------------------------------------
uint32_t
Thread::regAddr (unsigned int  regnum) const
{
  static const uint32_t regs [GdbServer::NUM_REGS] = {
    TargetControl::R0,
    TargetControl::R0 +   4,
    TargetControl::R0 +   8,
    TargetControl::R0 +  12,
    TargetControl::R0 +  16,
    TargetControl::R0 +  20,
    TargetControl::R0 +  24,
    TargetControl::R0 +  28,
    TargetControl::R0 +  32,
    TargetControl::R0 +  36,
    TargetControl::R0 +  40,
    TargetControl::R0 +  44,
    TargetControl::R0 +  48,
    TargetControl::R0 +  52,
    TargetControl::R0 +  56,
    TargetControl::R0 +  60,
    TargetControl::R0 +  64,
    TargetControl::R0 +  68,
    TargetControl::R0 +  72,
    TargetControl::R0 +  76,
    TargetControl::R0 +  80,
    TargetControl::R0 +  84,
    TargetControl::R0 +  88,
    TargetControl::R0 +  92,
    TargetControl::R0 +  96,
    TargetControl::R0 + 100,
    TargetControl::R0 + 104,
    TargetControl::R0 + 108,
    TargetControl::R0 + 112,
    TargetControl::R0 + 116,
    TargetControl::R0 + 120,
    TargetControl::R0 + 124,
    TargetControl::R0 + 128,
    TargetControl::R0 + 132,
    TargetControl::R0 + 136,
    TargetControl::R0 + 140,
    TargetControl::R0 + 144,
    TargetControl::R0 + 148,
    TargetControl::R0 + 152,
    TargetControl::R0 + 156,
    TargetControl::R0 + 160,
    TargetControl::R0 + 164,
    TargetControl::R0 + 168,
    TargetControl::R0 + 172,
    TargetControl::R0 + 176,
    TargetControl::R0 + 180,
    TargetControl::R0 + 184,
    TargetControl::R0 + 188,
    TargetControl::R0 + 192,
    TargetControl::R0 + 196,
    TargetControl::R0 + 100,
    TargetControl::R0 + 104,
    TargetControl::R0 + 108,
    TargetControl::R0 + 112,
    TargetControl::R0 + 116,
    TargetControl::R0 + 120,
    TargetControl::R0 + 124,
    TargetControl::R0 + 128,
    TargetControl::R0 + 132,
    TargetControl::R0 + 136,
    TargetControl::R0 + 140,
    TargetControl::R0 + 144,
    TargetControl::R0 + 148,
    TargetControl::R63,
    TargetControl::CONFIG,
    TargetControl::STATUS,
    TargetControl::PC,
    TargetControl::DEBUGSTATUS,
    TargetControl::LC,
    TargetControl::LS,
    TargetControl::LE,
    TargetControl::IRET,
    TargetControl::IMASK,
    TargetControl::ILAT,
    TargetControl::ILATST,
    TargetControl::ILATCL,
    TargetControl::IPEND,
    TargetControl::FSTATUS,
    TargetControl::DEBUGCMD,
    TargetControl::RESETCORE,
    TargetControl::CTIMER0,
    TargetControl::CTIMER1,
    TargetControl::MEMSTATUS,
    TargetControl::MEMPROTECT,
    TargetControl::DMA0CONFIG,
    TargetControl::DMA0STRIDE,
    TargetControl::DMA0COUNT,
    TargetControl::DMA0SRCADDR,
    TargetControl::DMA0DSTADDR,
    TargetControl::DMA0AUTO0,
    TargetControl::DMA0AUTO1,
    TargetControl::DMA0STATUS,
    TargetControl::DMA1CONFIG,
    TargetControl::DMA1STRIDE,
    TargetControl::DMA1COUNT,
    TargetControl::DMA1SRCADDR,
    TargetControl::DMA1DSTADDR,
    TargetControl::DMA1AUTO0,
    TargetControl::DMA1AUTO1,
    TargetControl::DMA1STATUS,
    TargetControl::MESHCONFIG,
    TargetControl::COREID,
    TargetControl::MULTICAST,
    TargetControl::CMESHROUTE,
    TargetControl::XMESHROUTE,
    TargetControl::RMESHROUTE
  };

  assert (regnum < GdbServer::NUM_REGS);
  return regs[regnum];

}	// regAddr ()


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
