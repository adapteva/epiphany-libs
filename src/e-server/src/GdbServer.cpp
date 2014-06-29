// GDB RSP server class: Definition.

// Copyright (C) 2008, 2009, Embecosm Limited
// Copyright (C) 2009-2014 Adapteva Inc.

// Contributor: Oleg Raikhman <support@adapteva.com>
// Contributor: Yaniv Sapir <support@adapteva.com>
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

// Note that the Epiphany is a little endian architecture.

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>

#include <execinfo.h>
#include <fcntl.h>
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>
#include <unistd.h>

#include "GdbServer.h"
#include "Thread.h"
#include "Utils.h"

using std::cerr;
using std::cout;
using std::dec;
using std::endl;
using std::flush;
using std::hex;
using std::ostringstream;
using std::pair;
using std::setbase;
using std::setfill;
using std::setw;
using std::stringstream;
using std::vector;


//! @todo We do not handle a user coded BKPT properly (i.e. one that is not
//!       a breakpoint). Effectively it is ignored, whereas we ought to set up
//!       the exception registers and redirect through the trap vector.

//! At this time the stall state of the target is unknown.

//! Logically, there is a mapping as follows
//! - GDB process -> Epiphany workgroup
//! - GDB thread -> core within an Epiphany workgroup

//! We maintain a structure mapping processes and threads to workgroups and
//! cores. Any unallocated cores are held as threads in process 0.

//! We provide some "monitor" commands to deal with this
//! - monitor workgroup <list of coreids> which returns a process id (pid)
//! - monitor load <pid> <image>

//! At present the image format must be SREC. Note also that at present we
//! offer only a subset of the functionality provided by the e-hal
//! library. All cores in a workgroup must have the same image, and loading
//! halts (but does not reset) the cores in the workgroup.



//! Constructor for the GDB RSP server.

//! Create a new packet for passing, a new connection to listen to the client
//! and a new hash table for breakpoints etc.

//! @param[in] _si   All the information about the server.
GdbServer::GdbServer (ServerInfo* _si) :
  mDebugMode (ALL_STOP),
  currentCThread (NULL),
  currentGThread (NULL),
  si (_si),
  fTargetControl (NULL),
  fIsTargetRunning (false)
{
  pkt = new RspPacket (RSP_PKT_MAX);
  rsp = new RspConnection (si);
  mpHash = new MpHash ();

}	// GdbServer ()


//! Destructor
GdbServer::~GdbServer ()
{
  delete mpHash;
  delete rsp;
  delete pkt;

}	// ~GdbServer ()


//-----------------------------------------------------------------------------
//! Listen for RSP requests

//! @param[in] _fTargetControl  Pointer to the target API for the actual
//!                             target.
//-----------------------------------------------------------------------------
void
GdbServer::rspServer (TargetControl* _fTargetControl)
{
  fTargetControl = _fTargetControl;

  initProcesses ();		// Set up the processes and core to thread maps

  // Loop processing commands forever
  while (true)
    {
      // Make sure we are still connected.
      while (!rsp->isConnected ())
	{
	  // Reconnect and stall the processor on a new connection
	  if (!rsp->rspConnect ())
	    {
	      // Serious failure. Must abort execution.
	      cerr << "ERROR: Failed to reconnect to client. Exiting.";
	      exit (EXIT_FAILURE);
	    }
	}

      if (NON_STOP == mDebugMode)
	{
	  // In non-stop mode first check for any notifications we need to
	  // send.
	  if (si->debugTranDetail ())
	    cerr << "DebugTranDetail: Sending RSP client notifications."
		 << endl;

	  rspClientNotifications();
	}

      // Deal with any RSP client request. May return immediately in non-stop
      // mode.
      if (si->debugTranDetail ())
	cerr << "DebugTranDetail: Getting RSP client request." << endl;

      rspClientRequest ();

      // We will have dealt with most requests. In non-stop mode the target
      // will be running, but we have not yet responded. In all-stop mode we
      // have stopped all threads and responded.
      if (si->debugTranDetail ())
	cerr << "DebugTranDetail: RSP client request complete" << endl;
    }
}	// rspServer()


//-----------------------------------------------------------------------------
//! Initialize core to process mapping

//! Processes correspond to GDB work groups. For now, they are set up by the
//! "monitor workgroup" command, which returns a process ID.

//! We start by mapping all threads to process ID IDLE_PID. This can be
//! thought of as the "idle process".

//! Epiphany GDB uses a hard mapping of thread ID to cores. We can only
//! provide this mapping once a connection identifies the cores available.

//! The mapping is:

//!   threadID = (core row + 1) * 100 + core column + 1

//! This means that in decimal the thread ID will read off the core number as
//! decimal row, column, counting from 1. The addition of 1 is needed, because
//! threadId 0 has a special meaning, so cannot be used.
//-----------------------------------------------------------------------------
void
GdbServer::initProcesses ()
{
  assert (fTargetControl);		// Just in case of a stupid connection

  // Create the idle process
  mNextPid = IDLE_PID;
  mIdleProcess = new ProcessInfo (mNextPid);
  mProcessList[mNextPid] = mIdleProcess;
  mNextPid++;

  // Initialize a thread for each core. A thread is referenced by its thread
  // ID, and from the thread we can get the coreId (accessor). We need a
  // reverse map, to allow us to gte from core to thread ID if we ever need to
  // in the future.
  for (vector <CoreId>::iterator it = fTargetControl->coreIdBegin ();
       it!= fTargetControl->coreIdEnd ();
       it++)
    {
      CoreId coreId = *it;
      int tid = (coreId.row () + 1) * 100 + coreId.col () + 1;
      Thread *thread = new Thread (coreId, fTargetControl, si, tid);

      mThreads[tid] = thread;
      mCore2Tid [coreId] = tid;

      // Add to idle process
      assert (mIdleProcess->addThread (thread));
    }

  mCurrentProcess = mIdleProcess;

}	// initProcesses ()


//-----------------------------------------------------------------------------
//! Attach to the target

//! If not already halted, the target will be halted. We send an appropriate
//! stop packet back to the client.

//! @todo What should we really do if the target fails to halt?

//! @note  The target should *not* be reset when attaching.

//! @param[in] process  The process to which we attach
//-----------------------------------------------------------------------------
void
GdbServer::rspAttach (ProcessInfo* process)
{
  bool isHalted = true;

  for (set <Thread *>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    {
      Thread *thread = *it;
      isHalted &= thread->halt ();

      if (thread->isIdle ())
	{
	  if (thread->activate ())
	    {
	      if (si->debugStopResumeDetail ())
		cerr << "DebugStopResumeDetail: Thread " << thread
		     << " idle on attach: forced active." << endl;
	    }
	  else
	    {
	      if (si->debugStopResumeDetail ())
		cerr << "DebugStopResumeDetail: Thread " << thread
		     << " idle on attach: failed to force active." << endl;
	    }
	}
    }

  // @todo Does this belong here. Is TARGET_SIGNAL_HUP correct?
  if (!isHalted)
    rspReportException (-1 /*all threads */ , TARGET_SIGNAL_HUP);

}	// rspAttach ()


//-----------------------------------------------------------------------------
//! Detach from a process

//! Restart all threads in the process, *unless* it is the idle process (why
//! waste CPU with it).

//! @param[in] process  The process from which we detach
//-----------------------------------------------------------------------------
void
GdbServer::rspDetach (ProcessInfo* process)
{
  if (IDLE_PID != process->pid ())
    {
      for (set <int>::iterator it = process->threadBegin ();
	   it != process->threadEnd ();
	   it++)
	{
	  Thread* thread = *it;
	  thread->resume ();
	}
    }
}	// rspDetach ()


//-----------------------------------------------------------------------------
//! Deal with a request from the GDB client session

//! In general, apart from the simplest requests, this function replies on
//! other functions to implement the functionality.

//! @note It is the responsibility of the recipient to delete the packet when
//!       it is finished with. It is permissible to reuse the packet for a
//!       reply.

//! @todo Is this the implementation of the 'D' packet really the intended
//!       meaning? Or does it just mean that only vAttach will be recognized
//!       after this?

//! @param[in] pkt  The received RSP packet
//-----------------------------------------------------------------------------
void
GdbServer::rspClientRequest ()
{
  switch (mDebugMode)
    {
    case NON_STOP:

      // Non-blocking read of a packet (which therefore may be empty)
      if (!rsp->getPktNonBlock (pkt))
	{
	  rspDetach (mCurrentProcess);
	  rsp->rspClose ();		// Comms failure
	  return;
	}

      if (0 == pkt->getLen ())
	return;				// No packet found

      break;

    case ALL_STOP:

      // Blocking read of a packet. Nothing happens until we get a packet.
      if (!rsp->getPkt (pkt))
	{
	  rspDetach (mCurrentProcess);
	  rsp->rspClose ();		// Comms failure
	  return;
	}

      break;
    }

  switch (pkt->data[0])
    {
    case '!':
      // Request for extended remote mode
      pkt->packStr ("");	// Empty = not supported
      rsp->putPkt (pkt);
      break;

    case '?':
      // Return last signal ID
      rspReportException (-1 /*all threads */ , TARGET_SIGNAL_TRAP);
      break;

    case 'A':
      // Initialization of argv not supported
      cerr << "Warning: RSP 'A' packet not supported: ignored" << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      break;

    case 'b':
      // Setting baud rate is deprecated
      cerr << "Warning: RSP 'b' packet is deprecated and not "
	<< "supported: ignored" << endl;
      break;

    case 'B':
      // Breakpoints should be set using Z packets
      cerr << "Warning: RSP 'B' packet is deprecated (use 'Z'/'z' "
	<< "packets instead): ignored" << endl;
      break;

    case 'c':
      // Continue
      rspContinue ();
      break;

    case 'C':
      // Continue with signal (in the packet)
      rspContinueWithSignal ();
      break;

    case 'd':
      // Disable debug using a general query
      cerr << "Warning: RSP 'd' packet is deprecated (define a 'Q' "
	<< "packet instead: ignored" << endl;
      break;

    case 'D':
      // Detach GDB. Do this by closing the client. The rules say that
      // execution should continue, so unstall the processor.
      rspDetach (mCurrentProcess);
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
      rsp->rspClose ();

      break;

    case 'F':

      // Parse the F reply packet
      rspFileIOreply ();

      // For all-stop mode we assume all threads need restarting.
      resumeAllThreads ();

      break;

    case 'g':
      rspReadAllRegs ();
      break;

    case 'G':
      rspWriteAllRegs ();
      break;

    case 'H':
      rspSetThread ();
      break;

    case 'i':
    case 'I':
      // Single cycle step not supported.
      // @todo How should we respond to this?
      cerr << "ERROR: RSP cycle stepping not supported: ignored." << endl;
      break;

    case 'k':
      rspDetach (mCurrentProcess);
      rsp->rspClose ();			// Close the connection.
      // Reset to the initial state to prevent reporting to the disconnected
      // client.
      fIsTargetRunning = false;

      break;

    case 'm':
      // Read memory (symbolic)
      rspReadMem ();
      break;

    case 'M':
      // Write memory (symbolic)
      rspWriteMem ();
      break;

    case 'p':
      // Read a register
      rspReadReg ();
      break;

    case 'P':
      // Write a register
      rspWriteReg ();
      break;

    case 'q':
      // Any one of a number of query packets
      rspQuery ();
      break;

    case 'Q':
      // Any one of a number of set packets
      rspSet ();
      break;

    case 'r':
      // Reset the system. Deprecated (use 'R' instead)
      cerr << "Warning: RSP 'r' packet is deprecated (use 'R' "
	<< "packet instead): ignored" << endl;
      break;

    case 'R':
      // Restart the program being debugged.
      rspRestart ();
      break;

    case 's':
      // Single step one machine instruction. This should never be used, since
      // we only support software single step.
      // @todo How should we respond to this?
      cerr << "ERROR: RSP 's' packet not supported: ignored." << endl;
      break;

    case 'S':
      // Single step one machine instruction with signal. This should never be
      // used, since we only support software single step.
      // @todo How should we respond to this?
      cerr << "ERROR: RSP 's' packet not supported: ignored." << endl;
      break;

    case 't':
      // Search. This is not well defined in the manual and for now we don't
      // support it. No response is defined.
      cerr << "Warning: RSP 't' packet not supported: ignored" << endl;
      break;

    case 'T':
      // Is the thread alive.
      rspIsThreadAlive ();
      break;

    case 'v':
      // Any one of a number of packets to control execution
      rspVpkt ();
      break;

    case 'X':
      // Write memory (binary)
      rspWriteMemBin ();
      break;

    case 'z':
      // Remove a breakpoint/watchpoint.
      rspRemoveMatchpoint ();
      break;

    case 'Z':
      // Insert a breakpoint/watchpoint.
      rspInsertMatchpoint ();
      break;

    default:
      // Unknown commands are ignored
      cerr << "Warning: Unknown RSP request" << pkt->data << endl;
      break;
    }
}				// rspClientRequest()


//-----------------------------------------------------------------------------
//! Get a thread from a thread ID

//! This is the reverse lookup. Should only be called with a known good thread
//! ID.

//! @param[in] tid  Thread ID
//! @return  Thread
//-----------------------------------------------------------------------------
Thread*
GdbServer::getThread (int  tid)
{
  map <int, Thread*>::iterator it = mThreads.find(tid);

  assert (it != mThreads.end ());
  return it->second;

}	// getThread ()


//-----------------------------------------------------------------------------
//! Wait for one or more threads to halt in the given process

//! We first verify that the stoppedThreads we are told about really are
//! stopped, then we look at all the other threads to see if they are stopped

//! @todo  Is this ever called in NON_STOP mode?

//! @param[in] process          The process we are examining
//! @param[out] stoppedThreads  The set of all stopped threads we find.
//! @return  The first stopped thread we find, or NULL if were interrupted
//!          before any threads were found to have stopped.
//-----------------------------------------------------------------------------
Thread *
GdbServer::waitAllThreads (ProcessInfo*   process,
			   set <Thread*>  stoppedThreads)
{
  // We must wait until at least one thread halts.
  while (true)
    {
      stoppedThreads.clear ();

      for (set <int>::iterator it = mCurrentProcess->threadBegin ();
	   it != mCurrentProcess->threadEnd ();
	   it++)
	{
	  Thread *thread = *it;
	  if (thread->isHalted ())
	    {
	      thread->restoreIVT ();		// OK to duplicate
	      stoppedThreads.insert (thread);
	    }
	}

      if (!stoppedThreads.empty ())
	break;				// Found a stopped thread

      // Check for Ctrl-C
      if (si->debugCtrlCWait())
	cerr << "DebugCtrlCWait: Check for Ctrl-C" << endl;

      if (rsp->getBreakCommand ())
	{
	  if (stoppedThreads.empty ())
	    return NULL;
	  else
	    return *(stoppedThreads.begin ());
	}

      if (si->debugCtrlCWait())
	cerr << "DebugCtrlCWait: check for CTLR-C done" << endl;

      Utils::microSleep (WAIT_INTERVAL);
    }

  return *(stoppedThreads.begin ());

}	// waitAllThreads ()


//-----------------------------------------------------------------------------
//! Halt a thread

//! @param[in] thread  The thread to halt.
//! @return  TRUE if the thread halts, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
GdbServer::haltThread (Thread *thread)
{
  thread->restoreIVT ();		// OK to duplicate
  return thread->halt ();

}	// haltThread ()


//-----------------------------------------------------------------------------
//! Halt all threads in the given process.

//! @param[in] process  The process for which threads should be halted.
//! @return  TRUE if all threads halt, FALSE otherwise
//-----------------------------------------------------------------------------
bool
GdbServer::haltAllThreads (ProcessInfo* process)
{
  bool allHalted = true;

  for (set <int>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    {
      allHalted &= haltThread (*it);
    }

  return allHalted;

}	// haltAllThreads ()


//-----------------------------------------------------------------------------
//! Continue execution of a thread with a specific address

//! This is a convenience for the main thread routine

//! @param[in] thread  The thread to continue.
//! @param[in] addr    The address at which to continue.
//! @param[in] sig     The exception to use. Defaults to
//!                    TARGET_SIGNAL_NONE.  Currently ignored
//! @return  TRUE if the thread resumed OK.
//-----------------------------------------------------------------------------
bool
GdbServer:continueThread (Thread*       thread,
			  uint32_t      addr,
			  TargetSignal  sig)
{
  // Set the address and continue the thread
  thread->writePc (addr);
  return continueThread (thread, sig);

}	// continueThread ()


//-----------------------------------------------------------------------------
//! Continue execution of a thread at the PC

//! This is only half of continue processing - we'll worry about the thread
//! stopping later.

//! @param[in] thread  The thread to continue.
//! @param[in] sig     The exception to use. Defaults to
//!                    TARGET_SIGNAL_NONE.  Currently ignored
//! @return  TRUE if the thread resumed OK.
//-----------------------------------------------------------------------------
bool
GdbServer:continueThread (Thread*       thread,
			  TargetSignal  sig)
{
  if (si->debugStopResume ())
    {
      cerr << "DebugStopResume: continueThread (thread = " << thread
	   << ", PC = " << intStr (thread->readPC (), 16, 8) << ", sig = "
	   << sig << ")." << endl;
    }

  // Set breakpoints in the IVT
  assert (thread->breakpointIVT ());
  return thread->resume ();

}	// continueThread ()


//-----------------------------------------------------------------------------
//! Resume all threads in the given process.

//! @param[in] process  The process for which threads should be halted.
//! @return  TRUE if all threads resume, FALSE otherwise
//-----------------------------------------------------------------------------
bool
GdbServer::continueAllThreads (ProcessInfo* process)
{
  bool allResumed = true;

  for (set <int>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    allResumed &= continueThread (*it);

  return allResumed;

}	// continueAllThreads ()


//-----------------------------------------------------------------------------
//! Send a reply reporting the various stopped threads

//! By this stage, all threads should have been stopped. The supplied list, is
//! those threads which were found to have been stopped by themselves

//! Since this is all-stop mode, we will use a S packet to report all
//! threads. The signal we report is selected from the following (in order of
//! precdence:

//!   - TARGET_SIGNAL_NONE if there are no stopped threads (we presumably got
//!     ctrl-C)

//!   - the signal of the current C thread if it is in the list of stopped
//!     threads.

//!   - the signal of the first thread in the list of stopped threads.

//! @note Only for use in all-stop mode.

//! @param[in] currentProcess  The current process.
//! @param[in] stoppedThreads  The set of all threads to report.
//-----------------------------------------------------------------------------
void
GdbServer::rspReportStopped (ProcessInfo*  process,
			     set<Thread*>  stoppedThreads)
{
  assert (ALL_STOP == mDebugMode);

  if (0 == stoppedThreads.size ())
    sig = TARGET_SIGNAL_NONE;
  else if (stoppedThreads.find (currentCThread) != stoppedThreads.end ())
    sig = findStopReason (currentCThread);
  else
    sig = findStopReason (*(stoppedThreads.begin ()));

  if (si->debugStopResume ())
    cerr << "DebugStopResume: Report stopped  for all threads with GDB signal "
	 << sig << endl;

  ostringstream oss;
  oss << "S" << Utils::intStr (sig, 16, 2);

  pkt->packStr (oss.str ().c_str ());
  rsp->putPkt (pkt);

  // Mark all threads as reported
  for (set <int>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    {
      Thread* thread = *it;
      thread->markReported ();
    }

}	// rspReportStopped ()


//-----------------------------------------------------------------------------
//! Why did we stop?

//! We know the thread is halted. Did it halt immediately after a BKPT, TRAP
//! or IDLE instruction. Not quite as easy as it seems, since TRAP may often be
//! followed by NOP. These are the signals returned under various
//! circumstances.

//!   - BKPT instruction. Return TARGET_SIGNAL_TRAP.

//!   - IDLE instruction. Return TARGET_SIGNAL_STOP.

//!   - If in the IVT, then return a signal appropriate to the specific vector
//!     entry. Note that we only do this, if we have not already determined
//!     the instruction was a BKPT instruction (a breakpoint in the IVT).

//!       - Sync (aka reset). I doubt this is possible, but if it happened,
//!         TARGET_SIGNAL_PWR would seem appropriate.

//!       - Software exception. Depends on the EXCAUSE field in the STATUS
//!         register

//!           - Unimplemented. Return TARGET_SIGNAL_ILL.

//!           - SWI. Return TARGET_SIGNAL_INT.

//!           - Unaligned. Return TARGET_SIGNAL_BUS.

//!           - Illegal access. Return TARGET_SIGNAL_SEGV.

//!           - FPU exception. Return TARGET_SIGNAL_FPE.

//!       - Memory fault. Return TARGET_SIGNAL_SEGV.

//!       - Timer0 interrupt. Return TARGET_SIGNAL_REALTIME_33.

//!       - Timer1 interrupt. Return TARGET_SIGNAL_REALTIME_34.

//!       - Message interrupt. Return TARGET_SIGNAL_REALTIME_35.

//!       - DMA0 interrupt. Return TARGET_SIGNAL_REALTIME_36.

//!       - DMA1 interrupt. Return TARGET_SIGNAL_REALTIME_37.

//!       - WAND interrupt. Return TARGET_SIGNAL_REALTIME_38.

//!       - User interrupt. Return TARGET_SIGNAL_REALTIME_39.

//!   - TRAP instruction. The result depends on the trap value.

//!       - 0-2 or 6. Reserved, so should not be use. Return
//!         TARGET_SIGNAL_SYS.

//!       - 3. Program exit. Return TARGET_SIGNAL_KILL.

//!       - 4. Success. Return TARGET_SIGNAL_USR1.

//!       - 5. Failure. Return TARGET_SIGNAL_USR2.

//!       - 7. System call. If the value in R3 is valid, return
//!         TARGET_SIGNAL_EMT, otherwise return TARGET_SIGNAL_SYS.

//!   - Anything else. We must have been stopped externally, so return
//!     TARGET_SIGNAL_NONE.

//! @param[in] thread  The thread to consider.
//! @return  The appropriate signal as described above.
//-----------------------------------------------------------------------------
TargetSignal
GdbServer::findStopReason (Thread *thread)
{
  assert (thread->isHalted ());

  // First see if we just hit a breakpoint or IDLE. Fortunately IDLE, BREAK,
  // TRAP and NOP are all 16-bit instructions.
  uint32_t  pc = thread->readPc () - SHORT_INSTRLEN;
  uint16_t  instr16 = thread->readMem16 (pc);

  if (BKPT_INSTR == instr16)
    return  TARGET_SIGNAL_TRAP;

  if (IDLE_INSTR == instr16)
    return  TARGET_SIGNAL_STOP;

  switch (pc)
    {
    case TargetControl::IVT_SYNC:
      // Don't expect to get this
      return TARGET_SIGNAL_PWR;

    case TargetControl::IVT_SWE:
      return thread->getException ();

    case TargetControl::IVT_PROT:
      return TARGET_SIGNAL_SEGV;

    case TargetControl::IVT_TIMER0:
      return TARGET_SIGNAL_REALTIME_33;

    case TargetControl::IVT_TIMER1:
      return TARGET_SIGNAL_REALTIME_34;

    case TargetControl::IVT_MSG:
      return TARGET_SIGNAL_REALTIME_35;

    case TargetControl::IVT_DMA0:
      return TARGET_SIGNAL_REALTIME_36;

    case TargetControl::IVT_DMA1:
      return TARGET_SIGNAL_REALTIME_37;

    case TargetControl::IVT_WAND:
      return TARGET_SIGNAL_REALTIME_38;

    case TargetControl::IVT_USER:
      return TARGET_SIGNAL_REALTIME_39;

    default:
      // Not in the IVT
      break;
    }

  // Is it a TRAP? Find the first preceding non-NOP instruction
  while (NOP_INSTR == instr16)
    {
      pc -= SHORT_INSTRLEN;
      instr16 = thread->readMem16 (pc);
    }

  // If it isn't a TRAP, something has gone 'orribly wrong.
  if (getOpcode10 (instr16) == TRAP_INSTR)
    return instr16;
  else
    return NOP_INSTR;

}	// getStopInstr ()


//-----------------------------------------------------------------------------
//! Handle a RSP continue request

//! This version is typically used for the 'c' packet, to continue without
//! signal.
//-----------------------------------------------------------------------------
void
GdbServer::rspContinue ()
{
  bool      haveAddr;		// Was an address supplied
  uint32_t  addr;		// Address supplied in packet

  // Get an address if we have one
  if (1 == sscanf (pkt->data, "c%" SCNx32, &addr))
    haveAddr = true;
  else if (0 == strcmp ("c", pkt->data))
    haveAddr = false;
  else
    {
      cerr << "Warning: RSP continue address " << pkt->data
	<< " not recognized: ignored" << endl;
      haveAddr = false;		// Default will use current PC
    }

  rspContinueGeneric (currentCThread, haveAddr, addr, TARGET_SIGNAL_NONE);

}	// rspContinue ()


//-----------------------------------------------------------------------------
//! Handle a RSP continue with signal request

//! The signal is in the packet
//-----------------------------------------------------------------------------
void
GdbServer::rspContinueWithSignal ()
{
  bool          haveAddr;	// Was an address supplied
  uint32_t      addr;		// Address supplied in packet
  TargetSignal  sig;		// Signal to use

  // Get an address if we have one
  if (2 == sscanf (pkt->data, "C%d;%" SCNx32, (int *)(&sig), &addr))
    haveAddr = true;
  else if (1 == sscanf (pkt->data, "C%d", (int *)(&sig)))
    haveAddr = false;
  else
    {
      cerr << "Warning: RSP continue signal/address " << pkt->data
	<< " not recognized: ignored" << endl;
      haveAddr = false;		// Default will use current PC
      sig = TARGET_SIGNAL_NONE;
    }

  rspContinueGeneric (currentCThread, haveAddr, addr, sig);

}	// rspContinueWithSignal ()


//-----------------------------------------------------------------------------
//! Generic processing of a continue request

//! Handles whether we should continue single thread or all threads.

//! @param[in] thread    The thread to continue, NULL means all threads
//! @param[in] haveAddr  TRUE if we have supplied an address to continue from,
//!                      other use the PC of the thread being continued.
//! @param[in] addr      Address to use if haveAddr is TRUE.
//! @param[in] sig       Signal to restart with (TARGET_SIGNAL_NONE) will be
//!                      used if there is no such signal.
//-----------------------------------------------------------------------------
void
GdbServer::rspContinueGeneric (Thread*       thread,
			       bool          haveAddr,
			       uint32_t      addr,
			       TargetSignal  sig)
{
  cerr << "Warning: Using c/C packets with threads deprecated." << endl;

  // NULL thread means all threads
  if (NULL == thread)
    {
      assert (mCurrentProcess);			// Sanity check

      for (set <int>::iterator tit = mCurrentProcess->threadBegin ();
	   tit != mCurrentProcess->threadEnd ();
	   tit++)
	{
	  if (haveAddr)
	    rspContinueThread (thread, addr, sig);
	  else
	    rspContinueThread (thread, (*tit)->readPc (), sig);
	}
    }
  else
    {
      if (haveAddr)
	rspContinueThread (thread, addr, sig);
      else
	rspContinueThread (thread, thread->readPc (), sig);
    }

  if (NON_STOP = mDebugMode)
    {
      // Report stopped threads later.
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else
    {
      set <Thread*> stoppedThreads;
      waitAllThreads (mCurrentProcess, stoppedThreads);
      haltAllThreads (mCurrentProcess);
      rspReportStopped (mCurrentProcess, stoppedThreads);
    }
}	// rspContinueGeneric ()


//-----------------------------------------------------------------------------
//! Stop in response to a ctrl-C

//! We just halt everything. Let GDB worry about it later.

//! @todo Should we see if we have an actual interrupt?
//-----------------------------------------------------------------------------
void
GdbServer::rspSuspend ()
{
  // Halt all threads.
  if (!haltAllThreads ())
    cerr << "Warning: suspend failed to halt all threads." << endl;

  // Report to gdb the target has been stopped
  rspReportException (-1 /*all threads */ , TARGET_SIGNAL_HUP);

}	// rspSuspend ()


//-----------------------------------------------------------------------------
//! Reply to F packet
/*
 * Fretcode,errno,Ctrl-C flag;call-specific attachment’ retcode is the return code of the system call as hexadecimal value.
 * errno is the errno set by the call, in protocol-specific representation.
 * This parameter can be omitted if the call was successful. Ctrl-C flag is only sent if the user requested a break.
 * In this case, errno must be sent as well, even if the call was successful.
 * The Ctrl-C flag itself consists of
 * the character ‘C’: F0,0,C or, if the call was interrupted before the host call has been performed: F-1,4,C
 * assuming 4 is the protocol-specific representation of EINTR.
 */
//-----------------------------------------------------------------------------
void
GdbServer::rspFileIOreply ()
{
  Thread* thread = currentCThread;

  long int result_io = -1;
  long int host_respond_error_code;

  if (2 ==
      sscanf (pkt->data, "F%lx,%lx", &result_io, &host_respond_error_code))
    {
      //write to r0
      thread->writeReg (R0_REGNUM + 0, result_io);

      //write to r3 error core
      thread->writeReg (R0_REGNUM + 3, host_respond_error_code);
      if (si->debugStopResumeDetail ())
	cerr << dec <<
	  " remote io done " << result_io << "error code" <<
	  host_respond_error_code << endl;

    }
  else if (1 == sscanf (pkt->data, "F%lx", &result_io))
    {

      if (si->debugStopResumeDetail ())
	cerr << dec <<
	  " remote io done " << result_io << endl;

      //write to r0
      thread->writeReg (R0_REGNUM + 0, result_io);
    }
  else
    {
      cerr << " remote IO operation fail " << endl;
    }
}

//-----------------------------------------------------------------------------
//! Redirect the SDIO to client GDB.

//! The requests are sent using F packets. The open, write, read and close
//! system calls are supported

//! At this point all threads have been halted, and we have set
//! fIsTargetRunning to FALSE.

//! @param[in] thread   Thread making the I/O request
//! @param[in] trap  The number of the trap.
//-----------------------------------------------------------------------------
void
GdbServer::redirectStdioOnTrap (Thread *thread,
				uint8_t trap)
{
  // The meaning of the TRAP codes. These are mostly not documented. Really
  // for most of these, use TRAP_SYSCALL.
  static const uint8_t  TRAP_WRITE   = 0;
  static const uint8_t  TRAP_READ    = 1;
  static const uint8_t  TRAP_OPEN    = 2;
  static const uint8_t  TRAP_EXIT    = 3;
  static const uint8_t  TRAP_PASS    = 4;
  static const uint8_t  TRAP_FAIL    = 5;
  static const uint8_t  TRAP_CLOSE   = 6;
  static const uint8_t  TRAP_SYSCALL = 7;

  // The system calls we support. We arguably should link to the libgloss
  // header to pick these up, but we have no guarantee we know where that
  // header is. Safest is to put them locally here where they are to be used.
  static const uint32_t  SYS_open     =  2;
  static const uint32_t  SYS_close    =  3;
  static const uint32_t  SYS_read     =  4;
  static const uint32_t  SYS_write    =  5;
  static const uint32_t  SYS_lseek    =  6;
  static const uint32_t  SYS_unlink   =  7;
  static const uint32_t  SYS_fstat    = 10;
  static const uint32_t  SYS_stat     = 15;

  // We will almost certainly want to use these registers, so get them once
  // and for all.
  uint32_t  r0 = thread->readReg (R0_REGNUM + 0);
  uint32_t  r1 = thread->readReg (R0_REGNUM + 1);
  uint32_t  r2 = thread->readReg (R0_REGNUM + 2);
  uint32_t  r3 = thread->readReg (R0_REGNUM + 3);

  ostringstream oss;

  char *buf;
  //int result_io;
  unsigned int k;

  char fmt[2048];

  switch (trap)
    {
    case TRAP_WRITE:
      // Surely we prefer the syscall
      hostWrite ("TRAP 0", r0, r1, r2);
      return;

    case TRAP_READ:
      if (si->debugTrapAndRspCon ())
	cerr << dec << " Trap 1 read " << endl;	/*read(chan, addr, len) */
      r0 = thread->readReg (R0_REGNUM + 0);		//chan
      r1 = thread->readReg (R0_REGNUM + 1);		//addr
      r2 = thread->readReg (R0_REGNUM + 2);		//length

      if (si->debugTrapAndRspCon ())
	cerr << dec <<
	  " read from chan " << r0 << " bytes " << r2 << endl;


      sprintf ((pkt->data), "Fread,%lx,%lx,%lx", (unsigned long) r0,
	       (unsigned long) r1, (unsigned long) r2);
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);

      break;
    case TRAP_OPEN:
      r0 = thread->readReg (R0_REGNUM + 0);		//filepath
      r1 = thread->readReg (R0_REGNUM + 1);		//flags

      if (si->debugTrapAndRspCon ())
	cerr << dec <<
	  " Trap 2 open, file name located @" << hex << r0 << dec << " (mode)"
	  << r1 << endl;

#define MAX_FILE_NAME_LENGTH (256*4)
      for (k = 0; k < MAX_FILE_NAME_LENGTH - 1; k++)
	{
	  uint8_t val_;
	  thread->readMem8 (r0 + k, val_);
	  if (val_ == '\0')
	    {
	      break;
	    }
	}

      //Fopen, pathptr/len, flags, mode
      sprintf ((pkt->data), "Fopen,%lx/%d,%lx,%lx", (unsigned long) r0, k,
	       (unsigned long) r1 /*O_WRONLY */ ,
	       (unsigned long) (S_IRUSR | S_IWUSR));
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);
      break;

    case TRAP_EXIT:
      if (si->debugTrapAndRspCon ())
	cerr << dec <<
	  " Trap 3 exiting .... ??? " << endl;
      r0 = thread->readReg (R0_REGNUM + 0);		//status
      //cerr << " The remote target got exit() call ... no OS -- ignored" << endl;
      //exit(4);
      rspReportException (-1 /*all threads */ , TARGET_SIGNAL_QUIT);
      break;
    case TRAP_PASS:
      cerr << " Trap 4 PASS " << endl;
      rspReportException (-1 /*all threads */ , TARGET_SIGNAL_TRAP);
      break;
    case TRAP_FAIL:
      cerr << " Trap 5 FAIL " << endl;
      rspReportException (-1 /*all threads */ , TARGET_SIGNAL_QUIT);
      break;
    case TRAP_CLOSE:
      r0 = thread->readReg (R0_REGNUM + 0);		//chan
      if (si->debugTrapAndRspCon ())
	cerr << dec <<
	  " Trap 6 close: " << r0 << endl;
      sprintf ((pkt->data), "Fclose,%lx", (unsigned long) r0);
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);
      break;
    case TRAP_SYSCALL:

      if (NULL != si->ttyOut ())
	{

	  //cerr << " Trap 7 syscall -- ignored" << endl;
	  if (si->debugTrapAndRspCon ())
	    cerr << dec <<
	      " Trap 7 " << endl;
	  r0 = thread->readReg (R0_REGNUM + 0);	// buf_addr
	  r1 = thread->readReg (R0_REGNUM + 1);	// fmt_len
	  r2 = thread->readReg (R0_REGNUM + 2);	// total_len

	  //fprintf(stderr, " TRAP_SYSCALL %x %x", PARM0,PARM1);

	  //cerr << " buf " << hex << r0 << "  " << r1 << "  " << r2 << dec << endl;

	  buf = (char *) malloc (r2);
	  for (unsigned k = 0; k < r2; k++)
	    {
	      uint8_t val_;

	      thread->readMem8 (r0 + k, val_);
	      buf[k] = val_;
	    }


	  strncpy (fmt, buf, r1);
	  fmt[r1] = '\0';

	  fprintf (si->ttyOut (), fmt, buf + r1 + 1);

	  thread->resume ();
	}
      else
	{

	  r0 = thread->readReg (R0_REGNUM + 0);
	  r1 = thread->readReg (R0_REGNUM + 1);
	  r2 = thread->readReg (R0_REGNUM + 2);
	  r3 = thread->readReg (R0_REGNUM + 3);	//SUBFUN;

	  switch (r3)
	    {

	    case SYS_close:

	      //int close(int fd);
	      //‘Fclose, fd’
	      sprintf ((pkt->data), "Fclose,%lx", (unsigned long) r0);
	      break;

	    case SYS_open:

	      //asm_syscall(file, flags, mode, SYS_open);
	      for (k = 0; k < MAX_FILE_NAME_LENGTH - 1; k++)
		{
		  uint8_t val_;
		  thread->readMem8 (r0 + k, val_);
		  if (val_ == '\0')
		    {
		      break;
		    }
		}

	      //Fopen, pathptr/len, flags, mode
	      sprintf ((pkt->data), "Fopen,%lx/%d,%lx,%lx",
		       (unsigned long) r0, k,
		       (unsigned long) r1 /*O_WRONLY */ , (unsigned long) r2);
	      break;

	    case SYS_read:
	      //int read(int fd, void *buf, unsigned int count);
	      // ‘Fread, fd, bufptr, count’
	      //asm_syscall(fildes, ptr, len, SYS_read);

	      sprintf ((pkt->data), "Fread,%lx,%lx,%lx", (unsigned long) r0,
		       (unsigned long) r1, (unsigned long) r2);
	      break;


	    case SYS_write:
	      //int write(int fd, const void *buf, unsigned int count);
	      //‘Fwrite, fd, bufptr, count’
	      //asm_syscall(file, ptr, len, SYS_write);
	      sprintf ((pkt->data), "Fwrite,%lx,%lx,%lx", (unsigned long) r0,
		       (unsigned long) r1, (unsigned long) r2);
	      break;


	    case SYS_lseek:
	      //‘Flseek, fd, offset, flag’
	      //asm_syscall(fildes, offset, whence, ..)
	      sprintf ((pkt->data), "Flseek,%lx,%lx,%lx", (unsigned long) r0,
		       (unsigned long) r1, (unsigned long) r2);
	      break;

	    case SYS_unlink:
	      //‘Funlink, pathnameptr/len’
	      // asm_syscall(name, NULL, NULL, SYS_unlink);
	      for (k = 0; k < MAX_FILE_NAME_LENGTH - 1; k++)
		{
		  uint8_t val_;
		  thread->readMem8 (r0 + k, val_);
		  if (val_ == '\0')
		    {
		      break;
		    }
		}
	      sprintf ((pkt->data), "Funlink,%lx/%d", (unsigned long) r0, k);
	      break;

	    case SYS_stat:
	      //‘Fstat, pathnameptr/len, bufptr’
	      //_stat(const char *file, struct stat *st)
	      //asm_syscall(file, st, NULL, SYS_stat);
	      for (k = 0; k < MAX_FILE_NAME_LENGTH - 1; k++)
		{
		  uint8_t val_;
		  thread->readMem8 (r0 + k, val_);
		  if (val_ == '\0')
		    {
		      break;
		    }
		}
	      sprintf ((pkt->data), "Fstat,%lx/%d,%lx", (unsigned long) r0, k,
		       (unsigned long) r1);
	      break;

	    case SYS_fstat:
	      //‘Ffstat, fd, bufptr’
	      //_fstat(int fildes, struct stat *st)
	      //asm_syscall(fildes, st, NULL, SYS_fstat);

	      sprintf ((pkt->data), "Ffstat,%lx,%lx", (unsigned long) r0,
		       (unsigned long) r1);
	      if (si->debugTrapAndRspCon ())
		cerr <<
		  dec << "SYS_fstat fildes " << hex << r0 << " struct stat * "
		  << r1 << dec << endl;
	      break;

	    default:
	      cerr << "ERROR: Trap 7 --- unknown SUBFUN " << r3 << endl;
	      break;
	    }
	  if (si->debugTrapAndRspCon ())
	    cerr << "Trap 7: "
	      << (pkt->data) << endl;

	  pkt->setLen (strlen (pkt->data));
	  rsp->putPkt (pkt);

	  //rspReportException(readPc() /* PC no trap found */, 0 /* all threads */, TARGET_SIGNAL_QUIT);
	}

      break;
    default:
      break;
    }
}


//-----------------------------------------------------------------------------
//! Handle a host call to write

//! This may come from TRAP 0 or TRAP 7 and SYS_write.

//! @param[in] source  Where this came from (TRAP or Syscall)
//! @param[in] chan    Channel to write to.
//! @param[in] addr    Address of bytes to write.
//! @param[in] len     Number of bytes to write.
//-----------------------------------------------------------------------------
void
GdbServer::hostWrite (const char* intro,
		      uint32_t    chan,
		      uint32_t    addr,
		      uint32_t    len)
{
  ostringstream oss;

  if (si->debugTrapAndRspCon ())
    cerr << "DebuTrapAndRspCon: " << intro << " write (0x"
	 << Utils::intStr (chan, 16, 8) << ", 0x"
	 << Utils::intStr (addr, 16, 8) << ", 0x"
	 << Utils::intStr (len, 16, 8) << ")." << endl;

  oss << "Fwrite," << Utils::intStr (chan, 16, 8) << ","
      << Utils::intStr (addr, 16, 8) << "," << Utils::intStr (len, 16, 8);
  pkt->packStr (oss.str ().c_str ());
  rsp->putPkt (pkt);

}	// hostWrite ()


//-----------------------------------------------------------------------------
//! Handle a RSP read all registers request

//! The registers follow the GDB sequence for Epiphany: GPR0 through GPR63,
//! followed by all the Special Control Registers. Each register is returned
//! as a sequence of bytes in target endian order.

//! Each byte is packed as a pair of hex digits.
//-----------------------------------------------------------------------------
void
GdbServer::rspReadAllRegs ()
{
  assert (NULL != currentGThread);

  // Start timing if debugging
  if (si->debugStopResumeDetail ())
    fTargetControl->startOfBaudMeasurement ();

  // Get each reg
  for (unsigned int r = 0; r < NUM_REGS; r++)
    {
      uint32_t val;
      unsigned int pktOffset = r * TargetControl::E_REG_BYTES * 2;

      // Not all registers are necessarily supported.
      if (currentGThread->readReg (r, val))
	Utils::reg2Hex (val, &(pkt->data[pktOffset]));
      else
	for (unsigned int i = 0; i < TargetControl::E_REG_BYTES * 2; i++)
	  pkt->data[pktOffset + i] = 'X';
    }

  // Debugging
  if (si->debugStopResumeDetail ())
    {
      double mes = fTargetControl->endOfBaudMeasurement();
      cerr << "DebugStopResumeDetail: readAllRegs time: " << mes << "ms."
	   << endl;
    }

  // Finalize the packet and send it
  pkt->data[NUM_REGS * TargetControl::E_REG_BYTES * 2] = '\0';
  pkt->setLen (NUM_REGS * TargetControl::E_REG_BYTES * 2);
  rsp->putPkt (pkt);

}	// rspReadAllRegs ()


//-----------------------------------------------------------------------------
//! Handle a RSP write all registers request

//! The registers follow the GDB sequence for Epiphany: GPR0 through GPR63,
//! followed by the SCRs. Each register is supplied as a sequence of bytes in
//! target endian order.

//! Each byte is packed as a pair of hex digits.

//! @note Not believed to be used by the GDB client at present.

//! @todo There is no error checking at present. Non-hex chars will generate a
//!       warning message, but there is no other check that the right amount
//!       of data is present. The result is always "OK".
//-----------------------------------------------------------------------------
void
GdbServer::rspWriteAllRegs ()
{
  assert (NULL != currentGThread);

  // All registers
  for (unsigned int r = 0; r < NUM_REGS; r++)
      (void) currentGThread->writeReg (r, Utils::hex2Reg (&(pkt->data[r * 8])));

  // Acknowledge (always OK for now).
  pkt->packStr ("OK");
  rsp->putPkt (pkt);

}				// rspWriteAllRegs()


//-----------------------------------------------------------------------------
//! Set the thread number of subsequent operations.

//! A tid of -1 means "all threads", 0 means "any thread". 0 causes all sorts of
//! problems later, so we replace it by the first thread in the current
//! process. We use NULL as the current thread to indicate "all threads".
//-----------------------------------------------------------------------------
void
GdbServer::rspSetThread ()
{
  char  c;
  int  tid;
  Thread* thread;

  if (2 != sscanf (pkt->data, "H%c%x:", &c, &tid))
    {
      cerr << "Warning: Failed to recognize RSP set thread command: "
	   << pkt->data << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  assert (tid >= -1);			// Sanity check

  switch (tid)
    {
    case -1:
      thread = NULL;

    case 0:
      thread = mCurrentProcess->threadBegin ();

    default:
      thread = getThread (tid);
    }

  switch (c)
    {
    case 'c': currentCThread = thread; break;
    case 'g': currentGThread = thread; break;

    default:
      cerr << "Warning: Failed RSP set thread command '"
	   << pkt->data << "': ignored." endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  pkt->packStr ("OK");
  rsp->putPkt (pkt);

}	// rspSetThread ()


//-----------------------------------------------------------------------------
//! Handle a RSP read memory (symbolic) request

//! Syntax is:

//!   m<addr>,<length>:

//! The response is the bytes, lowest address first, encoded as pairs of hex
//! digits.

//! The length given is the number of bytes to be read.

//! @todo This implementation writes everything as individual bytes. A more
//!       efficient implementation would write words (where possible) and
//!       stream the accesses, since the Epiphany only supports word
//!       read/write at present.
//-----------------------------------------------------------------------------
void
GdbServer::rspReadMem ()
{
  unsigned int addr;		// Where to read the memory
  int len;			// Number of bytes to read
  int off;			// Offset into the memory

  assert (NULL != currentGThread);

  if (2 != sscanf (pkt->data, "m%x,%x:", &addr, &len))
    {
      cerr << "Warning: Failed to recognize RSP read memory command: "
	<< pkt->data << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  // Make sure we won't overflow the buffer (2 chars per byte)
  if ((len * 2) >= pkt->getBufSize ())
    {
      cerr << "Warning: Memory read " << pkt->data
	<< " too large for RSP packet: truncated" << endl;
      len = (pkt->getBufSize () - 1) / 2;
    }

  if (si->debugTiming ())
    {
      fTargetControl->startOfBaudMeasurement ();
      cerr << "DebugTiming: rspReadMem START, address " << addr
	   << ", length " << len << endl;
    }

  // Write the bytes to memory
  {
    char buf[len];

    bool retReadOp =
      currentGThread->readMemBlock (addr, (unsigned char *) buf, len);

    if (!retReadOp)
      {
	pkt->packStr ("E01");
	rsp->putPkt (pkt);
	return;
      }

    // Refill the buffer with the reply
    for (off = 0; off < len; off++)
      {

	unsigned char ch = buf[off];

	pkt->data[off * 2] = Utils::hex2Char (ch >> 4);
	pkt->data[off * 2 + 1] = Utils::hex2Char (ch & 0xf);
      }


  }

  if (si->debugTiming ())
    {
      double mes = fTargetControl->endOfBaudMeasurement();
      cerr << "DebugTiming: rspReadMem END, " << mes << "  ms." << endl;
    }

  pkt->data[off * 2] = '\0';	// End of string
  pkt->setLen (strlen (pkt->data));
  rsp->putPkt (pkt);

}				// rsp_read_mem()


//-----------------------------------------------------------------------------
//! Handle a RSP write memory (symbolic) request

//! Syntax is:

//!   m<addr>,<length>:<data>

//! The data is the bytes, lowest address first, encoded as pairs of hex
//! digits.

//! The length given is the number of bytes to be written.

//! @note Not believed to be used by the GDB client at present.
//-----------------------------------------------------------------------------
void
GdbServer::rspWriteMem ()
{
  uint32_t addr;		// Where to write the memory
  int len;			// Number of bytes to write

  assert (NULL != currentGThread);

  if (2 != sscanf (pkt->data, "M%x,%x:", &addr, &len))
    {
      cerr << "Warning: Failed to recognize RSP write memory "
	<< pkt->data << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  // Find the start of the data and check there is the amount we expect.
  char *symDat = (char *) (memchr (pkt->data, ':', pkt->getBufSize ())) + 1;
  int datLen = pkt->getLen () - (symDat - pkt->data);

  // Sanity check
  if (len * 2 != datLen)
    {
      cerr << "Warning: Write of " << len * 2 << "digits requested, but "
	<< datLen << " digits supplied: packet ignored" << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  // Write the bytes to memory
  {
    //cerr << "rspWriteMem" << hex << addr << dec << " (" << len << ")" << endl;
    if (!currentGThread->writeMemBlock (addr, (unsigned char *) symDat, len))
      {
	pkt->packStr ("E01");
	rsp->putPkt (pkt);
	return;
      }
  }

  pkt->packStr ("OK");
  rsp->putPkt (pkt);

}				// rspWriteMem()


//-----------------------------------------------------------------------------
//! Read a single register

//! The register is returned as a sequence of bytes in target endian order.

//! Each byte is packed as a pair of hex digits.

//! @note Not believed to be used by the GDB client at present.
//-----------------------------------------------------------------------------
void
GdbServer::rspReadReg ()
{
  assert (NULL != currentGThread);

  unsigned int regnum;
  uint32_t regval;

  // Break out the fields from the data
  if (1 != sscanf (pkt->data, "p%x", &regnum))
    {
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  if (regnum >= NUM_REGS)
    {
      pkt->packStr ("E02");
      rsp->putPkt (pkt);
      return;
    }

  // Get the relevant register
  if (!currentGThread->readReg (regnum, regval))
    {
      pkt->packStr ("E03");
      rsp->putPkt (pkt);
      return;
    }

  Utils::reg2Hex (regval, pkt->data);
  pkt->setLen (strlen (pkt->data));
  rsp->putPkt (pkt);

}	// rspReadReg()


//-----------------------------------------------------------------------------
//! Write a single register

//! The register value is specified as a sequence of bytes in target endian
//! order.

//! Each byte is packed as a pair of hex digits.
//-----------------------------------------------------------------------------
void
GdbServer::rspWriteReg ()
{
  assert (NUll != currentGThread);

  unsigned int regnum;
  char valstr[9];		// Allow for EOS on the string

  // Break out the fields from the data
  if (2 != sscanf (pkt->data, "P%x=%8s", &regnum, valstr))
    {
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  if (regnum >= NUM_REGS)
    {
      pkt->packStr ("E02");
      rsp->putPkt (pkt);
      return;
    }

  // Set the relevant register
  if (! currentGThread->writeReg (regnum, Utils::hex2Reg (valstr)))
    {
      pkt->packStr ("E03");
      rsp->putPkt (pkt);
      return;
    }

  pkt->packStr ("OK");
  rsp->putPkt (pkt);

}	// rspWriteReg()


//-----------------------------------------------------------------------------
//! Handle a RSP query request
//-----------------------------------------------------------------------------
void
GdbServer::rspQuery ()
{
  //cerr << "rspQuery " << pkt->data << endl;

  if (0 == strcmp ("qC", pkt->data))
    {
      // Return the current thread ID (unsigned hex). A null response
      // indicates to use the previously selected thread. We use the G thread,
      // since C thread should be handled by vCont anyway.

      sprintf (pkt->data, "QC%x",
	       (NULL == currentGThread) ? -1 : currentGThread->tid ());
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);
    }
  else if (0 == strncmp ("qCRC", pkt->data, strlen ("qCRC")))
    {
      // Return CRC of memory area
      cerr << "Warning: RSP CRC query not supported" << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
    }
  else if (0 == strcmp ("qfThreadInfo", pkt->data))
    {
      // Return initial info about active threads.
      rspQThreadInfo (true);
    }
  else if (0 == strcmp ("qsThreadInfo", pkt->data))
    {
      // Return more info about active threads.
      rspQThreadInfo (false);
    }
  else if (0 == strncmp ("qGetTLSAddr:", pkt->data, strlen ("qGetTLSAddr:")))
    {
      // We don't support this feature
      pkt->packStr ("");
      rsp->putPkt (pkt);
    }
  else if (0 == strncmp ("qL", pkt->data, strlen ("qL")))
    {
      // Deprecated and replaced by 'qfThreadInfo'
      cerr << "Warning: RSP qL deprecated: no info returned" << endl;
      pkt->packStr ("qM001");
      rsp->putPkt (pkt);
    }
  else if (0 == strcmp ("qNonStop:0", pkt->data))
    {
      mDebugMode = ALL_STOP;
      stopAttachedProcesses ();
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else if (0 == strcmp ("qNonStop:1", pkt->data))
    {
      mDebugMode = NON_STOP;
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else if (0 == strcmp ("qOffsets", pkt->data))
    {
      // Report any relocation
      pkt->packStr ("Text=0;Data=0;Bss=0");
      rsp->putPkt (pkt);
    }
  else if (0 == strncmp ("qP", pkt->data, strlen ("qP")))
    {
      // Deprecated and replaced by 'qThreadExtraInfo'
      cerr << "Warning: RSP qP deprecated: no info returned" << endl;
      pkt->packStr ("");
      rsp->putPkt (pkt);
    }
  else if (0 == strncmp ("qRcmd,", pkt->data, strlen ("qRcmd,")))
    {
      // This is used to interface to commands to do "stuff"
      rspCommand ();
    }
  else if (0 == strncmp ("qSupported", pkt->data, strlen ("qSupported")))
    {
      char const *coreExtension = "qSupported:xmlRegisters=coreid.";

      if (0 == strncmp (coreExtension, pkt->data, strlen (coreExtension)))
	cerr << "Warning: GDB setcoreid not supporte: ignored" << endl;

      // Report a list of the features we support. For now we just ignore any
      // supplied specific feature queries, but in the future these may be
      // supported as well. Note that the packet size allows for 'G' + all the
      // registers sent to us, or a reply to 'g' with all the registers and an
      // EOS so the buffer is a well formed string.
      sprintf (pkt->data, "PacketSize=%x;qXfer:osdata:read+,QNonStop+",
	       pkt->getBufSize ());
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);
    }
  else if (0 == strncmp ("qSymbol:", pkt->data, strlen ("qSymbol:")))
    {
      // Offer to look up symbols. Nothing we want (for now). TODO. This just
      // ignores any replies to symbols we looked up, but we didn't want to
      // do that anyway!
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else if (0 ==
	   strncmp ("qThreadExtraInfo,", pkt->data,
		    strlen ("qThreadExtraInfo,")))
    {
      rspQThreadExtraInfo();
    }
  else if (0 == strncmp ("qXfer:", pkt->data, strlen ("qXfer:")))
    {
      rspTransfer ();
    }
  else if (0 == strncmp ("qTStatus", pkt->data, strlen ("qTStatus")))
    {
      //Ask the stub if there is a trace experiment running right now
      //For now we support no 'qTStatus' requests
      //cerr << "Warning: RSP 'qTStatus' not supported: ignored" << endl;
      pkt->packStr ("");
      rsp->putPkt (pkt);
    }
  else if (0 == strncmp ("qAttached", pkt->data, strlen ("qAttached")))
    {
      //Querying remote process attach state
      //The remote target doesn't run under any OS suppling the, dteaching and killing in will have a same effect
      //cerr << "Warning: RSP 'qAttached' not supported: ignored" << endl;
      pkt->packStr ("");
      rsp->putPkt (pkt);
    }
  else
    {
      // We don't support this feature. RSP specification is to return an
      // empty packet.
      pkt->packStr ("");
      rsp->putPkt (pkt);
    }
}				// rspQuery()


//-----------------------------------------------------------------------------
//! Handle a RSP qfThreadInfo/qsThreadInfo request

//! Reuse the incoming packet. We only return thread info for the current
//! process.

//! @todo We assume we can do everything in the first packet.

//! @param[in] isFirst  TRUE if we were a qfThreadInfo packet.
//-----------------------------------------------------------------------------
void
GdbServer::rspQThreadInfo (bool isFirst)
{
  if (isFirst)
    {
      ostringstream  os;

      // Iterate all the threads in the core
      for (set <Thread*>::iterator it = mCurrentProcess->threadBegin ();
	   it != mCurrentProcess->threadEnd ();
	   it++)
	{
	  if (it != mCurrentProcess->threadBegin ())
	    os << ",";

	  os << hex << (*it)->tid ();
	}

      string reply = os.str ();
      pkt->packNStr (reply.c_str (), reply.size (), 'm');
    }
  else
    pkt->packStr ("l");

  rsp->putPkt (pkt);

}	// rspQThreadInfo ()


//-----------------------------------------------------------------------------
//! Handle a RSP qThreadExtraInfo request

//! Reuse the incoming packet. For now we just ignore -1 and 0 as threads.
//-----------------------------------------------------------------------------
void
GdbServer::rspQThreadExtraInfo ()
{
  int tid;

  if (1 != sscanf (pkt->data, "qThreadExtraInfo,%x", &tid))
    {
      cerr << "Warning: Failed to recognize RSP qThreadExtraInfo command : "
	<< pkt->data << endl;
      pkt->packStr ("E01");
      return;
    }

  if (tid > 0)
    {
      // Data about thread
      Thread* thread = getThread (tid);
      CoreId coreId = thread->coreId ();
      bool isHalted = thread->isHalted ();
      bool isIdle = thread->isIdle ();
      bool isInterruptible = thread->isInterruptible ();

      string res = "Core: ";
      res += coreId;
      if (isIdle)
	res += isHalted ? ": idle, halted" : ": idle";
      else
	res += isHalted ? ": halted" : ": running";

      res += isInterruptible ? ", interruptible" : ", not interruptible";

      // Convert each byte to ASCII
      Utils::ascii2Hex (&(pkt->data[0]), res.c_str ());
      pkt->setLen (strlen (pkt->data));
    }
  else
    {
      cerr << "Warning: Cannot supply info for thread -1." << endl;
      pkt->packStr ("");
    }

  rsp->putPkt (pkt);

}	// rspQThreadExtraInfo ()


//-----------------------------------------------------------------------------
//! Handle a RSP qRcmd request

//! The actual command follows the "qRcmd," in ASCII encoded to hex
//-----------------------------------------------------------------------------
void
GdbServer::rspCommand ()
{
  char cmd[RSP_PKT_MAX];

  Utils::hex2Ascii (cmd, &(pkt->data[strlen ("qRcmd,")]));

  // Say OK, so we don't stop
  sprintf (pkt->data, "OK");


  if (strcmp ("swreset", cmd) == 0)
    {
      cout << "INFO: Software reset" << endl;
      targetSwReset ();
      pkt->packHexstr ("Software reset issued\n");
      rsp->putPkt (pkt);
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else if (strcmp ("hwreset", cmd) == 0)
    {
      cout << "INFO: Hardware reset" << endl;
      targetHWReset ();
      pkt->packHexstr ("Hardware reset issued: restart debug client (s)\n");
      rsp->putPkt (pkt);
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else if (strcmp ("halt", cmd) == 0)
    {
      cout << "INFO: Halting all cores" << endl;

      if (haltAllThreads ())
	pkt->packHexstr ("All cores halted\n");
      else
	{
	  cout << "INFO: - some cores failed to halt" << endl;
	  pkt->packHexstr ("Some cores halted\n");
	}
      rsp->putPkt (pkt);
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else if (strcmp ("run", cmd) == 0)
    {
      // Not at all sure what this is meant to do. Send a rude message for
      // now. Perhaps it was about taking cores out of IDLE?
      pkt->packHexstr ("monitor run no longer supported\n");
      rsp->putPkt (pkt);
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else if (strcmp ("coreid", cmd) == 0)
    {
      CoreId absCCoreId = currentCThread->readCoreId ();
      CoreId absGCoreId = currentGThread->readCoreId ();
      CoreId relCCoreId = fTargetControl->abs2rel (absCCoreId);
      CoreId relGCoreId = fTargetControl->abs2rel (absGCoreId);
      ostringstream  oss;

      oss << "Continue core ID: " << absCCoreId << " (absolute), "
	  << relCCoreId << " (relative)" << endl;
      pkt->packHexstr (oss.str ().c_str ());
      oss << "General  core ID: " << absGCoreId << " (absolute), "
	  << relGCoreId << " (relative)" << endl;
      pkt->packHexstr (oss.str ().c_str ());

      rsp->putPkt (pkt);
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else if (strncmp ("workgroup", cmd, strlen ("workgroup")) == 0)
    {
      rspCmdWorkgroup (cmd);
    }
  else if (strncmp ("process", cmd, strlen ("process")) == 0)
    {
      rspCmdProcess (cmd);
    }
  else if (strcmp ("help", cmd) == 0)
    {
      pkt->packHexstr ("monitor commands: hwreset, coreid, swreset, halt, "
		       "run, help\n");
      rsp->putPkt (pkt);
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else
    {
      cerr << "Warning: Remote command " << cmd << ": ignored" << endl;
      pkt->packHexstr ("monitor command not recognized\n");
      rsp->putPkt (pkt);
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
    }
}	// rspCommand()


//-----------------------------------------------------------------------------
//! Handle the "monitor workgroup" command.

//! Format is: "monitor workgroup <row> <col> <rows> <cols>

//! Where the meaning of the arguments correspond to the arguments to
//! e_open. We create a new process, and associate it with the handle for the
//! workgroup from e_open.

//! @param[in] cmd  The command string for parsing.
//-----------------------------------------------------------------------------
void
GdbServer::rspCmdWorkgroup (char* cmd)
{
  stringstream    ss (cmd);
  vector <string> tokens;
  string          item;

  // Break out the command line args
  while (getline (ss, item, ' '))
    tokens.push_back (item);

  if ((5 != tokens.size ()) || (0 != tokens[0].compare ("workgroup")))
    {
      cerr << "Warning: Defective monitor workgroup command: " << cmd
	   << ": ignored." << endl;
      pkt->packHexstr ("monitor workgroup command not recognized\n");
      rsp->putPkt (pkt);
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  unsigned long int row  = strtoul (tokens[1].c_str (), NULL, 0);
  unsigned long int col  = strtoul (tokens[2].c_str (), NULL, 0);
  unsigned long int rows = strtoul (tokens[3].c_str (), NULL, 0);
  unsigned long int cols = strtoul (tokens[4].c_str (), NULL, 0);
  unsigned int numRows = fTargetControl->getNumRows ();
  unsigned int numCols = fTargetControl->getNumCols ();

  if (row >= numRows)
    {
      cerr << "Warning: Starting row " << row << "too large: ignored." << endl;
      pkt->packHexstr ("Starting row too large.\n");
      rsp->putPkt (pkt);
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
  else if (col >= numCols)
    {
      cerr << "Warning: Starting column " << col << "too large: ignored."
	   << endl;
      pkt->packHexstr ("Starting column too large.\n");
      rsp->putPkt (pkt);
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
  else if ((row + rows) > numRows)
    {
      cerr << "Warning: Two many rows: " << rows << ": ignored." << endl;
      pkt->packHexstr ("Too many rows.\n");
      rsp->putPkt (pkt);
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
  else if ((col + cols) > numCols)
    {
      cerr << "Warning: Two many columns: " << cols << ": ignored." << endl;
      pkt->packHexstr ("Too many columns.\n");
      rsp->putPkt (pkt);
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  // We can only accept a group whose cores are all in the idle group.
  ProcessInfo *process = new ProcessInfo;
  int pid = mNextPid;

  for (unsigned int r = 0; r < rows; r++)
    for (unsigned int c = 0; c < cols; c++)
      {
	CoreId coreId (row + r, col + c);
	Thread* thread = mThreads[mCore2Tid[coreId]];
	if (mIdleProcess->eraseThread (thread))
	  assert (process->addThread (thread));
	else
	  {
	    // Yuk - blew up half way. Put all the threads back into the idle
	    // group, delete the process an give up.
	    for (set <int>::iterator it = process->threadBegin ();
		 it != process->threadEnd ();
		 it++)
	      {
		assert (process->eraseThread (*it));
		assert (mIdleProcess->addThread (*it));
	      }

	    delete process;

	    cerr << "Warning: failed to add thread " << thread
		 << "to workgroup." << endl;

	    pkt->packHexstr ("Not all workgroup cores in idle process.\n");
	    rsp->putPkt (pkt);
	    pkt->packStr ("E01");
	    rsp->putPkt (pkt);
	    return;
	  }
      }

  mProcessList[pid] = process;
  mNextPid++;

  ostringstream oss;
  oss << "New workgroup process ID " << pid << endl;
  pkt->packHexstr (oss.str ().c_str ());
  rsp->putPkt (pkt);
  pkt->packStr ("OK");
  rsp->putPkt (pkt);

}	// rspCmdWorkgroup ()


//-----------------------------------------------------------------------------
//! Handle the "monitor process" command.

//! Format is: "monitor process <pid>"

//! Where <pid> is a process ID shown by "info os processes".

//! @param[in] cmd  The command string for parsing.
//-----------------------------------------------------------------------------
void
GdbServer::rspCmdProcess (char* cmd)
{
  stringstream    ss (cmd);
  vector <string> tokens;
  string          item;

  // Break out the command line args
  while (getline (ss, item, ' '))
    tokens.push_back (item);

  if ((2 != tokens.size ()) || (0 != tokens[0].compare ("process")))
    {
      cerr << "Warning: Defective monitor process command: " << cmd
	   << ": ignored." << endl;
      pkt->packHexstr ("monitor process command not recognized\n");
      rsp->putPkt (pkt);
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  // Check the PID exists
  unsigned long int pid  = strtoul (tokens[1].c_str (), NULL, 0);

  if ((pid <= 0) || (pid >= mProcessList.size ()))
    {
      cerr << "Warning: Non existent PID " << pid << ": ignored." << endl;
      pkt->packHexstr ("Process ID does not exist.\n");
      rsp->putPkt (pkt);
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
  else
    {
      mCurrentProcess = mProcessList[pid];

      ostringstream oss;
      oss << "Process ID now " << pid << "." << endl;

      // This may have invalidated the current threads. If so correct
      // them. This is really a big dodgy - ultimately this needs proper
      // process handling.
      if ((NULL != currentCThread)
	  && (! mCurrentProcess->hasThread (currentCThread)))
	{
	  currentCThread = *(mCurrentProcess->threadBegin ());
	  oss << "- switching control thread to " << currentCThread << "."
	      << endl;
	  pkt->packHexstr (oss.str ().c_str ());
	  rsp->putPkt (pkt);
	}

      if ((NULL != currentGThread)
	  && (! mCurrentProcess->hasThread (currentGThread)))
	{
	  currentGThread = *(mCurrentProcess->threadBegin ());
	  oss << "- switching general thread to " << currentGThtread << "."
	      << endl;
	  pkt->packHexstr (oss.str ().c_str ());
	  rsp->putPkt (pkt);
	}

      pkt->packHexstr (oss.str ().c_str ());
      rsp->putPkt (pkt);
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
}	// rspCmdProcess ()


//-----------------------------------------------------------------------------
//! Handle a RSP qXfer request

//! The actual format is one of:
//! - "qXfer:<object>:read:<annex>:<offset>,<length>"
//! - "qXfer:<object>:write:<annex>:<offset>,<data>"

//! We only support a small subset.
//-----------------------------------------------------------------------------
void
GdbServer::rspTransfer ()
{
  stringstream   ss (pkt->data);
  vector<string> tokens;		// To break out the packet elements
  string         item;

  // Break out the packet
  while (getline (ss, item, ':'))
    tokens.push_back (item);

  // Break out offset/length or offset/data, which are comma separated.
  if (5 == tokens.size ())
    {
      ss.str (tokens[4]);
      ss.clear ();
      tokens.pop_back ();		// Remove offset/{length,data}

      // Break out the packet
      while (getline (ss, item, ','))
	tokens.push_back (item);
    }

  if (si->debugTrapAndRspCon ())
    {
      for (unsigned int i = 0; i < tokens.size (); i++)
	{
	  cerr << "RSP trace: qXfer: tokens[" << i << "] = " << tokens[i]
	       << "." << endl;
	}
    }

  // Default is to return an empty packet, indicating
  // unsupported/unrecognized.
  pkt->packStr ("");

  // See if we recognize anything
  if ((6 == tokens.size ())
      && (0 == tokens[2].compare ("read"))
      && (0 != tokens[4].size ())
      && (0 != tokens[5].size ()))
    {
      // All the read qXfers
      string object = tokens[1];
      string annex = tokens[3];
      unsigned int  offset;
      unsigned int  length;

      // Convert the offset and length.
      ss.str ("");
      ss.clear ();
      ss << hex << tokens[4];
      ss >> offset;
      ss.str ("");
      ss.clear ();
      ss << hex << tokens[5];
      ss >> length;

      if (si->debugTrapAndRspCon ())
	{
	  cerr << "RSP trace: qXfer, object = \"" << object
	       << "\", read, annex = \"" << annex << "\", offset = 0x"
	       << hex << offset << ", length = 0x" << length << dec << endl;
	}

      // Sort out what we have. Remember substrings are recognized
      if (0 == object.compare ("osdata"))
	{
	  if (0 == annex.compare (""))
	    rspOsData (offset, length);
	  else if (0 == string ("processes").find (annex))
	    rspOsDataProcesses (offset, length);
	  else if (0 == string ("load").find (annex))
	    rspOsDataLoad (offset, length);
	  else if (0 == string ("traffic").find (annex))
	    rspOsDataTraffic (offset, length);
	}
    }
  else if ((6 == tokens.size ())
	   && (0 == tokens[2].compare ("write"))
	   && (0 != tokens[4].size ()))
    {
      string object = tokens[1];
      string annex = tokens[3];
      unsigned int  offset;
      string data = tokens[5];

      // Convert the offset.
      ss.str ("");
      ss.clear ();
      ss << hex << tokens[4];
      ss >> offset;

      // All the write qXfers. Currently none supported
      if (si->debugTrapAndRspCon ())
	cerr << "RSP trace: qXfer, object = \"" << object
	     << ", write, annex = \"" << annex << "\", offset = 0x" << hex
	     << offset << dec <<  ", data = " << data << endl;
    }
  else
    if (si->debugTrapAndRspCon ())
      cerr << "RSP trace: qXfer unrecognzed." << endl;

  // Push out the packet
  rsp->putPkt (pkt);

}	// rspTransfer ()


//-----------------------------------------------------------------------------
//! Handle an OS info request

//! We need to return a list of all the commands we support

//! @param[in] offset  Offset into the reply to send.
//! @param[in] length  Length of the reply to send.
//-----------------------------------------------------------------------------
void
GdbServer::rspOsData (unsigned int offset,
		      unsigned int length)
{
  if (si->debugTrapAndRspCon ())
    {
      cerr << "RSP trace: qXfer:osdata:read:: offset 0x" << hex
	   << offset << ", length " << length << dec << endl;
    }

  // Get the data only for the first part of the reply. The rest of the time
  // we are just sending the remainder of the string.
  if (0 == offset)
    {
      osInfoReply =
	"<?xml version=\"1.0\"?>\n"
	"<!DOCTYPE target SYSTEM \"osdata.dtd\">\n"
	"<osdata type=\"types\">\n"
	"  <item>\n"
	"    <column name=\"Type\">processes</column>\n"
	"    <column name=\"Description\">Listing of all processes</column>\n"
	"    <column name=\"Title\">Processes</column>\n"
	"  </item>\n"
	"  <item>\n"
	"    <column name=\"Type\">load</column>\n"
	"    <column name=\"Description\">Listing of load on all cores</column>\n"
	"    <column name=\"Title\">Load</column>\n"
	"  </item>\n"
	"  <item>\n"
	"    <column name=\"Type\">traffic</column>\n"
	"    <column name=\"Description\">Listing of all cmesh traffic</column>\n"
	"    <column name=\"Title\">Traffic</column>\n"
	"  </item>\n"
	"</osdata>";
    }

  // Send the reply (or part reply) back
  unsigned int  len = osInfoReply.size ();

  if (si->debugTrapAndRspCon ())
    {
      cerr << "RSP trace: OS info length " << len << endl;
      cerr << osInfoReply << endl;
    }

  if (offset >= len)
    pkt->packStr ("l");
  else
    {
      unsigned int pktlen = len - offset;
      char pkttype = 'l';

      if (pktlen > length)
	{
	  /* Will need more packets */
	  pktlen = length;
	  pkttype = 'm';
	}

      pkt->packNStr (&(osInfoReply.c_str ()[offset]), pktlen, pkttype);
    }
}	// rspOsData ()


//-----------------------------------------------------------------------------
//! Handle an OS processes request

//! We need to return standard data, at this stage with all the cores. The
//! header and trailer part of the response is fixed.

//! @param[in] offset  Offset into the reply to send.
//! @param[in] length  Length of the reply to send.
//-----------------------------------------------------------------------------
void
GdbServer::rspOsDataProcesses (unsigned int offset,
			       unsigned int length)
{
  if (si->debugTrapAndRspCon ())
    {
      cerr << "RSP trace: qXfer:osdata:read:processes offset 0x" << hex
	   << offset << ", length " << length << dec << endl;
    }

  // Get the data only for the first part of the reply. The rest of the time
  // we are just sending the remainder of the string.
  if (0 == offset)
    {
      osProcessReply =
	"<?xml version=\"1.0\"?>\n"
	"<!DOCTYPE target SYSTEM \"osdata.dtd\">\n"
	"<osdata type=\"processes\">\n";

      // Iterate through all processes. We need to temporarily make each
      // process the "current" process
      for (vector <ProcessInfo *>::iterator pit = mProcessList.begin ();
	   pit != mProcessList.end ();
	   pit++)
	{
	  ProcessInfo *process = *pit;
	  osProcessReply += "  <item>\n"
	    "    <column name=\"pid\">";
	  osProcessReply += Utils::intStr (process);
	  osProcessReply += "</column>\n"
	    "    <column name=\"user\">root</column>\n"
	    "    <column name=\"command\"></column>\n"
	    "    <column name=\"cores\">\n"
	    "      ";

	  for (set <int>::iterator tit = process->threadBegin ();
	       tit != process->threadEnd (); tit++)
	    {
	      if (tit != process->threadBegin ())
		osProcessReply += ",";

	      osProcessReply += (*tit)->coreId ();
	    }

	  osProcessReply += "\n"
	    "    </column>\n"
	    "  </item>\n";
	}
      osProcessReply += "</osdata>";
    }

  // Send the reply (or part reply) back
  unsigned int  len = osProcessReply.size ();

  if (si->debugTrapAndRspCon ())
    {
      cerr << "RSP trace: OS process info length " << len << endl;
      cerr << osProcessReply << endl;
    }

  if (offset >= len)
    pkt->packStr ("l");
  else
    {
      unsigned int pktlen = len - offset;
      char pkttype = 'l';

      if (pktlen > length)
	{
	  /* Will need more packets */
	  pktlen = length;
	  pkttype = 'm';
	}

      pkt->packNStr (&(osProcessReply.c_str ()[offset]), pktlen, pkttype);
    }
}	// rspOsDataProcesses ()


//-----------------------------------------------------------------------------
//! Handle an OS core load request

//! This is epiphany specific.

//! @todo For now this is a stub which returns random values in the range 0 -
//! 99 for each core.

//! @param[in] offset  Offset into the reply to send.
//! @param[in] length  Length of the reply to send.
//-----------------------------------------------------------------------------
void
GdbServer::rspOsDataLoad (unsigned int offset,
			     unsigned int length)
{
  if (si->debugTrapAndRspCon ())
    {
      cerr << "RSP trace: qXfer:osdata:read:load offset 0x" << hex << offset
	   << ", length " << length << dec << endl;
    }

  // Get the data only for the first part of the reply. The rest of the time
  // we are just sending the remainder of the string.
  if (0 == offset)
    {
      osLoadReply =
	"<?xml version=\"1.0\"?>\n"
	"<!DOCTYPE target SYSTEM \"osdata.dtd\">\n"
	"<osdata type=\"load\">\n";

      for (map <CoreId, int>::iterator it = mCore2Tid.begin ();
	   it != mCore2Tid.end ();
	   it++)
	{
	  osLoadReply +=
	    "  <item>\n"
	    "    <column name=\"coreid\">";
	  osLoadReply += it->first;
	  osLoadReply += "</column>\n";

	  osLoadReply +=
	    "    <column name=\"load\">";
	  osLoadReply += Utils::intStr (random () % 100, 10, 2);
	  osLoadReply += "</column>\n"
	    "  </item>\n";
	}

      osLoadReply += "</osdata>";

      if (si->debugTrapAndRspCon ())
	{
	  cerr << "RSP trace: OS load info length "
	       << osLoadReply.size () << endl;
	  cerr << osLoadReply << endl;
	}
    }

  // Send the reply (or part reply) back
  unsigned int  len = osLoadReply.size ();

  if (si->debugTrapAndRspCon ())
    {
      cerr << "RSP trace: OS load info length " << len << endl;
      cerr << osLoadReply << endl;
    }

  if (offset >= len)
    pkt->packStr ("l");
  else
    {
      unsigned int pktlen = len - offset;
      char pkttype = 'l';

      if (pktlen > length)
	{
	  /* Will need more packets */
	  pktlen = length;
	  pkttype = 'm';
	}

      pkt->packNStr (&(osLoadReply.c_str ()[offset]), pktlen, pkttype);
    }
}	// rspOsDataLoad ()


//-----------------------------------------------------------------------------
//! Handle an OS mesh load request

//! This is epiphany specific.

//! When working out "North", "South", "East" and "West", the assumption is
//! that core (0,0) is at the North-East corner. We provide in and out traffic
//! for each direction.

//! @todo Currently only dummy data.

//! @param[in] offset  Offset into the reply to send.
//! @param[in] length  Length of the reply to send.
//-----------------------------------------------------------------------------
void
GdbServer::rspOsDataTraffic (unsigned int offset,
			     unsigned int length)
{
  if (si->debugTrapAndRspCon ())
    {
      cerr << "RSP trace: qXfer:osdata:read:traffic offset 0x" << hex << offset
	   << ", length " << length << dec << endl;
    }

  // Get the data only for the first part of the reply. The rest of the time
  // we are just sending the remainder of the string.
  if (0 == offset)
    {
      osTrafficReply =
	"<?xml version=\"1.0\"?>\n"
	"<!DOCTYPE target SYSTEM \"osdata.dtd\">\n"
	"<osdata type=\"traffic\">\n";

      unsigned int maxRow = fTargetControl->getNumRows () - 1;
      unsigned int maxCol = fTargetControl->getNumCols () - 1;

      for (map <CoreId, int>::iterator it = mCore2Tid.begin ();
	   it != mCore2Tid.end ();
	   it++)
	{
	  CoreId coreId = it->first;
	  string inTraffic;
	  string outTraffic;

	  osTrafficReply +=
	    "  <item>\n"
	    "    <column name=\"coreid\">";
	  osTrafficReply += coreId;
	  osTrafficReply += "</column>\n";

	  // See what adjacent cores we have. Note that empty columns confuse
	  // GDB! There is traffic on incoming edges, but not outgoing.
	  inTraffic = Utils::intStr (random () % 100, 10, 2);
	  if (coreId.row () > 0)
	    outTraffic = Utils::intStr (random () % 100, 10, 2);
	  else
	    outTraffic = "--";

	  osTrafficReply +=
	    "    <column name=\"North In\">";
	  osTrafficReply += inTraffic;
	  osTrafficReply += "</column>\n"
	    "    <column name=\"North Out\">";
	  osTrafficReply += outTraffic;
	  osTrafficReply += "</column>\n";

	  inTraffic = Utils::intStr (random () % 100, 10, 2);
	  if (coreId.row ()< maxRow)
	    outTraffic = Utils::intStr (random () % 100, 10, 2);
	  else
	    outTraffic = "--";

	  osTrafficReply +=
	    "    <column name=\"South In\">";
	  osTrafficReply += inTraffic;
	  osTrafficReply += "</column>\n"
	    "    <column name=\"South Out\">";
	  osTrafficReply += outTraffic;
	  osTrafficReply += "</column>\n";

	  inTraffic = Utils::intStr (random () % 100, 10, 2);
	  if (coreId.col () < maxCol)
	    outTraffic = Utils::intStr (random () % 100, 10, 2);
	  else
	    outTraffic = "--";

	  osTrafficReply +=
	    "    <column name=\"East In\">";
	  osTrafficReply += inTraffic;
	  osTrafficReply += "</column>\n"
	    "    <column name=\"East Out\">";
	  osTrafficReply += outTraffic;
	  osTrafficReply += "</column>\n";

	  inTraffic = Utils::intStr (random () % 100, 10, 2);
	  if (coreId.col () > 0)
	    outTraffic = Utils::intStr (random () % 100, 10, 2);
	  else
	    outTraffic = "--";

	  osTrafficReply +=
	    "    <column name=\"West In\">";
	  osTrafficReply += inTraffic;
	  osTrafficReply += "</column>\n"
	    "    <column name=\"West Out\">";
	  osTrafficReply += outTraffic;
	  osTrafficReply += "</column>\n"
	    "  </item>\n";
	}

      osTrafficReply += "</osdata>";

      if (si->debugTrapAndRspCon ())
	{
	  cerr << "RSP trace: OS traffic info length "
	       << osTrafficReply.size () << endl;
	  cerr << osTrafficReply << endl;
	}
    }

  // Send the reply (or part reply) back
  unsigned int  len = osTrafficReply.size ();

  if (offset >= len)
    pkt->packStr ("l");
  else
    {
      unsigned int pktlen = len - offset;
      char pkttype = 'l';

      if (pktlen > length)
	{
	  /* Will need more packets */
	  pktlen = length;
	  pkttype = 'm';
	}

      pkt->packNStr (&(osTrafficReply.c_str ()[offset]), pktlen, pkttype);
    }
}	// rspOsDataTraffic ()


//-----------------------------------------------------------------------------
//! Handle a RSP set request
//-----------------------------------------------------------------------------
void
GdbServer::rspSet ()
{
  if (0 == strncmp ("QPassSignals:", pkt->data, strlen ("QPassSignals:")))
    {
      // Passing signals not supported
      pkt->packStr ("");
      rsp->putPkt (pkt);
    }
  else if ((0 == strcmp ("QTStart", pkt->data)))
    {
      if (fTargetControl->startTrace ())
	{
	  pkt->packStr ("OK");
	  rsp->putPkt (pkt);
	}
      else
	{
	  pkt->packStr ("");
	  rsp->putPkt (pkt);
	}
    }
  else if ((0 == strcmp ("QTStop", pkt->data)))
    {
      if (fTargetControl->stopTrace ())
	{
	  pkt->packStr ("OK");
	  rsp->putPkt (pkt);
	}
      else
	{
	  pkt->packStr ("");
	  rsp->putPkt (pkt);
	}
    }
  else if ((0 == strcmp ("QTinit", pkt->data)))
    {
      if (fTargetControl->initTrace ())
	{
	  pkt->packStr ("OK");
	  rsp->putPkt (pkt);
	}
      else
	{
	  pkt->packStr ("");
	  rsp->putPkt (pkt);
	}
    }
  else if ((0 == strncmp ("QTDP", pkt->data, strlen ("QTDP"))) ||
	   (0 == strncmp ("QFrame", pkt->data, strlen ("QFrame"))) ||
	   (0 == strncmp ("QTro", pkt->data, strlen ("QTro"))))
    {
      // All tracepoint features are not supported. This reply is really only
      // needed to 'QTDP', since with that the others should not be
      // generated.

      // TODO support trace .. VCD dump
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else
    {
      cerr << "Unrecognized RSP set request: ignored" << endl;
      delete pkt;
    }
}				// rspSet()


//-----------------------------------------------------------------------------
//! Handle a RSP restart request

//! For now we just put the program counter back to zero. If we supported the
//! vRun request, we should use the address specified there. There is no point
//! in unstalling the processor, since we'll never get control back.
//-----------------------------------------------------------------------------
void
GdbServer::rspRestart ()
{
  currentCThread->writePc (0);

}				// rspRestart()


//---------------------------------------------------------------------------
//! Handle a RSP 'T' packet

//! We have no concept of "dead" threads, because a core is always alive. So
//! for any valid thread, we return OK.

//! @todo Do we need to handle -1 (all threads) and 0 (any thread).
//---------------------------------------------------------------------------
void
GdbServer::rspIsThreadAlive ()
{
  int tid;

  if (1 != sscanf (pkt->data, "T%x", &tid))
    {
      cerr << "Warning: Failed to recognize RSP 'T' command : "
	   << pkt->data << endl;
      pkt->packStr ("E02");
      rsp->putPkt (pkt);
      return;
    }

  // This will not find thread IDs 0 (any) or -1 (all), which seems to be what
  // we want.
  if (mCurrentProcess->hasThread (tid))
    pkt->packStr ("OK");
  else
    pkt->packStr ("E01");

  rsp->putPkt (pkt);

}	// isThreadAlive ()


//---------------------------------------------------------------------------
//! Test if we have a 32-bit instruction in our hand.

//! @param[in] iab_instr  The instruction to test
//! @return TRUE if this is a 32-bit instruction, FALSE otherwise.
//---------------------------------------------------------------------------
bool
GdbServer::is32BitsInstr (uint32_t iab_instr)
{

  bool de_extended_instr = (getfield (iab_instr, 3, 0) == uint8_t (0xf));

  bool de_regi = (getfield (iab_instr, 2, 0) == uint8_t (3));
  bool de_regi_long = de_regi && (getfield (iab_instr, 3, 3) == 1);

  bool de_loadstore = (getfield (iab_instr, 2, 0) == uint8_t (0x4))
    || (getfield (iab_instr, 1, 0) == uint8_t (1));
  bool de_loadstore_long = de_loadstore && (getfield (iab_instr, 3, 3) == 1);

  bool de_branch = (getfield (iab_instr, 2, 0) == uint8_t (0));
  bool de_branch_long_sel = de_branch && (getfield (iab_instr, 3, 3) == 1);

  bool res = (de_extended_instr ||	// extension
	      de_loadstore_long ||	// long load/store
	      de_regi_long ||	// long imm reg
	      de_branch_long_sel);	// long branch

  return res;

}	// is32BitsInstr ()


//-----------------------------------------------------------------------------
//! Handle a RSP 'v' packet

//! These are commands associated with executing the code on the target
//-----------------------------------------------------------------------------
void
GdbServer::rspVpkt ()
{
  if (0 == strncmp ("vAttach;", pkt->data, strlen ("vAttach;")))
    {
      // Attaching is a null action, since we have no other process. We just
      // return a stop packet (as a TRAP exception) to indicate we are stopped.
      pkt->packStr ("S05");
      rsp->putPkt (pkt);
      return;
    }
  else if (0 == strcmp ("vCont?", pkt->data))
    {
      // Support this for multi-threading
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
      return;
    }
  else if (0 == strncmp ("vCont", pkt->data, strlen ("vCont")))
    {
      rspVCont ();
      return;
    }
  else if (0 == strncmp ("vFile:", pkt->data, strlen ("vFile:")))
    {
      // For now we don't support this.
      cerr << "Warning: RSP vFile not supported: ignored" << endl;
      pkt->packStr ("");
      rsp->putPkt (pkt);
      return;
    }
  else if (0 == strncmp ("vFlashErase:", pkt->data, strlen ("vFlashErase:")))
    {
      // For now we don't support this.
      cerr << "Warning: RSP vFlashErase not supported: ignored" << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
  else if (0 == strncmp ("vFlashWrite:", pkt->data, strlen ("vFlashWrite:")))
    {
      // For now we don't support this.
      cerr << "Warning: RSP vFlashWrite not supported: ignored" << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
  else if (0 == strcmp ("vFlashDone", pkt->data))
    {
      // For now we don't support this.
      cerr << "Warning: RSP vFlashDone not supported: ignored" << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
  else if (0 == strncmp ("vRun;", pkt->data, strlen ("vRun;")))
    {
      // We shouldn't be given any args, but check for this
      if (pkt->getLen () > (int) strlen ("vRun;"))
	{
	  cerr << "Warning: Unexpected arguments to RSP vRun "
	    "command: ignored" << endl;
	}

      // Restart the current program. However unlike a "R" packet, "vRun"
      // should behave as though it has just stopped. We use signal 5 (TRAP).
      rspRestart ();
      pkt->packStr ("S05");
      rsp->putPkt (pkt);
    }
  else
    {
      cerr << "Warning: Unknown RSP 'v' packet type " << pkt->data
	<< ": ignored" << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
}				// rspVpkt()


//-----------------------------------------------------------------------------
//! Handle a vCont packet

//! Syntax is vCont[;action[:thread-id]]...

//! For now we only have the all-stop functionality supported.

//! There should be at most one default action.
//-----------------------------------------------------------------------------
void
GdbServer::rspVCont ()
{
  map <Thread*, string>  threadActions;
  string defaultAction = parseVContArgs (mCurrentProcess, threadActions);

  // We have a map of threads to actions and a default action for any thread
  // that is not specified. We go through all threads applying the specified
  // action.

  // Apply the actions
  for (set <Thread*>::iterator it = mCurrentProcess->threadBegin ();
       it != mCurrentProcess->threadEnd ();
       it++)
    {
      Thread thread = *it;
      map <Thread*, string>::iterator ait = threadActions.find (thread);
      string ta = (threadActions.end () == ait)	? defaultAction : ait->second;
      uint32_t pc;
      TargetSignal sig;

      switch (ta[0])
	{
	case 'c':
	  pc = thread->readPc ();
	  continueThread (thread, pc, TARGET_SIGNAL_NONE);
	  break;

	case 'C':
	  pc = thread->readPc ();
	  cerr << "Warning: rspVCont: signal " << sig
	       << " for 'C' action ignored." << endl;
	  sig = (TargetSignal) strtol (ta.substr (1).c_str (), NULL, 16);
	  continueThread (thread, pc, sig);
	  break;

	case 'r':
	case 's':
	case 'S':

	  // We don't have hardware support for single stepping, so we just
	  // should not get these requests.
	  cerr << "ERROR: '" << ta[0]
	       << "' action not supported for vCont packet: ignored." << endl;
	  break;

	case 't':
	  haltThread (thread);
	  break;

	case 'n':
	  break;

	default:
	  // This really should be impossible, treat as 'n' if it happens.
	  cerr << "Warning: rspVCont: unknown action '" << action
	       << "': ignored" << endl;
	  break;
	}
    }

  if (NON_STOP == mDebugMode)
    {
      // In non-stop mode we return immediately.
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else
    {
      // In all-stop mode, we wait until one thread halts, then halt everyone.
      set <Thread *> stoppedThreads;
      Thread *thread = waitAllThreads (mCurrentProcess, stoppedThreads);
      haltAllThreads (mCurrentProcess);
      rspReportStopped (mCurrentProcess, stoppedThreads);
    }
}	// rspVCont ()


//-----------------------------------------------------------------------------
//! Parse args for a vCont packet

//! Populate a map of thread to action string.

//! In addition to the actions specified by vCont, we also define 'n' (do
//! nothing).

//! @param[in]   process        The process we are working with
//! @param[out]  threadActions  Map of actions to populate
//! @return  The default action or NULL on error.
//-----------------------------------------------------------------------------
string
GdbServer::parseVContArgs (ProcessInfo*           process,
			   map <Thread*, string>  threadActions)
{
  stringstream    ss (pkt->data);
  vector <string> elements;		// To break out the packet elements
  string          item;

  // Break out the action/thread pairs
  while (getline (ss, item, ';'))
    elements.push_back (item);

  if (1 == elements.size ())
    {
      // We'll get a default "do nothing" action at the end of this function.
      cerr << "ERROR: No actions specified for vCont." << endl;
    }

  // Sort out the detail of each action
  string defaultAction;

  // In all-stop mode we should only (I think) be asked to step one
  // thread. Let's monitor that.
  int  numSteps = 0;

  for (size_t i = 1; i < elements.size (); i++)
    {
      vector <string> tokens;
      stringstream tss (elements [i]);

      // Break out the action and the thread
      while (getline (tss, item, ':'))
	tokens.push_back (item);

      if (1 == tokens.size ())
	{
	  // No thread ID, set default action

	  // @todo Are all actions valid as default?
	  if (0 != defaultAction.size ())
	    {
	      cerr << "ERROR: Duplicate default action for vCont: ignored."
		   << endl;
	      continue;
	    }

	  defaultAction = tokens[0];
	}
      else if (2 == tokens.size ())
	{
	  int tid = strtol (tokens[1].c_str (), NULL, 16);
	  Thread* thread  = getThread (tid);
	  if (process->hasThread (thread))
	    {
	      if (threadActions.find (thread) == threadActions.end ())
		{
		  string action = tokens[0];
		  threadActions.insert (pair <Thread *, string> (thread,
								 action));

		  // Note if we are a step thread. There should only be one of
		  // these in all-stop mode.
		  char actChar = action[0];
		  if (('s' == actChar) || ('S' == actChar))
		    {
		      numSteps++;

		      if ((ALL_STOP == mDebugMode) && (numSteps > 1))
			{
			  cerr << "Info: Multiple vCont steps in all-stop mode."
			       << endl;
			}
		    }
		}
	      else
		cerr << "Warning: Duplicate vCont action for thread ID "
		     << tid << ": ignored." << endl;
	    }
	  else
	    {
	      cerr << "Warning: vCont thread ID " << tid
		   << " not part of current process: ignored." << endl;
	    }
	}
      else
	{
	  cerr << "Warning: Unrecognized vCont action of size "
	       << tokens.size () << ": " << elements[i] << endl;
	}
    }

  if (0 == defaultAction.size ())
    defaultAction = string ("n");

  return defaultAction;

}	// parseVContArgs ()


//-----------------------------------------------------------------------------
//! Have we fit a "stopping" instruction

//! We know the thread is halted. Did it halt immediately after a BREAK, TRAP
//! or IDLE instruction. Not quite as easy as it seems, since TRAP may often be
//! followed by NOP.

//! Return the stop instruction found, or NOP_INSTR if it isn't a stop
//! instruction.

//! @param[in] thread  The thread to consider.
//! @return  TRUE if we processed file I/O and restarted. FALSE otherwise.
//-----------------------------------------------------------------------------
uint16_t
GdbServer::getStopInstr (Thread *thread)
{
  assert (thread->isHalted ());

  // First see if we just hit a breakpoint or IDLE. Fortunately IDLE, BREAK,
  // TRAP and NOP are all 16-bit instructions.
  uint32_t  pc = thread->readPc () - SHORT_INSTRLEN;
  uint16_t  instr16 = thread->readMem16 (pc);

  if ((BKPT_INSTR == instr16) || (IDLE_INSTR == instr16))
    return  instr16;

  // Find the first preceding non-NOP instruction
  while (NOP_INSTR == instr16)
    {
      pc -= SHORT_INSTRLEN;
      instr16 = thread->readMem16 (pc);
    }

  // If it isn't a TRAP, something has gone 'orribly wrong.
  if (getOpcode10 (instr16) == TRAP_INSTR)
    return instr16;
  else
    return NOP_INSTR;

}	// getStopInstr ()


//-----------------------------------------------------------------------------
//! Process file I/O if needed for a continued thread.

//! We know the thread is halted. Did it halt immediately after a TRAP
//! instruction. Not quite as easy as it seems, since TRAP may often be
//! followed by NOP.

//! If we do find TRAP, then process the File I/O and restart the thread.

//! @todo For now this is a placeholder. We don't actually deal with the F
//!       packet.

//! @param[in] thread  The thread to consider.
//! @return  TRUE if we processed file I/O and restarted. FALSE otherwise.
//-----------------------------------------------------------------------------
bool
GdbServer::doFileIO (Thread* thread)
{
  assert (thread->isHalted ());
  uint16_t instr16 = getStopInstr (thread);

  // Have we stopped for a TRAP
  if (getOpcode10 (instr16) == TRAP_INSTR)
    {
      fIsTargetRunning = false;
      haltAllThreads ();
      redirectStdioOnTrap (thread, getTrap (instr16));
      return true;
    }
  else
    return false;

}	// doFileIO ()


//-----------------------------------------------------------------------------
//! Handle a RSP write memory (binary) request

//! Syntax is:

//!   X<addr>,<length>:

//! Followed by the specified number of bytes as raw binary. Response should be
//! "OK" if all copied OK, E<nn> if error <nn> has occurred.

//! The length given is the number of bytes to be written. The data buffer has
//! already been unescaped, so will hold this number of bytes.

//! The data is in model-endian format, so no transformation is needed.

//! @todo This implementation writes everything as individual bytes/words. A
//!       more efficient implementation would stream the accesses, thereby
//!       saving one cycle/word.
//-----------------------------------------------------------------------------
void
GdbServer::rspWriteMemBin ()
{
  Thread *thread = currentGThread;

  uint32_t addr;		// Where to write the memory
  int len;			// Number of bytes to write

  if (2 != sscanf (pkt->data, "X%x,%x:", &addr, &len))
    {
      cerr << "Warning: Failed to recognize RSP write memory command: %s"
	<< pkt->data << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  // Find the start of the data and "unescape" it. Bindat must be unsigned, or
  // all sorts of horrible sign extensions will happen when val is computed
  // below!
  uint8_t *bindat =
    (uint8_t *) (memchr (pkt->data, ':', pkt->getBufSize ())) + 1;
  int off = (char *) bindat - pkt->data;
  int newLen = Utils::rspUnescape ((char *) bindat, pkt->getLen () - off);

  // Sanity check
  if (newLen != len)
    {
      int minLen = len < newLen ? len : newLen;

      cerr << "Warning: Write of " << len << " bytes requested, but "
	<< newLen << " bytes supplied. " << minLen << " will be written" <<
	endl;
      len = minLen;
    }

  //cerr << "rspWriteMemBin" << hex << addr << dec << " (" << len << ")" << endl;
  if (! thread->writeMemBlock (addr, bindat, len))
    {
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  pkt->packStr ("OK");
  rsp->putPkt (pkt);
}				// rspWriteMemBin()


//-----------------------------------------------------------------------------
//! Handle a RSP remove breakpoint or matchpoint request

//! For now only memory breakpoints are implemented, which are implemented by
//! substituting a breakpoint at the specified address. The implementation must
//! cope with the possibility of duplicate packets.

//! @todo This doesn't work with icache/immu yet
//-----------------------------------------------------------------------------
void
GdbServer::rspRemoveMatchpoint ()
{
  MpType type;			// What sort of matchpoint
  uint32_t addr;		// Address specified
  uint16_t instr;		// Instruction value found
  unsigned int len;		// Matchpoint length

  // Break out the instruction
  if (3 != sscanf (pkt->data, "z%1d,%x,%1ud", (int *) &type, &addr, &len))
    {
      cerr << "Warning: RSP matchpoint deletion request not "
	<< "recognized: ignored" << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  // Sanity check that the length is that of a BKPT instruction
  if (SHORT_INSTRLEN != len)
    {
      cerr << "Warning: RSP matchpoint deletion length " << len
	<< " not valid: " << SHORT_INSTRLEN << " assumed" << endl;
      len = SHORT_INSTRLEN;
    }

  // Sort out the type of matchpoint
  switch (type)
    {
    case BP_MEMORY:
      // Memory breakpoint - replace the original instruction. If we are
      // talking about all threads, we need to do all the threads.
      if (NULL == currentCThread)
	{
	  for (set <Thread*>::iterator it = mCurrentProcess->threadBegin ();
	       it != mCurrentProcess->threadEnd ();
	       it++)
	    {
	      if (mpHash->remove (type, addr, tid, &instr))
		(*it)->writeMem16 (addr, instr);
	    }
	}
      else
	{
	  if (mpHash->remove (type, addr, currentCThread, &instr))
	    thread->writeMem16 (addr, instr);
	}

      pkt->packStr ("OK");
      rsp->putPkt (pkt);
      return;

    case BP_HARDWARE:
      pkt->packStr ("");	// Not supported
      rsp->putPkt (pkt);
      return;

    case WP_WRITE:
      pkt->packStr ("");	// Not supported
      rsp->putPkt (pkt);
      return;

    case WP_READ:
      pkt->packStr ("");	// Not supported
      rsp->putPkt (pkt);
      return;

    case WP_ACCESS:
      pkt->packStr ("");	// Not supported
      rsp->putPkt (pkt);
      return;

    default:
      cerr << "Warning: RSP matchpoint type " << type
	<< " not recognized: ignored" << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
}	// rspRemoveMatchpoint()


//---------------------------------------------------------------------------*/
//! Handle a RSP insert breakpoint or matchpoint request

//! For now only memory breakpoints are implemented, which are implemented by
//! substituting a breakpoint at the specified address. The implementation must
//! cope with the possibility of duplicate packets.

//! @todo This doesn't work with icache/immu yet
//---------------------------------------------------------------------------*/
void
GdbServer::rspInsertMatchpoint ()
{
  MpType type;			// What sort of matchpoint
  uint32_t addr;		// Address specified
  unsigned int len;		// Matchpoint length (not used)

  // Break out the instruction
  if (3 != sscanf (pkt->data, "Z%1d,%x,%1ud", (int *) &type, &addr, &len))
    {
      cerr << "Warning: RSP matchpoint insertion request not "
	<< "recognized: ignored" << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  // Sanity check that the length is that of a BKPT instruction
  if (SHORT_INSTRLEN != len)
    {
      cerr << "Warning: RSP matchpoint insertion length " << len
	<< " not valid: " << SHORT_INSTRLEN << " assumed" << endl;
      len = SHORT_INSTRLEN;
    }

  // Sort out the type of matchpoint
  uint16_t bpMemVal;
  //bool bpMemValSt;
  switch (type)
    {
    case BP_MEMORY:
      // Memory breakpoint - substitute a BKPT instruction If we are
      // talking about all threads, we need to do all the threads.
      if (-1 == currentCThread)
	{
	  for (set <Thread *>::iterator it = mCurrentProcess->threadBegin ();
	       it != mCurrentProcess->threadEnd ();
	       it++)
	    {
	      Thread *thread = *it;

	      thread->readMem16 (addr, bpMemVal);
	      mpHash->add (type, addr, tid, bpMemVal);
	      thread->insertBreakpoint (addr);
	    }
	}
      else
	{
	  thread->readMem16 (addr, bpMemVal);
	  mpHash->add (type, addr, currentCThread, bpMemVal);

	  thread->insertBreakpoint (addr);
	}


      pkt->packStr ("OK");
      rsp->putPkt (pkt);
      return;

    case BP_HARDWARE:
      pkt->packStr ("");	// Not supported
      rsp->putPkt (pkt);
      return;

    case WP_WRITE:
      pkt->packStr ("");	// Not supported
      rsp->putPkt (pkt);
      return;

    case WP_READ:
      pkt->packStr ("");	// Not supported
      rsp->putPkt (pkt);
      return;

    case WP_ACCESS:
      pkt->packStr ("");	// Not supported
      rsp->putPkt (pkt);
      return;

    default:
      cerr << "Warning: RSP matchpoint type " << type
	<< "not recognized: ignored" << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }
}	// rspInsertMatchpoint()


//-----------------------------------------------------------------------------
//! Sotware reset of the processor

//! This is achieved by repeatedly writing 1 (RESET exception) and finally 0
//! to the  RESETCORE_REGNUM register

//! @todo Should this just be the current thread, or the current process, or
//!       all cores?
//-----------------------------------------------------------------------------
void
GdbServer::targetSwReset ()
{
  for (unsigned ncyclesReset = 0; ncyclesReset < 12; ncyclesReset++)
    (void) currentCThread->writeReg (RESETCORE_REGNUM, 1);

  (void) currentCThread->writeReg (RESETCORE_REGNUM, 0);

}	// targetSWreset()


//-----------------------------------------------------------------------------
//! HW specific (board) reset

//! The Platform driver is responsible for the actual implementation
//-----------------------------------------------------------------------------
void
GdbServer::targetHWReset ()
{
  fTargetControl->platformReset ();
}				// hw_reset, ESYS_RESET


//-----------------------------------------------------------------------------
//! Stop all attached processes

//! Required when switching to all-stop mode. For now we can't attach to more
//! than one process, so it is a null operation.

//! @todo Make this work for multiple attached processes.
//-----------------------------------------------------------------------------
void
GdbServer::stopAttachedProcesses ()
{
  // Nothing for now

}	// stopAttachedProcesses ()


//-----------------------------------------------------------------------------
//! Get a 1-bit plus 4-bit opcode field from a 32-bit instruction

//! @param[in] instr  The instruction
//! @return  The opcode
//-----------------------------------------------------------------------------
uint32_t
GdbServer::getOpcode1_4 (uint32_t  instr)
{
  return instr & 0x0200000f;

}	// getOpcode1_4 ()


//-----------------------------------------------------------------------------
//! Get a 1-bit plus 5-bit opcode field from a 32-bit instruction

//! @param[in] instr  The instruction
//! @return  The opcode
//-----------------------------------------------------------------------------
uint32_t
GdbServer::getOpcode1_5 (uint32_t  instr)
{
  return instr & 0x1000001f;

}	// getOpcode15 ()


//-----------------------------------------------------------------------------
//! Get a 2-bit plus 4-bit opcode field from a 16-bit instruction

//! @param[in] instr  The instruction
//! @return  The opcode
//-----------------------------------------------------------------------------
uint16_t
GdbServer::getOpcode2_4 (uint16_t  instr)
{
  return instr & 0x030f;

}	// getOpcode24 ()


//-----------------------------------------------------------------------------
//! Get a 2-bit plus 4-bit opcode field from a 32-bit instruction

//! @param[in] instr  The instruction
//! @return  The opcode
//-----------------------------------------------------------------------------
uint32_t
GdbServer::getOpcode2_4 (uint32_t  instr)
{
  return instr & 0x0060000f;

}	// getOpcode24 ()


//-----------------------------------------------------------------------------
//! Get a 4-bit opcode field from a 16-bit instruction

//! @param[in] instr  The instruction
//! @return  The opcode
//-----------------------------------------------------------------------------
uint16_t
GdbServer::getOpcode4 (uint16_t  instr)
{
  return instr & 0x000f;

}	// getOpcode4 ()


//-----------------------------------------------------------------------------
//! Get a 4-bit opcode field from a 32-bit instruction

//! @param[in] instr  The instruction
//! @return  The opcode
//-----------------------------------------------------------------------------
uint32_t
GdbServer::getOpcode4 (uint32_t  instr)
{
  return instr & 0x0000000f;

}	// getOpcode4 ()


//-----------------------------------------------------------------------------
//! Get a 4-bit plus 2-bit + 4-bit opcode field from a 32-bit instruction

//! @param[in] instr  The instruction
//! @return  The opcode
//-----------------------------------------------------------------------------
uint32_t
GdbServer::getOpcode4_2_4 (uint32_t  instr)
{
  return instr & 0x000f030f;

}	// getOpcode4_2_4 ()


//-----------------------------------------------------------------------------
//! Get a 4-bit plus 5-bit opcode field from a 32-bit instruction

//! @param[in] instr  The instruction
//! @return  The opcode
//-----------------------------------------------------------------------------
uint32_t
GdbServer::getOpcode4_5 (uint32_t  instr)
{
  return instr & 0x000f001f;

}	// getOpcode4_5 ()


//-----------------------------------------------------------------------------
//! Get a 4-bit plus 7-bit opcode field from a 32-bit instruction

//! @param[in] instr  The instruction
//! @return  The opcode
//-----------------------------------------------------------------------------
uint32_t
GdbServer::getOpcode4_7 (uint32_t  instr)
{
  return instr & 0x000f007f;

}	// getOpcode4_7 ()


//-----------------------------------------------------------------------------
//! Get a 4-bit plus 10-bit opcode field from a 32-bit instruction

//! @param[in] instr  The instruction
//! @return  The opcode
//-----------------------------------------------------------------------------
uint32_t
GdbServer::getOpcode4_10 (uint32_t  instr)
{
  return instr & 0x000f03ff;

}	// getOpcode4_10 ()


//-----------------------------------------------------------------------------
//! Get a 5-bit opcode field from a 16-bit instruction

//! @param[in] instr  The instruction
//! @return  The opcode
//-----------------------------------------------------------------------------
uint16_t
GdbServer::getOpcode5 (uint16_t  instr)
{
  return instr & 0x001f;

}	// getOpcode5 ()


//-----------------------------------------------------------------------------
//! Get a 5-bit opcode field from a 32-bit instruction

//! @param[in] instr  The instruction
//! @return  The opcode
//-----------------------------------------------------------------------------
uint32_t
GdbServer::getOpcode5 (uint32_t  instr)
{
  return instr & 0x0000001f;

}	// getOpcode5 ()


//-----------------------------------------------------------------------------
//! Get a 7-bit opcode field from a 16-bit instruction

//! @param[in] instr  The instruction
//! @return  The opcode
//-----------------------------------------------------------------------------
uint16_t
GdbServer::getOpcode7 (uint16_t  instr)
{
  return instr & 0x007f;

}	// getOpcode7 ()


//-----------------------------------------------------------------------------
//! Get a 7-bit opcode field from a 32-bit instruction

//! @param[in] instr  The instruction
//! @return  The opcode
//-----------------------------------------------------------------------------
uint32_t
GdbServer::getOpcode7 (uint32_t  instr)
{
  return instr & 0x0000007f;

}	// getOpcode7 ()


//-----------------------------------------------------------------------------
//! Get a 10-bit opcode field from a 16-bit instruction

//! @param[in] instr  The instruction
//! @return  The opcode
//-----------------------------------------------------------------------------
uint16_t
GdbServer::getOpcode10 (uint16_t  instr)
{
  return instr & 0x03ff;

}	// getOpcode10 ()


//-----------------------------------------------------------------------------
//! Get a 10-bit opcode field from a 32-bit instruction

//! @param[in] instr  The instruction
//! @return  The opcode
//-----------------------------------------------------------------------------
uint32_t
GdbServer::getOpcode10 (uint32_t  instr)
{
  return instr & 0x000003ff;

}	// getOpcode10 ()


//-----------------------------------------------------------------------------
//! Get the Rd field for a 16-bit instruction

//! @param[in] instr  The instruction
//! @return  The register number
//-----------------------------------------------------------------------------
uint8_t
GdbServer::getRd (uint16_t  instr)
{
  return (uint8_t) ((instr & 0xe000) >> 13);

}	// getRd ()


//-----------------------------------------------------------------------------
//! Get the Rd field for a 32-bit instruction

//! @param[in] instr  The instruction
//! @return  The register number
//-----------------------------------------------------------------------------
uint8_t
GdbServer::getRd (uint32_t  instr)
{
  uint8_t lo = (uint8_t) ((instr & 0x0000e000) >> 13);
  uint8_t hi = (uint8_t) ((instr & 0xe0000000) >> 29);
  return  (hi << 3) | lo;

}	// getRd ()


//-----------------------------------------------------------------------------
//! Get the Rm field for a 16-bit instruction

//! @param[in] instr  The instruction
//! @return  The register number
//-----------------------------------------------------------------------------
uint8_t
GdbServer::getRm (uint16_t  instr)
{
  return (uint8_t) ((instr & 0x0380) >> 7);

}	// getRm ()


//-----------------------------------------------------------------------------
//! Get the Rm field for a 32-bit instruction

//! @param[in] instr  The instruction
//! @return  The register number
//-----------------------------------------------------------------------------
uint8_t
GdbServer::getRm (uint32_t  instr)
{
  uint8_t lo = (uint8_t) ((instr & 0x00000380) >>  7);
  uint8_t hi = (uint8_t) ((instr & 0x03800000) >> 23);
  return  (hi << 3) | lo;

}	// getRm ()


//-----------------------------------------------------------------------------
//! Get the Rn field for a 16-bit instruction

//! @param[in] instr  The instruction
//! @return  The register number
//-----------------------------------------------------------------------------
uint8_t
GdbServer::getRn (uint16_t  instr)
{
  return (uint8_t) ((instr & 0x1c00) >> 10);

}	// getRn ()


//-----------------------------------------------------------------------------
//! Get the Rn field for a 32-bit instruction

//! @param[in] instr  The instruction
//! @return  The register number
//-----------------------------------------------------------------------------
uint8_t
GdbServer::getRn (uint32_t  instr)
{
  uint8_t lo = (uint8_t) ((instr & 0x00001c00) >> 10);
  uint8_t hi = (uint8_t) ((instr & 0x1c000000) >> 26);
  return  (hi << 3) | lo;

}	// getRn ()


//-----------------------------------------------------------------------------
//! Get the trap number from a TRAP instruction

//! @param[in] instr  The TRAP instruction
//! @return  The trap number
//-----------------------------------------------------------------------------
uint8_t
GdbServer::getTrap (uint16_t  instr)
{
  return (uint8_t) ((instr & 0xfc00) >> 10);

}	// getTrap ()


//-----------------------------------------------------------------------------
//! Get the offset from a 16-bit branch instruction

//! @param[in] instr  The branch instruction
//! @return  The (signed) branch offset in bytes
//-----------------------------------------------------------------------------
int32_t
GdbServer::getBranchOffset (uint16_t  instr)
{
  int32_t raw = (int32_t) (instr >> 8);
  return ((raw ^ 0x80) - 0x80) << 1;		// Sign extend and double

}	// getBranchOffset ()


//-----------------------------------------------------------------------------
//! Get the offset from a 32-bit branch instruction

//! @param[in] instr  The branch instruction
//! @return  The (signed) branch offset in bytes
//-----------------------------------------------------------------------------
int32_t
GdbServer::getBranchOffset (uint32_t  instr)
{
  int32_t raw = (int32_t) (instr >> 8);
  return ((raw ^ 0x800000) - 0x800000) << 1;	// Sign extend and double

}	// getBranchOffset ()


//-----------------------------------------------------------------------------
//! Get a 16-bit jump destination

//! Possibilites are a branch (immediate offset), jump (register address) or
//! return-from-interrupt (implicit register address).

//! @param[in]  thread    The thread we are looking at.
//! @param[in]  instr     16-bit instruction
//! @param[in]  addr      Address of instruction being examined
//! @param[out] destAddr  Destination address
//! @return  TRUE if this was a 16-bit jump destination
//-----------------------------------------------------------------------------
bool
GdbServer::getJump (Thread*   thread,
		    uint16_t  instr,
		    uint32_t  addr,
		    uint32_t& destAddr)
{
  if (0x0000 == getOpcode4 (instr))
    {
      // Bcc
      int32_t offset = getBranchOffset (instr);
      destAddr = addr + offset;
      return true;
    }
  else if (   (0x142 == getOpcode10 (instr))
	   || (0x152 == getOpcode10 (instr)))
    {
      // JR or JALR
      uint8_t rn = getRn (instr);
      destAddr = thread->readReg (R0_REGNUM + rn);
      return true;
    }
  else if (0x1d2 == getOpcode10 (instr))
    {
      // RTI
      destAddr = thread->readReg (IRET_REGNUM);
      return true;
    }
  else
    return false;

}	// getJump ()


//-----------------------------------------------------------------------------
//! Get a 32-bit jump destination

//! Possibilites are a branch (immediate offset) or jump (register address)

//! @param[in]  thread    The thread we are looking at.
//! @param[in]  instr     32-bit instruction
//! @param[in]  addr      Address of instruction being examined
//! @param[out] destAddr  Destination address
//! @return  TRUE if this was a 16-bit jump destination
//-----------------------------------------------------------------------------
bool
GdbServer::getJump (Thread*   thread,
		    uint32_t  instr,
		    uint32_t  addr,
		    uint32_t& destAddr)
{
  if (0x00000008 == getOpcode4 (instr))
    {
      // Bcc
      int32_t offset = getBranchOffset (instr);
      destAddr = addr + offset;
      return true;
    }
  else if (   (0x0002014f == getOpcode4_10 (instr))
	   || (0x0002015f == getOpcode4_10 (instr)))
    {
      // JR or JALR
      uint8_t rn = getRn (instr);
      destAddr = thread->readReg (R0_REGNUM + rn);
      return true;
    }
  else
    return false;

}	// getJump ()


//-----------------------------------------------------------------------------
//! Do a backtrace
//-----------------------------------------------------------------------------
void
GdbServer::doBacktrace ()
{
  void* calls [100];
  int nCalls = backtrace (calls, sizeof (calls) / sizeof (void *));
  char **symbols = backtrace_symbols (calls, nCalls);

  for (int  i = 1; i < nCalls; i++)
    {
      stringstream ss (symbols[i]);
      string fileName;
      string location;
      string address;
      getline (ss, fileName, '(');
      getline (ss, location, '[');
      getline (ss, address, ']');

      ostringstream oss;
      oss << "/opt/adapteva/esdk/tools/e-gnu/bin/e-addr2line -C -f -e "
	  << fileName << " " << address
	  << " | sed -e :a -e '$!N; s/\\n/ /; ta' | sed -e 's#/.*/##'";
      cout << i << ": " << flush;
      if (0 != system (oss.str ().c_str ()))
	cerr << "Warning: Could not translate address" << endl;
    }

  free (symbols);

}	// doBacktrace ()


// These functions replace the intrinsic SystemC bitfield operators.
uint8_t
GdbServer::getfield (uint8_t x, int _lt, int _rt)
{
  return (x & ((1 << (_lt + 1)) - 1)) >> _rt;
}


uint16_t
GdbServer::getfield (uint16_t x, int _lt, int _rt)
{
  return (x & ((1 << (_lt + 1)) - 1)) >> _rt;
}


uint32_t
GdbServer::getfield (uint32_t x, int _lt, int _rt)
{
  return (x & ((1 << (_lt + 1)) - 1)) >> _rt;
}


uint64_t
GdbServer::getfield (uint64_t x, int _lt, int _rt)
{
  return (x & ((1 << (_lt + 1)) - 1)) >> _rt;
}


void
GdbServer::setfield (uint32_t & x, int _lt, int _rt, uint32_t val)
{
  uint32_t mask;

  mask = ((1 << (_lt - _rt + 1)) - 1) << _rt;

  x = (x & (~mask)) | (val << _rt);

  return;
}


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// show-trailing-whitespace: t
// End:
