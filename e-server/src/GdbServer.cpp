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
  currentCTid (0),
  currentGTid (0),
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
	  else
	    {
	      cout << "INFO: connected to port " << si->port () << endl;

	      // All-stop mode requires we halt on attaching. This will return
	      // the appropriate stop packet.
	      rspAttach (currentPid);
	    }
	}


      // Get a RSP client request
      if (si->debugTranDetail ())
	cerr << "DebugTranDetail: Getting RSP client request." << endl;

      rspClientRequest ();

      // At this point we should have responded to the client, and so, in
      // all-stop mode, all threads should be halted.
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
  mIdleProcess = new ProcessInfo;
  mNextPid = IDLE_PID;
  pair <int, ProcessInfo *> entry (mNextPid, mIdleProcess);
  bool res = mProcesses.insert (entry).second;
  assert (res);
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
      Thread *thread = new Thread (coreId, fTargetControl, si);

      mThreads[tid] = thread;
      mCore2Tid [coreId] = tid;

      // Add to idle process
      res = mIdleProcess->addThread (tid);
      assert (res);
    }

  currentPid = IDLE_PID;

}	// initProcesses ()


//-----------------------------------------------------------------------------
//! Attach to the target

//! If not already halted, the target will be halted. We send an appropriate
//! stop packet back to the client.

//! @todo What should we really do if the target fails to halt?

//! @note  The target should *not* be reset when attaching.

//! @param[in] pid  The ID of the process to which we attach
//-----------------------------------------------------------------------------
void
GdbServer::rspAttach (int  pid)
{
  map <int, ProcessInfo *>::iterator  it = mProcesses.find (pid);
  assert (it != mProcesses.end ());
  ProcessInfo* process = it->second;
  bool isHalted = true;

  for (set <int>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    {
      int tid = *it;
      Thread *thread = getThread (tid);
      isHalted &= thread->halt ();

      if (thread->isIdle ())
	{
	  if (thread->activate ())
	    {
	      if (si->debugStopResumeDetail ())
		cerr << "DebugStopResumeDetail: Thread " << tid
		     << " idle on attach: forced active." << endl;
	    }
	  else
	    {
	      if (si->debugStopResumeDetail ())
		cerr << "DebugStopResumeDetail: Thread " << tid
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

//! @param[in] pid  The ID of the process from which we detach
//-----------------------------------------------------------------------------
void
GdbServer::rspDetach (int pid)
{
  if (IDLE_PID != pid)
    {
      map <int, ProcessInfo *>::iterator  it = mProcesses.find (pid);
      assert (it != mProcesses.end ());
      ProcessInfo* process = it->second;

      for (set <int>::iterator it = process->threadBegin ();
	   it != process->threadEnd ();
	   it++)
	{
	  Thread* thread = getThread (*it);
	  thread->resume ();
	}
    }
}	// rspDetach ()


//-----------------------------------------------------------------------------
//! Called when we get a packet that we don't support.  Returns back
//| the empty packet, as per RSP specification.

void
GdbServer::rspUnknownPacket ()
{
  if (si->debugTranDetail ())
    cerr << "Warning: Unknown RSP request" << pkt->data << endl;
  pkt->packStr ("");
  rsp->putPkt (pkt);
}	// rspUnknownPacket ()


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
  if (!rsp->getPkt (pkt))
    {
      rspDetach (currentPid);
      rsp->rspClose ();		// Comms failure
      return;
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

    case 'c':
      // Continue
      rspContinue (TARGET_SIGNAL_NONE);
      break;

    case 'C':
      // Continue with signal (in the packet)
      rspContinue ();
      break;

    case 'D':
      // Detach GDB. Do this by closing the client. The rules say that
      // execution should continue, so unstall the processor.
      rspDetach (currentPid);
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

    case 'k':
      rspDetach (currentPid);
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

    case 'R':
      // Restart the program being debugged.
      rspRestart ();
      break;

    case 's':
      // Single step one machine instruction.
      rspStep ();
      break;

    case 'S':
      // Single step one machine instruction with signal
      rspStep ();
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
      rspUnknownPacket ();
      break;
    }
}				// rspClientRequest()


//-----------------------------------------------------------------------------
//! Send a packet acknowledging an exception has occurred

//! @param[in] tid  The thread ID we are acknowledging.
//! @param[in] sig  The signal to report back.
//-----------------------------------------------------------------------------
void
GdbServer::rspReportException (int          tid,
			       TargetSignal sig)
{
  if (si->debugStopResume ())
    cerr << "DebugStopResume: Report exception  for thread " << tid
	 << " with GDB signal " << sig << endl;

  ostringstream oss;

  // Construct a signal received packet. Use S for all threads, T otherwise.
  switch (tid)
    {
    case -1:
      // All threads have stopped
      oss << "S" << Utils::intStr (sig, 16, 2);
      break;

    case 0:
      // Weird. This isn't a sensible thread to return
      cerr << "Warning: Attempt to report thread 0 stopped: Using -1 instead."
	   << endl;
      oss << "S" << Utils::intStr (sig, 16, 2);
      break;

    default:
      // A specific thread has stopped
      oss << "T" << Utils::intStr (sig, 16, 2) << "thread:" << hex << tid
	  << ";";
      break;
    }

  // In all-stop mode, ensure all threads are halted.
  haltAllThreads ();
  fIsTargetRunning = false;

  pkt->packStr (oss.str ().c_str ());
  rsp->putPkt (pkt);

}	// rspReportException()


//-----------------------------------------------------------------------------
//! Handle a RSP continue request

//! This version is typically used for the 'c' packet, to continue without
//! signal, in which case TARGET_SIGNAL_NONE is passed in as the exception to
//! use.

//! At present other exceptions are not supported

//! @param[in] except  The GDB signal to use
//-----------------------------------------------------------------------------
void
GdbServer::rspContinue (uint32_t except __attribute ((unused)) )
{
  uint32_t     addr;		// Address to continue from, if any
  uint32_t  hexAddr;		// Address supplied in packet
  Thread *thread = getThread (currentCTid);

  // Reject all except 'c' packets
  if ('c' != pkt->data[0])
    {
      cerr << "Warning: Continue with signal not currently supported: "
	<< "ignored" << endl;
      return;
    }

  // Get an address if we have one
  if (0 == strcmp ("c", pkt->data))
    addr = thread->readPc ();		// Default uses current PC
  else if (1 == sscanf (pkt->data, "c%" SCNx32, &hexAddr))
    addr = hexAddr;
  else
    {
      cerr << "Warning: RSP continue address " << pkt->data
	<< " not recognized: ignored" << endl;
      addr = thread->readPc ();		// Default uses current NPC
    }

  rspContinue (addr, TARGET_SIGNAL_NONE);

}				// rspContinue()


//-----------------------------------------------------------------------------
//! Handle a RSP continue with signal request

//! @todo Currently does nothing. Will use the underlying generic continue
//!       function.
//-----------------------------------------------------------------------------
void
GdbServer::rspContinue ()
{
  Thread* thread = getThread (currentCTid);

  if (si->debugTrapAndRspCon ())
    cerr << "RSP continue with signal '" << pkt->
      data << "' received" << endl;

  //return the same exception
  TargetSignal sig = TARGET_SIGNAL_TRAP;

  if ((0 == strcmp ("C03", pkt->data)))
    {				//get continue with signal after reporting QUIT/exit, silently ignore

      sig = TARGET_SIGNAL_QUIT;

    }
  else
    {
      cerr << "WARNING: RSP continue with signal '" << pkt->
	data << "' received, the server will ignore the continue" << endl;

      //check the exception state
      sig = (TargetSignal) thread->getException ();
    }

  // report to gdb the target has been stopped
  rspReportException (-1 /* all threads */ , sig);

}	// rspContinue()


//-----------------------------------------------------------------------------
//! Generic processing of a continue request

//! The signal may be TARGET_SIGNAL_NONE if there is no exception to be
//! handled. Currently the exception is ignored.

//! @param[in] addr    Address from which to step
//! @param[in] except  The exception to use (if any). Currently ignored
//-----------------------------------------------------------------------------
void
GdbServer::rspContinue (uint32_t addr, uint32_t except __attribute ((unused)))
{
  Thread* thread = getThread (currentCTid);

  if ((!fIsTargetRunning && si->debugStopResume ()) || si->debugTranDetail ())
    {
      cerr << "GdbServer::rspContinue PC 0x" << hex << addr << dec << endl;
    }

  uint32_t prevPc = 0;

  if (!fIsTargetRunning)
    {
      if (! thread->isHalted ())
	{
	  //cerr << "********* isTargetInDebugState = false **************" << endl;

	  //cerr << "Internal Error(DGB server): Core is not in HALT state while the GDB is asking the cont" << endl;
	  //pkt->packStr("E01");
	  //rsp->putPkt(pkt);
	  //exit(2);

	  fIsTargetRunning = true;
	}
      else
	{
	  //cerr << "********* isTargetInDebugState = true **************" << endl;

	  //set PC
	  thread->writePc (addr);

	  //resume
	  thread->resume ();
	}
    }

  unsigned long timeout_me = 0;
  unsigned long timeout_limit = 100;

  timeout_limit = 3;

  while (true)
    {
      //cerr << "********* while true **************" << endl;

      Utils::microSleep (300000);		// 300 ms

      timeout_me += 1;

      //give up control and check for CTRL-C
      if (timeout_me > timeout_limit)
	{
	  //cerr << "********* timeout me > limit **************" << endl;
	  //cerr << " PC << " << hex << readPc() << dec << endl;
	  assert (fIsTargetRunning);
	  break;
	}

      //check the value of debug register

      if (thread->isHalted ())
	{
	  //cerr << "********* isTargetInDebugState = true **************" << endl;

	  // If it's a breakpoint, then we need to back up one instruction, so
	  // on restart we execute the actual instruction.
	  uint32_t c_pc = thread->readPc ();
	  //cout << "stopped at @pc " << hex << c_pc << dec << endl;
	  prevPc = c_pc - SHORT_INSTRLEN;

	  //check if it is trap
	  uint16_t val_;
	  thread->readMem16 (prevPc, val_);
	  uint16_t valueOfStoppedInstr = val_;

	  if (valueOfStoppedInstr == BKPT_INSTR)
	    {
	      //cerr << "********* valueOfStoppedInstr = BKPT_INSTR **************" << endl;

	      if (mpHash->lookup (BP_MEMORY, prevPc, currentCTid))
		{
		  thread->writePc (prevPc);
		  if (si->debugTrapAndRspCon ())
		    cerr << dec << "set pc back " << hex << prevPc << dec << endl
		     ;
		}

	      if (si->debugTrapAndRspCon ())
		cerr <<
		  dec << "After wait CONT GdbServer::rspContinue PC 0x" <<
		  hex << prevPc << dec << endl;

	      // report to gdb the target has been stopped


	      rspReportException (-1 /*all threads */ , TARGET_SIGNAL_TRAP);



	    }
	  else
	    {			// check if stopped for trap (stdio handling)
	      //cerr << "********* valueOfStoppedInstr =\\= BKPT_INSTR **************" << endl;

	      bool stoppedAtTrap =
		(getfield (valueOfStoppedInstr, 9, 0) == TRAP_INSTR);
	      if (!stoppedAtTrap)
		{
		  //cerr << "********* stoppedAtTrap = false **************" << endl;
		  //try to go back an look for trap // bug in the design !!!!!!!!!!!!!!
		  if (si->debugTrapAndRspCon ())
		    cerr << dec << "missed trap ... looking backward for trap "
		      << hex << c_pc << dec << endl;


		  if (valueOfStoppedInstr == NOP_INSTR)
		    {		//trap is always padded by nops
		      for (uint32_t j = prevPc - 2; j > prevPc - 20;
			   j = j - 2 /* length of */ )
			{
			  //check if it is trap

			  thread->readMem16 (j, val_);
			  valueOfStoppedInstr = val_;

			  stoppedAtTrap =
			    (getfield (valueOfStoppedInstr, 9, 0) ==
			     TRAP_INSTR);
			  if (stoppedAtTrap)
			    {
			      if (si->debugStopResumeDetail ())
				cerr << dec <<
				  "trap found @" << hex << j << dec << endl;
			      break;

			    }
			}
		    }


		}

	      if (stoppedAtTrap)
		{
		  //cerr << "********* stoppedAtTrap = true **************" << endl;
		  haltAllThreads ();
		  fIsTargetRunning = false;
		  redirectStdioOnTrap (thread,
				       getTrap (valueOfStoppedInstr));
		}
	      else
		{
		  //cerr << "********* stoppedAtTrap = false **************" << endl;
		  if (si->debugStopResumeDetail ())
		    cerr << dec << " no trap found, return control to gdb" <<
		      endl;
		  // report to gdb the target has been stopped
		  rspReportException (-1 /* all threads */ ,
				      TARGET_SIGNAL_TRAP);
		}
	    }

	  break;
	}			// if (isCoreInDebugState())
    }				// while (true)
}	// rspContinue()


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
  Thread* thread = getThread (currentCTid);

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
  Thread *thread = getThread (currentGTid);
  // Start timing if debugging
  if (si->debugStopResumeDetail ())
    fTargetControl->startOfBaudMeasurement ();

  // Get each reg
  for (unsigned int r = 0; r < NUM_REGS; r++)
    {
      uint32_t val;
      unsigned int pktOffset = r * TargetControl::E_REG_BYTES * 2;

      // Not all registers are necessarily supported.
      if (thread->readReg (r, val))
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
  Thread* thread = getThread (currentGTid);
  // All registers
  for (unsigned int r = 0; r < NUM_REGS; r++)
      (void) thread->writeReg (r, Utils::hex2Reg (&(pkt->data[r * 8])));

  // Acknowledge (always OK for now).
  pkt->packStr ("OK");
  rsp->putPkt (pkt);

}				// rspWriteAllRegs()


//-----------------------------------------------------------------------------
//! Set the thread number of subsequent operations.

//! A tid of -1 means "all threads", 0 means "any thread". 0 causes all sorts of
//! problems later, so we replace it by the first thread in the current
//! process.
//-----------------------------------------------------------------------------
void
GdbServer::rspSetThread ()
{
  char  c;
  int  tid;

  if (2 != sscanf (pkt->data, "H%c%x:", &c, &tid))
    {
      cerr << "Warning: Failed to recognize RSP set thread command: "
	   << pkt->data << endl;
      pkt->packStr ("E01");
      rsp->putPkt (pkt);
      return;
    }

  if (0 == tid)
    tid = *(getProcess (currentPid)->threadBegin ());

  switch (c)
    {
    case 'c': currentCTid = tid; break;
    case 'g': currentGTid = tid; break;

    default:
      cerr << "Warning: Failed RSP set thread command: "
	   << pkt->data << endl;
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
  Thread *thread = getThread (currentGTid);
  unsigned int addr;		// Where to read the memory
  int len;			// Number of bytes to read
  int off;			// Offset into the memory

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
      thread->readMemBlock (addr, (unsigned char *) buf, len);

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
  Thread *thread = getThread (currentGTid);
  uint32_t addr;		// Where to write the memory
  int len;			// Number of bytes to write

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
    if (!thread->writeMemBlock (addr, (unsigned char *) symDat, len))
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
  Thread *thread = getThread (currentGTid);
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
  if (!thread->readReg (regnum, regval))
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
  Thread *thread = getThread (currentGTid);
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
  if (! thread->writeReg (regnum, Utils::hex2Reg (valstr)))
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

      sprintf (pkt->data, "QC%x", currentGTid);
      pkt->setLen (strlen (pkt->data));
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
      sprintf (pkt->data, "PacketSize=%x;qXfer:osdata:read+",
	       pkt->getBufSize ());
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);
    }
  else if (0 == strncmp ("qThreadExtraInfo,", pkt->data,
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
      ProcessInfo *process = getProcess (currentPid);

      // Iterate all the threads in the core
      for (set <int>::iterator tit = process->threadBegin ();
	   tit != process->threadEnd ();
	   tit++)
	{
	  if (tit != process->threadBegin ())
	    os << ",";

	  os << hex << *tit;
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
      Thread* gThread = getThread (currentGTid);
      Thread* cThread = getThread (currentCTid);
      CoreId absCCoreId = cThread->readCoreId ();
      CoreId absGCoreId = gThread->readCoreId ();
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
  pair <int, ProcessInfo *> entry (pid, process);
  bool res = mProcesses.insert (entry).second;
  assert (res);
  mNextPid++;

  for (unsigned int r = 0; r < rows; r++)
    for (unsigned int c = 0; c < cols; c++)
      {
	CoreId coreId (row + r, col + c);
	int thread = mCore2Tid[coreId];
	if (mIdleProcess->eraseThread (thread))
	  {
	    res = process->addThread (thread);
	    assert (res);
	  }
	else
	  {
	    // Yuk - blew up half way. Put all the threads back into the idle
	    // group, delete the process an give up.
	    for (set <int>::iterator it = process->threadBegin ();
		 it != process->threadEnd ();
		 it++)
	      {
		res = process->eraseThread (*it);
		assert (res);
		res = mIdleProcess->addThread (*it);
		assert (res);
	      }

	    --mNextPid;
	    int numRes = mProcesses.erase (mNextPid);
	    assert (numRes == 1);
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

  if (mProcesses.find (pid) == mProcesses.end ())
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
      currentPid = pid;
      ProcessInfo *process = getProcess (pid);

      ostringstream oss;
      oss << "Process ID now " << pid << "." << endl;

      // This may have invalidated the current threads. If so correct
      // them. This is really a big dodgy - ultimately this needs proper
      // process handling.
      if ((-1 != currentCTid) && (! process->hasThread (currentCTid)))
	{
	  currentCTid = *(process->threadBegin ());
	  oss << "- switching control thread to " << currentCTid << "." << endl;
	  pkt->packHexstr (oss.str ().c_str ());
	  rsp->putPkt (pkt);
	}

      if ((-1 != currentGTid) && (! process->hasThread (currentGTid)))
	{
	  currentGTid = *(process->threadBegin ());
	  oss << "- switching general thread to " << currentGTid << "." << endl;
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
      int oldCurrentPid = currentPid;
      for (map <int, ProcessInfo *>::iterator pit = mProcesses.begin ();
	   pit != mProcesses.end ();
	   pit++)
	{
	  currentPid = pit->first;
	  ProcessInfo *process = pit->second;
	  osProcessReply += "  <item>\n"
	    "    <column name=\"pid\">";
	  osProcessReply += Utils::intStr (currentPid);
	  osProcessReply += "</column>\n"
	    "    <column name=\"user\">root</column>\n"
	    "    <column name=\"command\"></column>\n"
	    "    <column name=\"cores\">\n"
	    "      ";

	  for (set <int>::iterator tit = process->threadBegin ();
	       tit != process->threadEnd (); tit++)
	    {
	      Thread* thread = getThread (*tit);

	      if (tit != process->threadBegin ())
		osProcessReply += ",";

	      osProcessReply += thread->coreId ();
	    }

	  osProcessReply += "\n"
	    "    </column>\n"
	    "  </item>\n";
	}
      currentPid = oldCurrentPid;	// Restored
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
  if ((0 == strcmp ("QTStart", pkt->data)))
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
      rspUnknownPacket ();
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
  Thread* thread = getThread (currentCTid);
  thread->writePc (0);

}				// rspRestart()


//-----------------------------------------------------------------------------
//! Handle a RSP step request

//! This may be a 's' packet with optional address or 'S' packet with signal
//! and optional address.
//-----------------------------------------------------------------------------
void
GdbServer::rspStep ()
{
  bool haveAddrP;			// Were we given an address
  uint32_t addr;
  TargetSignal sig;

  // Break out the arguments
  if ('s' == pkt->data[0])
    {
      // Plain step
      sig = TARGET_SIGNAL_NONE;
      // No warning if defective format
      haveAddrP = 1 == sscanf (pkt->data, "s%" SCNx32, &addr);
    }
  else
    {
      // Step with signal
      unsigned int sigval;
      int n = sscanf (pkt->data, "S%x;%" SCNx32, &sigval, &addr);
      switch (n)
	{
	case 1:
	  sig = (TargetSignal) sigval;
	  haveAddrP = false;
	  break;

	case 2:
	  sig = (TargetSignal) sigval;
	  haveAddrP = true;
	  break;

	default:
	  // Defective format
	  cerr << "Warning: Unrecognized step with signal '" << pkt->data
	       << "': Defaults used." << endl;
	  sig = TARGET_SIGNAL_NONE;
	  haveAddrP = false;
	  break;
	}
    }

  rspStep (haveAddrP, addr, sig);

}				// rspStep()


//-----------------------------------------------------------------------------
//! Generic processing of a step request

//! The signal may be TARGET_SIGNAL_NONE if there is no exception to be
//! handled.

//! The single step flag is set in the debug registers which has the effect of
//! unstalling the processor(s) for one instruction.

//! @todo If the current C thread is -1, we need to set up all cores, then
//!       unstall then, wait for one to halt, then stall them.

//! @param[in] haveAddrP  Were we supplied with an address (if not use PC)
//! @param[in] addr       Address from which to step
//! @param[in] sig        The GDB signal to use
//-----------------------------------------------------------------------------
void
GdbServer::rspStep (bool         haveAddrP,
		    uint32_t     addr,
		    TargetSignal sig)
{
  Thread* thread = getThread (currentCTid);	// Bad for 0/-1
  assert (thread->isHalted ());

  if (!haveAddrP)
    {
      // @todo This should be done on a per-core basis for thread -1
      addr = thread->readPc ();
    }

  if (si->debugStopResumeDetail ())
    cerr << dec << "DebugStopResumeDetail: rspStep (" << addr << ", " << sig
	 << ")" << endl;

  if (thread->getException () != TARGET_SIGNAL_NONE)
    {
      // Already stopped due to some exception. Just report to GDB.

      // @todo This is commented as being during to a silicon problem
      rspReportException (currentCTid, sig);
      return;
    }

  // Set the PC to the given address
  thread->writePc (addr);
  assert (thread->readPc () == addr);	// Do we really need this?

  uint16_t instr16 = thread->readMem16 (addr);
  uint16_t opcode = getOpcode10 (instr16);

  // IDLE and TRAP need special treatment
  if (IDLE_INSTR == opcode)
    {
      if (si->debugStopResumeDetail ())
	cerr << dec << "DebugStopResumeDetail: IDLE found at " << addr << "."
	     << endl;

      //check if global ISR enable state
      uint32_t coreStatus = thread->readStatus ();

      uint32_t imaskReg = thread->readReg (IMASK_REGNUM);
      uint32_t ilatReg = thread->readReg (ILAT_REGNUM);

      //next cycle should be jump to IVT
      if (((coreStatus & TargetControl::STATUS_GID_MASK)
	   == TargetControl::STATUS_GID_ENABLED)
	  && (((~imaskReg) & ilatReg) != 0))
	{
	  // Interrupts globally enabled, and at least one individual
	  // interrupt is active and enabled.

	  // Next cycle should be jump to IVT. Take care of ISR call. Put a
	  // breakpoint in each IVT slot except SYNC (aka RESET)
	  thread->saveIVT ();

	  thread->insertBkptInstr (TargetControl::IVT_SWE);
	  thread->insertBkptInstr (TargetControl::IVT_PROT);
	  thread->insertBkptInstr (TargetControl::IVT_TIMER0);
	  thread->insertBkptInstr (TargetControl::IVT_TIMER1);
	  thread->insertBkptInstr (TargetControl::IVT_MSG);
	  thread->insertBkptInstr (TargetControl::IVT_DMA0);
	  thread->insertBkptInstr (TargetControl::IVT_DMA1);
	  thread->insertBkptInstr (TargetControl::IVT_WAND);
	  thread->insertBkptInstr (TargetControl::IVT_USER);

	  // Resume which should hit the breakpoint in the IVT.
	  thread->resume ();

	  while (!thread->isHalted ())
	    ;

	  //restore IVT
	  thread->restoreIVT ();

	  // @todo The old code had reads of STATUS, IMASK and ILAT regs here
	  //       which it did nothing with. Why?

	  // Report to gdb the target has been stopped.
	  addr = thread->readPc () - SHORT_INSTRLEN;
	  thread->writePc (addr);
	  rspReportException (currentCTid, TARGET_SIGNAL_TRAP);
	  return;
	}
      else
	{
	  cerr << "ERROR: IDLE instruction at step, with no interrupt." << endl;
	  rspReportException (currentCTid, TARGET_SIGNAL_NONE);
	  return;
	}
    }
  else if (TRAP_INSTR == opcode)
    {
      if (si->debugStopResumeDetail ())
	cerr << "DebugStopResumeDetail: TRAP found at 0x"
	     << Utils::intStr (addr) << "." << endl;

      // TRAP instruction triggers I/O
      fIsTargetRunning = false;
      haltAllThreads ();
      redirectStdioOnTrap (thread, getTrap (instr16));
      thread->writePc (addr + SHORT_INSTRLEN);
      return;
    }

  // Ordinary instructions to be stepped.
  uint16_t instrExt = thread->readMem16 (addr + 2);
  uint32_t instr32 = (((uint32_t) instrExt) << 16) | (uint32_t) instr16;

  if (si->debugStopResumeDetail ())
    cerr << "DebugStopResumeDetail: instr16: 0x"
	 << Utils::intStr (instr16, 16, 4) << ", instr32: 0x"
	 << Utils::intStr (instr32, 16, 8) << "." << endl;

  // put sequential breakpoint
  uint32_t bkptAddr = is32BitsInstr (instr16) ? addr + 4 : addr + 2;
  uint16_t bkptVal = thread->readMem16 (bkptAddr);
  thread->insertBkptInstr (bkptAddr);

  if (si->debugStopResumeDetail ())
    cerr << "DebugStopResumeDetail: Step (sequential) bkpt at " << bkptAddr
	 << ", existing value " << Utils::intStr (bkptVal, 16, 4) << "."
	 << endl;


  uint32_t bkptJumpAddr;
  uint16_t bkptJumpVal;

  if (   getJump (thread, instr16, addr, bkptJumpAddr)
      || getJump (thread, instr32, addr, bkptJumpAddr))
    {
      // Put breakpoint to jump target
      bkptJumpVal = thread->readMem16 (bkptJumpAddr);
      thread->insertBkptInstr (bkptJumpAddr);

      if (si->debugStopResumeDetail ())
	cerr << "DebugStopResumeDetail: Step (branch) bkpt at " << bkptJumpAddr
	     << ", existing value " << Utils::intStr (bkptJumpVal, 16, 4) << "."
	     << endl;
    }
  else
    {
      bkptJumpAddr = bkptAddr;
      bkptJumpVal  = bkptVal;
    }

  // Take care of ISR call. Put a breakpoint in each IVT slot except
  // SYNC (aka RESET), but only if it doesn't overwrite the PC
  thread->saveIVT ();

  if (addr != TargetControl::IVT_SWE)
    thread->insertBkptInstr (TargetControl::IVT_SWE);
  if (addr != TargetControl::IVT_PROT)
    thread->insertBkptInstr (TargetControl::IVT_PROT);
  if (addr != TargetControl::IVT_TIMER0)
    thread->insertBkptInstr (TargetControl::IVT_TIMER0);
  if (addr != TargetControl::IVT_TIMER1)
    thread->insertBkptInstr (TargetControl::IVT_TIMER1);
  if (addr != TargetControl::IVT_MSG)
    thread->insertBkptInstr (TargetControl::IVT_MSG);
  if (addr != TargetControl::IVT_DMA0)
    thread->insertBkptInstr (TargetControl::IVT_DMA0);
  if (addr != TargetControl::IVT_DMA1)
    thread->insertBkptInstr (TargetControl::IVT_DMA1);
  if (addr != TargetControl::IVT_WAND)
    thread->insertBkptInstr (TargetControl::IVT_WAND);
  if (addr != TargetControl::IVT_USER)
    thread->insertBkptInstr (TargetControl::IVT_USER);

  // Resume until halt
  thread->resume ();

  while (!thread->isHalted ())
    ;

  addr = thread->readPc ();		// PC where we stopped

  if (si->debugStopResumeDetail ())
    cerr << "DebugStopResumeDetail: Step halted at " << addr << endl;

  thread->restoreIVT ();

  // If it's a breakpoint, then we need to back up one instruction, so
  // on restart we execute the actual instruction.
  addr -= SHORT_INSTRLEN;
  thread->writePc (addr);

  if ((addr != bkptAddr) && (addr != bkptJumpAddr))
    cerr << "Warning: Step stopped at " << addr << ", expected " << bkptAddr
	 << " or " << bkptJumpAddr << "." << endl;

  // Remove temporary breakpoint(s)
  thread->writeMem16 (bkptAddr, bkptVal);
  if (bkptAddr != bkptJumpAddr)
    thread->writeMem16 (bkptJumpAddr, bkptJumpVal);

  // report to GDB the target has been stopped
  rspReportException (currentCTid, TARGET_SIGNAL_TRAP);

}	// rspStep()


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
  ProcessInfo *process = getProcess (currentPid);

  if (process->hasThread (tid))
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
      // Support this for multi-thraeding
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
      return;
    }
  else if (0 == strncmp ("vCont", pkt->data, strlen ("vCont")))
    {
      rspVCont ();
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
      rspUnknownPacket ();
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
  ProcessInfo *process = getProcess (currentPid);

  stringstream    ss (pkt->data);
  vector <string> elements;		// To break out the packet elements
  string          item;

  // Break out the action/thread pairs
  while (getline (ss, item, ';'))
    elements.push_back (item);

  if (1 == elements.size ())
    {
      cerr << "Warning: No actions specified for vCont. Defaulting to stop."
	   << endl;
    }

  // Sort out the detail of each action
  map <int, char>  threadActions;
  char defaultAction = 'n';		// Custom "do nothing"
  int  numDefaultActions = 0;

  // In all-stop mode we should only (I think) be asked to step one
  // thread. Let's monitor that.
  int  numSteps = 0;
  int  stepTid;

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
	  if (0 == numDefaultActions)
	    {
	      defaultAction = extractVContAction (tokens[0]);
	      numDefaultActions++;
	    }
	  else
	    cerr << "Warning: Duplicate default action for vCont: Ignored."
		 << endl;
	}
      else if (2 == tokens.size ())
	{
	  int tid = strtol (tokens[1].c_str (), NULL, 16);
	  if (process->hasThread (tid))
	    {
	      if (threadActions.find (tid) == threadActions.end ())
		{
		  char action = extractVContAction (tokens[0].c_str ());
		  threadActions.insert (pair <int, char> (tid, action));

		  // Note if we are a step thread. There should only be one of
		  // these in all-stop mode.
		  if (action == 's')
		    {
		      numSteps++;

		      if ((ALL_STOP == mDebugMode) && (numSteps > 1))
			cerr << "INFO: Multiple vCont steps in all-stop mode: "
			     << "ignored." << endl;
		      else
			stepTid = tid;
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

  // We have a map of threads to actions and a default action for any thread
  // that is not specified. We may also have pending stops for previous
  // actions. So what we need to do is:
  // 1. Continue any thread that was not already marked as stopped.
  // 2. Deal with a step action if we have any.
  // 3. Otherwise report the first stopped thread.

  // Continue any thread without a pendingStop to deal with.
  for (set <int>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    {
      int tid = *it;
      map <int, char>::iterator ait = threadActions.find (tid);
      char action = (threadActions.end () == ait) ? defaultAction : ait->second;

      if (('c' == action) && !pendingStop (tid))
	{
	  continueThread (tid);
	}
    }

  // If we were single stepping, just focus on that and return
  if (numSteps > 0)
    {
      doStep (stepTid);
      markPendingStops (process, stepTid);
      return;
    }

  // Deal with any pending stops before looking for new ones.
  for (set <int>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    {
      int tid = *it;
      map <int, char>::iterator ait = threadActions.find (tid);
      char action = (threadActions.end () == ait) ? defaultAction : ait->second;

      if (('c' == action) && pendingStop (tid))
	{
	  doContinue (tid);
	  markPendingStops (process, tid);
	}
    }

  // We must wait until a thread halts.
  while (true)
    {
      for (set <int>::iterator it = process->threadBegin ();
	   it != process->threadEnd ();
	   it++)
	{
	  int tid = *it;
	  Thread *thread = getThread (tid);
	  map <int, char>::iterator ait = threadActions.find (tid);
	  char action =
	    (threadActions.end () == ait) ? defaultAction : ait->second;

	  if (('c' == action) && thread->isHalted ())
	    {
	      doContinue (tid);
	      markPendingStops (process, tid);
	      return;
	    }
	}

      // Check for Ctrl-C
      if (si->debugCtrlCWait())
	cerr << "DebugCtrlCWait: Check for Ctrl-C" << endl;

      if (rsp->getBreakCommand ())
	{
	  cerr << "INFO: Cntrl-C request from GDB client." << endl;
	  rspSuspend ();
	  return;
	}

      if (si->debugCtrlCWait())
	cerr << "DebugCtrlCWait: check for CTLR-C done" << endl;

      Utils::microSleep (100000);	// Every 100ms
    }
}	// rspVCont ()


//-----------------------------------------------------------------------------
//! Extract a vCont action

//! We don't support 'C' or 'S' for now, so if we find one of these, we print
//! a rude message and return 'c' or 's' respectively.  'r' is implemented as
//! 's' (degenerate implementation), so we return 's'.  't' is not supported in
//! all-stop mode, so we return 'n' to mean "no action'.  And finally we print
//! a rude message an return 'n' for any other action.

//! @param[in] action  The string with the action
//! @return the single character which is the action.
//-----------------------------------------------------------------------------
char
GdbServer::extractVContAction (string action)
{
  char  a = action.c_str () [0];

  switch (a)
    {
    case 'C':
      cerr << "Warning: 'C' action not supported for vCont: treated as 'c'."
	   << endl;
      a = 'c';
      break;

    case 'S':
      cerr << "Warning: 'S' action not supported for vCont: treated as 's'."
	   << endl;
      a = 's';
      break;

    case 'c':
    case 's':
      // All good
      break;

    case 'r':
      // All good and treated as 's'
      a = 's';
      break;

    case 't':
      if (ALL_STOP == mDebugMode)
	{
	  cerr << "Warning: 't' vCont action not permitted in all-stop mode: "
	       << "ignored." << endl;
	  a = 'n';
	  break;
	}

    default:
      cerr << "Warning: Unrecognized vCont action '" << a
	   << "': ignored." << endl;
      a = 'n';				// Custom for us
      break;
    }

  return  a;

}	// extractVContAction ()


//-----------------------------------------------------------------------------
//! Does a thread have a pending stop?

//! This means that it stopped during a previous vCont but was not yet
//! reported.

//! @param[in] tid  Thread ID to consider
//! @return  TRUE if the thread had a pending stop
//-----------------------------------------------------------------------------
bool
GdbServer::pendingStop (int  tid)
{
  return mPendingStops.find (tid) != mPendingStops.end ();

}	// pendingStop ()


//-----------------------------------------------------------------------------
//! Mark any pending stops

//! At this point all threads should have been halted anyway. So we are
//! looking to see if a thread has halted at a breakpoint. We are called from
//! a thread which has dealt with a pending stop, so we should remote that
//! thread ID from the list.

//! @todo  This assumes that in all-stop mode, all threads are restarted on
//!        vCont. Will need refinement for non-stop mode.

//! @param[in] process  The current process info structure
//! @param[in] tid      The thread ID xto clear pending stop for.
//-----------------------------------------------------------------------------
void
GdbServer::markPendingStops (ProcessInfo* process,
			     int          tid)
{
  for (set <int>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    if ((*it != tid) && (BKPT_INSTR == getStopInstr (getThread (*it))))
      mPendingStops.insert (*it);

  removePendingStop (tid);

}	// markPendingStops ()


//-----------------------------------------------------------------------------
//! Clear a pending stop

//! Remove the specified threads from the set of pending stops

//! @param[in] tid      The thread to remove
//-----------------------------------------------------------------------------
void
GdbServer::removePendingStop (int  tid)
{
  mPendingStops.erase (tid);

}	// removePendingStop ()


//-----------------------------------------------------------------------------
//! Step a thread.

//! We only concern ourselves the first half of this - setting the thread
//! going. A separate function concerns itself with the thread stopping.

//! @todo We ignore the signal for now.

//! @param[in] tid  The thread ID to be stepped. Must be > 0.
//! @param[in] sig  The signal to use for stepping, default to
//!                 TARGET_SIGNAL_NONE (currently ignored).
//! @return  Any signal encountered.
//-----------------------------------------------------------------------------
void
GdbServer::doStep (int          tid,
		   TargetSignal sig)
{
  Thread* thread = getThread (tid);
  assert (thread->isHalted ());

  if (si->debugStopResumeDetail ())
    cerr << dec << "DebugStopResumeDetail: stepThread (" << tid << ", " << sig
	 << ")" << endl;

  // @todo The old code did nothing here, which seems wrong.
  TargetSignal except = (TargetSignal) thread->getException ();
  if (except != TARGET_SIGNAL_NONE)
    {
      rspReportException (tid, except);
      return;
    }

  // We shouldn't have a pending stop. Sanity check
  if (pendingStop (tid))
    {
      cerr << "Warning: Unexpected pending stop in doStep: ignored" << endl;
      removePendingStop (tid);
    }

  uint32_t pc = thread->readPc ();
  uint16_t instr16 = thread->readMem16 (pc);
  uint16_t opcode = getOpcode10 (instr16);

  // IDLE and TRAP need special treatment
  if (IDLE_INSTR == opcode)
    {
      if (si->debugStopResumeDetail ())
	cerr << dec << "DebugStopResumeDetail: IDLE found at " << pc << "."
	     << endl;

      //check if global ISR enable state
      uint32_t coreStatus = thread->readStatus ();

      uint32_t imaskReg = thread->readReg (IMASK_REGNUM);
      uint32_t ilatReg = thread->readReg (ILAT_REGNUM);

      //next cycle should be jump to IVT
      if (((coreStatus & TargetControl::STATUS_GID_MASK)
	   == TargetControl::STATUS_GID_ENABLED)
	  && (((~imaskReg) & ilatReg) != 0))
	{
	  // Interrupts globally enabled, and at least one individual
	  // interrupt is active and enabled.

	  // Next cycle should be jump to IVT. Take care of ISR call. Put a
	  // breakpoint in each IVT slot except SYNC (aka RESET)
	  thread->saveIVT ();

	  thread->insertBkptInstr (TargetControl::IVT_SWE);
	  thread->insertBkptInstr (TargetControl::IVT_PROT);
	  thread->insertBkptInstr (TargetControl::IVT_TIMER0);
	  thread->insertBkptInstr (TargetControl::IVT_TIMER1);
	  thread->insertBkptInstr (TargetControl::IVT_MSG);
	  thread->insertBkptInstr (TargetControl::IVT_DMA0);
	  thread->insertBkptInstr (TargetControl::IVT_DMA1);
	  thread->insertBkptInstr (TargetControl::IVT_WAND);
	  thread->insertBkptInstr (TargetControl::IVT_USER);

	  // Resume which should hit the breakpoint in the IVT.
	  thread->resume ();

	  while (! thread->isHalted ())
	    ;

	  //restore IVT
	  thread->restoreIVT ();

	  // @todo The old code had reads of STATUS, IMASK and ILAT regs here
	  //       which it did nothing with. Why?

	  // Report to gdb the target has been stopped, stopping all other
	  // threads since we are in all-stop mode.
	  pc = thread->readPc () - SHORT_INSTRLEN;
	  thread->writePc (pc);
	  rspReportException (tid, TARGET_SIGNAL_TRAP);
	  return;
	}
      else
	{
	  cerr << "ERROR: IDLE instruction at step, with no interrupt." << endl;
	  rspReportException (tid, TARGET_SIGNAL_NONE);
	  return;
	}
    }
  else if (TRAP_INSTR == opcode)
    {
      if (si->debugStopResumeDetail ())
	cerr << dec << "DebugStopResumeDetail: TRAP found at " << pc << "."
	     << endl;

      // TRAP instruction triggers I/O
      fIsTargetRunning = false;
      haltAllThreads ();
      redirectStdioOnTrap (thread, getTrap (instr16));
      thread->writePc (pc + SHORT_INSTRLEN);
      return;
    }

  // Ordinary instructions to be stepped.
  uint16_t instrExt = thread->readMem16 (pc + 2);
  uint32_t instr32 = (((uint32_t) instrExt) << 16) | (uint32_t) instr16;

  if (si->debugStopResumeDetail ())
    cerr << "DebugStopResumeDetail: instr16: 0x"
	 << Utils::intStr (instr16, 16, 4) << ", instr32: 0x"
	 << Utils::intStr (instr32, 16, 8) << "." << endl;

  // put sequential breakpoint
  uint32_t bkptAddr = is32BitsInstr (instr16) ? pc + 4 : pc + 2;
  uint16_t bkptVal = thread->readMem16 (bkptAddr);
  thread->insertBkptInstr (bkptAddr);

  if (si->debugStopResumeDetail ())
    cerr << "DebugStopResumeDetail: Step (sequential) bkpt at " << bkptAddr
	 << ", existing value " << Utils::intStr (bkptVal, 16, 4) << "."
	 << endl;


  uint32_t bkptJumpAddr;
  uint16_t bkptJumpVal;

  if (   getJump (thread, instr16, pc, bkptJumpAddr)
      || getJump (thread, instr32, pc, bkptJumpAddr))
    {
      // Put breakpoint to jump target
      bkptJumpVal = thread->readMem16 (bkptJumpAddr);
      thread->insertBkptInstr (bkptJumpAddr);

      if (si->debugStopResumeDetail ())
	cerr << "DebugStopResumeDetail: Step (branch) bkpt at " << bkptJumpAddr
	     << ", existing value " << Utils::intStr (bkptJumpVal, 16, 4) << "."
	     << endl;
    }
  else
    {
      bkptJumpAddr = bkptAddr;
      bkptJumpVal  = bkptVal;
    }

  // Take care of ISR call. Put a breakpoint in each IVT slot except
  // SYNC (aka RESET), but only if it doesn't overwrite the PC
  thread->saveIVT ();

  if (pc != TargetControl::IVT_SWE)
    thread->insertBkptInstr (TargetControl::IVT_SWE);
  if (pc != TargetControl::IVT_PROT)
    thread->insertBkptInstr (TargetControl::IVT_PROT);
  if (pc != TargetControl::IVT_TIMER0)
    thread->insertBkptInstr (TargetControl::IVT_TIMER0);
  if (pc != TargetControl::IVT_TIMER1)
    thread->insertBkptInstr (TargetControl::IVT_TIMER1);
  if (pc != TargetControl::IVT_MSG)
    thread->insertBkptInstr (TargetControl::IVT_MSG);
  if (pc != TargetControl::IVT_DMA0)
    thread->insertBkptInstr (TargetControl::IVT_DMA0);
  if (pc != TargetControl::IVT_DMA1)
    thread->insertBkptInstr (TargetControl::IVT_DMA1);
  if (pc != TargetControl::IVT_WAND)
    thread->insertBkptInstr (TargetControl::IVT_WAND);
  if (pc != TargetControl::IVT_USER)
    thread->insertBkptInstr (TargetControl::IVT_USER);

  // Resume until halt
  thread->resume ();

  while (! thread->isHalted ())
    ;

  pc = thread->readPc ();		// PC where we stopped

  if (si->debugStopResumeDetail ())
    cerr << "DebugStopResumeDetail: Step halted at " << pc << endl;

  thread->restoreIVT ();

  // If it's a breakpoint, then we need to back up one instruction, so
  // on restart we execute the actual instruction.
  pc -= SHORT_INSTRLEN;
  thread->writePc (pc);

  if ((pc != bkptAddr) && (pc != bkptJumpAddr))
    cerr << "Warning: Step stopped at " << pc << ", expected " << bkptAddr
	 << " or " << bkptJumpAddr << "." << endl;

  // Remove temporary breakpoint(s)
  thread->writeMem16 (bkptAddr, bkptVal);
  if (bkptAddr != bkptJumpAddr)
    thread->writeMem16 (bkptJumpAddr, bkptJumpVal);

  // report to GDB the target has been stopped, stopping all other threads
  // since we are in all-stop mode.
  rspReportException (tid, TARGET_SIGNAL_TRAP);

}	// doStep ()


//-----------------------------------------------------------------------------
//! Continue execution of a thread

//! This is only half of continue processing - we'll worry about the thread
//! stopping later.

//! @param[in] tid  The thread ID to continue.
//! @param[in] sig  The exception to use. Defaults to
//!                 TARGET_SIGNAL_NONE.  Currently ignored
//-----------------------------------------------------------------------------
void
GdbServer::continueThread (int       tid,
			   uint32_t  sig)
{
  Thread* thread = getThread (tid);

  if (si->debugStopResume ())
    cerr << "DebugStopResume: continueThread (" << tid << ", " << sig << ")."
	 << endl;

  // Resume the thread, and set fIsTargetRunning true.
  // @todo What does fIsTargetRunning do?
  thread->resume ();
  fIsTargetRunning = true;

}	// continueThread ()


//-----------------------------------------------------------------------------
//! Deal with a stopped thread after continue

//! @param[in] tid  The thread ID which stopped.
//-----------------------------------------------------------------------------
void
GdbServer::doContinue (int  tid)
{
  Thread* thread = getThread (tid);

  if (si->debugStopResume ())
    cerr << "DebugStopResume: doContinue (" << tid << ")." << endl;

  // If it was a trap, then do the relevant F packet return.
  if (doFileIO (thread))
    return;
  else
    {
      TargetSignal sig = (TargetSignal) thread->getException ();
      // If no signal flags are set, this must be a breakpoint.
      if (TARGET_SIGNAL_NONE == sig)
	sig = TARGET_SIGNAL_TRAP;
      rspReportException (tid, sig);
    }
}	// doContinue ()


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
  Thread *thread = getThread (currentGTid);

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

//! GDB believes setting a breakpoint sets it in all threads.  For a
//! conventional thread, the code space is shared, so the breakpoint only
//! needs to be written once. However for Epiphany, any address in the local
//! address space (0 - 2^24-1) is not shared.  So if we are asked to set a
//! breakpoint in this range, we must set it in all threads (i.e. Epiphany
//! cores) in the process (i.e. Epiphany workgroup)

//! @todo  Do we really need per-thread recording of the replaced
//!        instruction. Surely they should be the same.  But if they aren't
//!        what do we do?

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
      // Memory breakpoint - replace the original instruction in all threads.
      if (fTargetControl->isLocalAddr (addr))
	{
	  // Local memory we need to remove in all cores.
	  ProcessInfo *process = getProcess (currentPid);

	  for (set <int>::iterator it = process->threadBegin ();
	       it != process->threadEnd ();
	       it++)
	    {
	      int tid = *it;
	      Thread *thread = getThread (tid);

	      if (mpHash->remove (type, addr, tid, &instr))
		thread->writeMem16 (addr, instr);
	    }
	}
      else
	{
	  // Shared memory we only need to remove once.
	  Thread* thread = getThread (currentCTid);

	  if (mpHash->remove (type, addr, currentCTid, &instr))
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

//! GDB believes setting a breakpoint sets it in all threads, so we must
//! implement this. @see GdbServer::rspRemoveMatchpoint () for more
//! explanation.

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
      // Memory breakpoint - substitute a BKPT instruction in all  threads.
      if (fTargetControl->isLocalAddr (addr))
	{
	  // Local memory we need to insert in all cores.
	  ProcessInfo *process = getProcess (currentPid);

	  for (set <int>::iterator it = process->threadBegin ();
	       it != process->threadEnd ();
	       it++)
	    {
	      int tid = *it;
	      Thread *thread = getThread (tid);

	      thread->readMem16 (addr, bpMemVal);
	      mpHash->add (type, addr, tid, bpMemVal);

	      thread->insertBkptInstr (addr);
	    }
	}
      else
	{
	  // Shared memory we only need to insert once.
	  Thread* thread = getThread (currentCTid);

	  thread->readMem16 (addr, bpMemVal);
	  mpHash->add (type, addr, currentCTid, bpMemVal);

	  thread->insertBkptInstr (addr);
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
  Thread* thread = getThread (currentCTid);

  for (unsigned ncyclesReset = 0; ncyclesReset < 12; ncyclesReset++)
    (void) thread->writeReg (RESETCORE_REGNUM, 1);

  (void) thread->writeReg (RESETCORE_REGNUM, 0);

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
//! Map from a process ID to a process structure with sanity checking

//! Barf if this is not a good pid

//! @param[in] pid  Process ID to map
//! @return  Pointer to the process info.
//-----------------------------------------------------------------------------
ProcessInfo *
GdbServer::getProcess (int  pid)
{
  map <int, ProcessInfo *>::iterator pit = mProcesses.find (pid);
  assert (pit != mProcesses.end ());
  return  pit->second;

}	// getProcess ()


//-----------------------------------------------------------------------------
//! Map a tid to a thread with some sanity checking.

//! This is for threads > 0 (i.e. not -1 meaning all threads or 0 meaning any
//! thread). In those circumstances we do something sensible.

//! @param[in] tid   The thraed ID to translate
//! @param[in] mess  Optional supplementary message in the event of
//!                  problems. Defaults to NULL.
//! @return  A coreID
//-----------------------------------------------------------------------------
Thread*
GdbServer::getThread (int         tid,
		      const char* mess)
{
  ProcessInfo *process = getProcess (currentPid);
  ostringstream oss;

  if (NULL == mess)
    oss << "";
  else
    oss << "for " << mess;

  if (tid < 1)
    {
      int newTid = *(process->threadBegin ());

      cerr << "Warning: Cannot use thread ID " << tid << oss.str ()
	   << ": using " << newTid << " from current process ID " << currentPid
	   << " instead." << endl;
      tid = newTid;
      doBacktrace ();
    }

  if (!process->hasThread (tid))
    {
      int newTid = *(process->threadBegin ());

      cerr << "Thread ID " << tid << " is not in process " << currentPid
	   << ": using " << newTid << " instead." << endl;
      tid = newTid;
    }

  map <int, Thread*>::iterator it = mThreads.find (tid);
  return it->second;

}	// getThread ()


//-----------------------------------------------------------------------------
//! Halt all threads in the current process.

//! @return  TRUE if all threads halt, FALSE otherwise
//-----------------------------------------------------------------------------
bool
GdbServer::haltAllThreads ()
{
  ProcessInfo *process = getProcess (currentPid);
  bool allHalted = true;

  for (set <int>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    {
      Thread* thread = getThread (*it);
      allHalted &= thread->halt ();
    }

  return allHalted;

}	// haltAllThreads ()


//-----------------------------------------------------------------------------
//! Resume all threads in the current process.

//! @return  TRUE if all threads resume, FALSE otherwise
//-----------------------------------------------------------------------------
bool
GdbServer::resumeAllThreads ()
{
  ProcessInfo *process = getProcess (currentPid);
  bool allResumed = true;

  for (set <int>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    {
      Thread* thread = getThread (*it);
      allResumed &= thread->resume ();
    }

  return allResumed;

}	// resumeAllThreads ()


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
