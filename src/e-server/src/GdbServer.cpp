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

#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>

#include <fcntl.h>
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>
#include <unistd.h>

#include "GdbServer.h"
#include "Utils.h"

#include "libgloss_syscall.h"

using std::cerr;
using std::cout;
using std::dec;
using std::endl;
using std::hex;
using std::ostringstream;
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
  currentCThread (0),
  currentGThread (0),
  si (_si),
  fTargetControl (NULL),
  fIsTargetRunning (false)
{
  // Some sanity checking that numbering has not got misaligned! This is a
  // consequence of our desire to have properly typed constants.
  assert (regAddr (R0_REGNUM)      == TargetControl::R0);
  assert (regAddr (R0_REGNUM + 63) == TargetControl::R63);

  assert (regAddr (CONFIG_REGNUM)      == TargetControl::CONFIG);
  assert (regAddr (STATUS_REGNUM)      == TargetControl::STATUS);
  assert (regAddr (PC_REGNUM)          == TargetControl::PC);
  assert (regAddr (DEBUGSTATUS_REGNUM) == TargetControl::DEBUGSTATUS);
  assert (regAddr (IRET_REGNUM)        == TargetControl::IRET);
  assert (regAddr (IMASK_REGNUM)       == TargetControl::IMASK);
  assert (regAddr (ILAT_REGNUM)        == TargetControl::ILAT);
  assert (regAddr (FSTATUS_REGNUM)      == TargetControl::FSTATUS);
  assert (regAddr (DEBUGCMD_REGNUM)    == TargetControl::DEBUGCMD);
  assert (regAddr (RESETCORE_REGNUM)   == TargetControl::RESETCORE);
  assert (regAddr (COREID_REGNUM)      == TargetControl::COREID);

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

	      if (si->haltOnAttach ())
		  rspAttach ();
	    }
	}


      // Get a RSP client request
      if (si->debugTranDetail ())
	cerr << "DebugTranDetail: Getting RSP client request." << endl;

      rspClientRequest ();

      //check if the target is stopped and not hit by BP in continue command
      //and check gdb CTRL-C and continue again
      while (fIsTargetRunning)
	{
	  if (si->debugCtrlCWait())
	    cerr << "DebugCtrlCWait: Check for Ctrl-C" << endl;
	  bool isGotBreakCommand = rsp->getBreakCommand ();
	  if (isGotBreakCommand)
	    {
	      cerr << "CTLR-C request from gdb server." << endl;

	      rspSuspend ();
	      //get CTRl-C from gdb, the user should continue the target
	    }
	  else
	    {
	      //continue
	      rspContinue (0, 0);	//the args are ignored by continue command in this mode
	    }
	  if (si->debugCtrlCWait())
	    cerr << dec <<
	      "check for CTLR-C done" << endl;
	}

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
  mIdleProcess = new ProcessInfo (IDLE_PID);
  mProcesses.insert (mIdleProcess);
  mNextPid = IDLE_PID + 1;

  // Initialize info from the target
  vector <CoreId> coreIds = fTargetControl->listCoreIds ();

  // Initialize a bi-directional mapping
  vector <CoreId>::iterator  it;
  for (it = coreIds.begin (); it!= coreIds.end (); it++)
    {
      CoreId coreId = *it;
      int threadId = (coreId.row () + 1) * 100 + coreId.col () + 1;

      // Oh for bi-directional maps
      core2thread [coreId] = threadId;
      thread2core [threadId] = coreId;

      // Add to idle process
      mIdleProcess->addThread (threadId);
    }
}	// initProcesses ()

  
//-----------------------------------------------------------------------------
//! Attach to the target

//! If not already halted, the target will be halted.

//! @todo What should we really do if the target fails to halt?

//! @note  The target should *not* be reset when attaching.
//-----------------------------------------------------------------------------
void
GdbServer::rspAttach ()
{
  bool isHalted = targetHalt ();
  CoreId coreId = cCore ();

  if (isCoreIdle (coreId))
    {
      cerr << "Warning: Core " << coreId << " idle on attach: forcing active."
	   << endl;
      uint32_t status = readReg (coreId, STATUS_REGNUM);
      status &= ~TargetControl::STATUS_ACTIVE_MASK;
      status |= TargetControl::STATUS_ACTIVE_ACTIVE;
      writeReg (coreId, FSTATUS_REGNUM, status);
    }
	
  if (!isHalted)
      rspReportException (0, -1 /*all threads */ , TARGET_SIGNAL_HUP);

}	// rspAttach ()


//-----------------------------------------------------------------------------
//! Detach from hardware.

//! For now a null function.

//! @todo Leave emulation mode?
//-----------------------------------------------------------------------------
void
GdbServer::rspDetach ()
{
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
  if (!rsp->getPkt (pkt))
    {
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
      rspReportException (0 /*PC ??? */ , 0 /*all threads */ ,
			  TARGET_SIGNAL_TRAP);
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
      rspContinue (TARGET_SIGNAL_NONE);
      break;

    case 'C':
      // Continue with signal (in the packet)
      rspContinue ();
      break;

    case 'd':
      // Disable debug using a general query
      cerr << "Warning: RSP 'd' packet is deprecated (define a 'Q' "
	<< "packet instead: ignored" << endl;
      break;

    case 'D':
      // Detach GDB. Do this by closing the client. The rules say that
      // execution should continue, so unstall the processor.
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
      rsp->rspClose ();

      break;

    case 'F':

      //parse the F reply packet
      rspFileIOreply ();

      //always resume -- (continue c or s command)
      targetResume (cCore ());

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
      // Single cycle step not currently supported. Mark the target as
      // running, so that next time it will be detected as stopped (it is
      // still stalled in reality) and an ack sent back to the client.
      cerr << "Warning: RSP cycle stepping not supported: target "
	<< "stopped immediately" << endl;
      break;

    case 'k':

      cerr << hex << "GDB client kill request. The multicore server will be detached from the"
	   << endl
	   << "specific gdb client. Use target remote :<port> to connect again"
	   << endl;	//Stop target id supported
      //pkt->packStr("OK");
      //rsp->putPkt(pkt);
      //exit(23);
      rspDetach ();
      //reset to the initial state to prevent reporting to the disconnected client
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
      // Single step one machine instruction.
      rspStep ();
      break;

    case 'S':
      // Single step one machine instruction with signal
      rspStep ();
      break;

    case 't':
      // Search. This is not well defined in the manual and for now we don't
      // support it. No response is defined.
      cerr << "Warning: RSP 't' packet not supported: ignored"
	<< endl;
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
//! Send a packet acknowledging an exception has occurred

//! The only signal we ever see in this implementation is TRAP/ABORT.
//! TODO no thread support -- always report as S packet
//-----------------------------------------------------------------------------
void
GdbServer::rspReportException (uint32_t stoppedPC, CoreId coreId,
			       TargetSignal exCause)
{
  int threadId = core2thread [coreId];

  if (si->debugStopResume ())
    cerr << "DebugStopResume: Report exception at PC " << stoppedPC
	 << " for thread " << threadId << " with GDB signal " << exCause
	 << endl;

  // Construct a signal received packet
  if (threadId == 0)
    {
      pkt->data[0] = 'S';
    }
  else
    {
      pkt->data[0] = 'T';
    }

  pkt->data[1] = Utils::hex2Char (exCause >> 4);
  pkt->data[2] = Utils::hex2Char (exCause % 16);

  if (threadId != 0)
    {
      sprintf ((pkt->data), "T05thread:%d;", threadId);

    }
  else
    {

      pkt->data[3] = '\0';
    }
  pkt->setLen (strlen (pkt->data));

  rsp->putPkt (pkt);

  //core in Debug state (bkpt) .. report to gdb
  fIsTargetRunning = false;

}				// rspReportException()


//-----------------------------------------------------------------------------
//! Handle a RSP continue request

//! This version is typically used for the 'c' packet, to continue without
//! signal, in which case TARGET_SIGNAL_NONE is passed in as the exception to
//! use.

//! At present other exceptions are not supported

//! @param[in] except  The GDB signal to use
//-----------------------------------------------------------------------------
void
GdbServer::rspContinue (uint32_t except)
{
  uint32_t     addr;		// Address to continue from, if any
  uint32_t  hexAddr;		// Address supplied in packet
  // Reject all except 'c' packets
  if ('c' != pkt->data[0])
    {
      cerr << "Warning: Continue with signal not currently supported: "
	<< "ignored" << endl;
      return;
    }

  // Get an address if we have one
  if (0 == strcmp ("c", pkt->data))
    addr = readPc (cCore ());		// Default uses current PC
  else if (1 == sscanf (pkt->data, "c%" SCNx32, &hexAddr))
    addr = hexAddr;
  else
    {
      cerr << "Warning: RSP continue address " << pkt->data
	<< " not recognized: ignored" << endl;
      addr = readPc (cCore ());		// Default uses current NPC
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
  if (si->debugTrapAndRspCon ())
    cerr << "RSP continue with signal '" << pkt->
      data << "' received" << endl;

  //return the same exception
  TargetSignal exCause = TARGET_SIGNAL_TRAP;

  if ((0 == strcmp ("C03", pkt->data)))
    {				//get continue with signal after reporting QUIT/exit, silently ignore

      exCause = TARGET_SIGNAL_QUIT;

    }
  else
    {
      cerr << "WARNING: RSP continue with signal '" << pkt->
	data << "' received, the server will ignore the continue" << endl;

      //check the exception state
      exCause = getException (cCore ());
      //bool isExState= isTargetExceptionState(exCause);
    }

  //get PC
  uint32_t reportedPc = readPc (cCore ());

  // report to gdb the target has been stopped
  rspReportException (reportedPc, 0 /* all threads */ , exCause);

}				// rspContinue()


//----------------------------------------------------------
//! have sleep con for request .. get other thread to communicate with target
//
//-----------------------------------------------------------------------------
void
GdbServer::NanoSleepThread (unsigned long timeout)
{
  struct timespec sleepTime;
  struct timespec remainingSleepTime;

  sleepTime.tv_sec = 0;
  sleepTime.tv_nsec = timeout;
  nanosleep (&sleepTime, &remainingSleepTime);

}


//-----------------------------------------------------------------------------
//! Resume target execution.

//! @param[in] coreId  The core to resume.
//-----------------------------------------------------------------------------
void
GdbServer::targetResume (CoreId coreId)
{
  if (!writeReg (coreId, DEBUGCMD_REGNUM, TargetControl::DEBUGCMD_COMMAND_RUN))
    cerr << "Warning: Failed to resume target." << endl;

  fIsTargetRunning = true;

  if (si->debugTrapAndRspCon () || si->debugStopResume ())
    cerr << "Resuming target." << endl;

}	// targetResume ()


//-----------------------------------------------------------------------------
//! Generic processing of a continue request

//! The signal may be TARGET_SIGNAL_NONE if there is no exception to be
//! handled. Currently the exception is ignored.

//! @param[in] addr    Address from which to step
//! @param[in] except  The exception to use (if any). Currently ignored
//-----------------------------------------------------------------------------
void
GdbServer::rspContinue (uint32_t addr, uint32_t except)
{
  if ((!fIsTargetRunning && si->debugStopResume ()) || si->debugTranDetail ())
    {
      cerr << "GdbServer::rspContinue PC 0x" << hex << addr << dec << endl;
    }

  uint32_t prevPc = 0;

  if (!fIsTargetRunning)
    {
      if (!isCoreHalted (cCore ()))
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
	  writePc (cCore (), addr);

	  //resume
	  targetResume (cCore ());
	}
    }

  unsigned long timeout_me = 0;
  unsigned long timeout_limit = 100;

  timeout_limit = 3;

  while (true)
    {
      //cerr << "********* while true **************" << endl;

      NanoSleepThread (300000000);

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

      if (isCoreHalted (cCore ()))
	{
	  //cerr << "********* isTargetInDebugState = true **************" << endl;

	  // If it's a breakpoint, then we need to back up one instruction, so
	  // on restart we execute the actual instruction.
	  uint32_t c_pc = readPc (cCore ());
	  //cout << "stopped at @pc " << hex << c_pc << dec << endl;
	  prevPc = c_pc - BKPT_INSTLEN;

	  //check if it is trap
	  uint16_t val_;
	  readMem16 (gCore (), prevPc, val_);
	  uint16_t valueOfStoppedInstr = val_;

	  if (valueOfStoppedInstr == BKPT_INSTR)
	    {
	      //cerr << "********* valueOfStoppedInstr = BKPT_INSTR **************" << endl;

	      if (NULL != mpHash->lookup (BP_MEMORY, prevPc))
		{
		  writePc (cCore (), prevPc);
		  if (si->debugTrapAndRspCon ())
		    cerr << dec << "set pc back " << hex << prevPc << dec << endl
		     ;
		}

	      if (si->debugTrapAndRspCon ())
		cerr <<
		  dec << "After wait CONT GdbServer::rspContinue PC 0x" <<
		  hex << prevPc << dec << endl;

	      // report to gdb the target has been stopped

	      rspReportException (prevPc, 0 /*all threads */ ,
				  TARGET_SIGNAL_TRAP);



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

			  readMem16 (cCore (), j, val_);
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

		  fIsTargetRunning = false;

		  uint8_t trapNumber =
		    getfield (valueOfStoppedInstr, 15, 10);
		  redirectSdioOnTrap (trapNumber);
		}
	      else
		{
		  //cerr << "********* stoppedAtTrap = false **************" << endl;
		  if (si->debugStopResumeDetail ())
		    cerr << dec << " no trap found, return control to gdb" <<
		      endl;
		  // report to gdb the target has been stopped
		  rspReportException (readPc (cCore ()) /* PC no trap found */ ,
				      0 /* all threads */ ,
				      TARGET_SIGNAL_TRAP);
		}
	    }

	  break;
	}			// if (isCoreInDebugState())
    }				// while (true)
}				// rspContinue()


//-----------------------------------------------------------------------------
//! Generic processing of a suspend request .. CTRL-C command in gdb
//! Stop target
//! wait for confirmation on DEBUG state
//! report to GDB by TRAP
//! switch to not running stage, same as c or s command
//-----------------------------------------------------------------------------
void
GdbServer::rspSuspend ()
{
  TargetSignal exCause = TARGET_SIGNAL_TRAP;
  uint32_t reportedPc;

  bool isHalted;

  if (si->debugTrapAndRspCon ())
    cerr << dec <<
      "force debug mode" << endl;

  //probably target suspended
  if (!isCoreHalted (cCore ()))
    {

      isHalted = targetHalt ();
    }
  else
    {
      isHalted = true;
    }

  if (!isHalted)
    {

      exCause = TARGET_SIGNAL_HUP;


    }
  else
    {

      //get PC
      reportedPc = readPc (cCore ());

      //check the exception state
      exCause = getException (cCore ());

      if (exCause != TARGET_SIGNAL_NONE)
	{
	  //stopped due to some exception -- just report to gdb

	}
      else
	{

	  if (isCoreIdle (cCore ()))
	    {

	      //fetch instruction opcode on PC
	      uint16_t val16;
	      readMem16 (cCore (), reportedPc, val16);
	      uint16_t instrOpcode = val16;

	      //idle
	      if (getfield (instrOpcode, 8, 0) == IDLE_INSTR)
		{
		  //cerr << "POINT on IDLE " << endl;
		}
	      else
		{

		  reportedPc = reportedPc - 2;
		}
	      writePc (cCore (), reportedPc);

	      //cerr << "SUPEND " << hex << reportedPc << endl;
	    }
	}
    }

  // report to gdb the target has been stopped
  rspReportException (reportedPc, 0 /*all threads */ , exCause);
}


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

  long int result_io = -1;
  long int host_respond_error_code;

  if (2 ==
      sscanf (pkt->data, "F%lx,%lx", &result_io, &host_respond_error_code))
    {
      //write to r0
      writeReg (gCore (), R0_REGNUM + 0, result_io);

      //write to r3 error core
      writeReg (gCore (), R0_REGNUM + 3, host_respond_error_code);
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
      writeReg (gCore (), R0_REGNUM + 0, result_io);
    }
  else
    {
      cerr << " remote IO operation fail " << endl;
    }
}

//-----------------------------------------------------------------------------
//! Redirect the SDIO to gdb using F packets open,write,read, close are supported
//
//
//
//-----------------------------------------------------------------------------

/* Enum declaration for trap instruction dispatch code. See sim/epiphany/epiphany-desc.*/
enum TRAP_CODES
{
  TRAP_WRITE,			// 0
  TRAP_READ,			// 1
  TRAP_OPEN,			// 2
  TRAP_EXIT,			// 3
  TRAP_PASS,			// 4
  TRAP_FAIL,			// 5
  TRAP_CLOSE,			// 6
  TRAP_OTHER,			// 7
};


#define MAX_FILE_NAME_LENGTH (256*4)


void
GdbServer::redirectSdioOnTrap (uint8_t trapNumber)
{
  //cout << "---- stop on PC 0x " << hex << prevPc << dec << endl;
  //cout << "---- got trap 0x" << hex << valueOfStoppedInstr << dec << endl;

  uint32_t r0, r1, r2, r3;
  char *buf;
  //int result_io;
  unsigned int k;

  char res_buf[2048];
  char fmt[2048];

  switch (trapNumber)
    {
    case TRAP_WRITE:

      if (si->debugTrapAndRspCon ())
	cerr << dec <<
	  " Trap 0 write " << endl;
      r0 = readReg (gCore (), R0_REGNUM + 0);		//chan
      r1 = readReg (gCore (), R0_REGNUM + 1);		//addr
      r2 = readReg (gCore (), R0_REGNUM + 2);		//length

      if (si->debugTrapAndRspCon ())
	cerr << dec <<
	  " write to chan " << r0 << " bytes " << r2 << endl;

      sprintf ((pkt->data), "Fwrite,%lx,%lx,%lx", (unsigned long) r0,
	       (unsigned long) r1, (unsigned long) r2);
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);

      break;

    case TRAP_READ:
      if (si->debugTrapAndRspCon ())
	cerr << dec << " Trap 1 read " << endl;	/*read(chan, addr, len) */
      r0 = readReg (gCore (), R0_REGNUM + 0);		//chan
      r1 = readReg (gCore (), R0_REGNUM + 1);		//addr
      r2 = readReg (gCore (), R0_REGNUM + 2);		//length

      if (si->debugTrapAndRspCon ())
	cerr << dec <<
	  " read from chan " << r0 << " bytes " << r2 << endl;


      sprintf ((pkt->data), "Fread,%lx,%lx,%lx", (unsigned long) r0,
	       (unsigned long) r1, (unsigned long) r2);
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);

      break;
    case TRAP_OPEN:
      r0 = readReg (gCore (), R0_REGNUM + 0);		//filepath
      r1 = readReg (gCore (), R0_REGNUM + 1);		//flags

      if (si->debugTrapAndRspCon ())
	cerr << dec <<
	  " Trap 2 open, file name located @" << hex << r0 << dec << " (mode)"
	  << r1 << endl;

      for (k = 0; k < MAX_FILE_NAME_LENGTH - 1; k++)
	{
	  uint8_t val_;
	  readMem8 (gCore (), r0 + k, val_);
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
      r0 = readReg (gCore (), R0_REGNUM + 0);		//status
      //cerr << " The remote target got exit() call ... no OS -- ignored" << endl;
      //exit(4);
      rspReportException (readPc (cCore ()), 0 /*all threads */ , TARGET_SIGNAL_QUIT);
      break;
    case TRAP_PASS:
      cerr << " Trap 4 PASS " << endl;
      rspReportException (readPc (cCore ()), 0 /*all threads */ , TARGET_SIGNAL_TRAP);
      break;
    case TRAP_FAIL:
      cerr << " Trap 5 FAIL " << endl;
      rspReportException (readPc (cCore ()), 0 /*all threads */ , TARGET_SIGNAL_QUIT);
      break;
    case TRAP_CLOSE:
      r0 = readReg (gCore (), R0_REGNUM + 0);		//chan
      if (si->debugTrapAndRspCon ())
	cerr << dec <<
	  " Trap 6 close: " << r0 << endl;
      sprintf ((pkt->data), "Fclose,%lx", (unsigned long) r0);
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);
      break;
    case TRAP_OTHER:

      if (NULL != si->ttyOut ())
	{

	  //cerr << " Trap 7 syscall -- ignored" << endl;
	  if (si->debugTrapAndRspCon ())
	    cerr << dec <<
	      " Trap 7 " << endl;
	  r0 = readReg (gCore (), R0_REGNUM + 0);	// buf_addr
	  r1 = readReg (gCore (), R0_REGNUM + 1);	// fmt_len
	  r2 = readReg (gCore (), R0_REGNUM + 2);	// total_len

	  //fprintf(stderr, " TRAP_OTHER %x %x", PARM0,PARM1);

	  //cerr << " buf " << hex << r0 << "  " << r1 << "  " << r2 << dec << endl;

	  buf = (char *) malloc (r2);
	  for (unsigned k = 0; k < r2; k++)
	    {
	      uint8_t val_;

	      readMem8 (gCore (), r0 + k, val_);
	      buf[k] = val_;
	    }


	  strncpy (fmt, buf, r1);
	  fmt[r1] = '\0';


	  printfWrapper (res_buf, fmt, buf + r1 + 1);
	  fprintf (si->ttyOut (), "%s", res_buf);

	  targetResume (cCore ());
	}
      else
	{

	  r0 = readReg (gCore (), R0_REGNUM + 0);
	  r1 = readReg (gCore (), R0_REGNUM + 1);
	  r2 = readReg (gCore (), R0_REGNUM + 2);
	  r3 = readReg (gCore (), R0_REGNUM + 3);	//SUBFUN;

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
		  readMem8 (gCore (), r0 + k, val_);
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
		  readMem8 (gCore (), r0 + k, val_);
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
		  readMem8 (gCore (), r0 + k, val_);
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
//! Handle a RSP read all registers request

//! The registers follow the GDB sequence for Epiphany: GPR0 through GPR63,
//! followed by all the Special Control Registers. Each register is returned
//! as a sequence of bytes in target endian order.

//! Each byte is packed as a pair of hex digits.
//-----------------------------------------------------------------------------
void
GdbServer::rspReadAllRegs ()
{
  // Start timing if debugging
  if (si->debugStopResumeDetail ())
    fTargetControl->startOfBaudMeasurement ();

  // Get each reg
  for (unsigned int r = 0; r < NUM_REGS; r++)
    {
      uint32_t val;
      unsigned int pktOffset = r * TargetControl::E_REG_BYTES * 2;

      // Not all registers are necessarily supported.
      if (readReg (gCore (), r, val))
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
  // All registers
  for (unsigned int r = 0; r < NUM_REGS; r++)
      (void) writeReg (gCore (), r, Utils::hex2Reg (&(pkt->data[r * 8])));

  // Acknowledge (always OK for now).
  pkt->packStr ("OK");
  rsp->putPkt (pkt);

}				// rspWriteAllRegs()


//! Set the thread number of subsequent operations.
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
  
  switch (c)
    {
    case 'c': currentCThread = tid; break;
    case 'g': currentGThread = tid; break;

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
      readMemBlock (gCore (), addr, (unsigned char *) buf, len);

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
    if (!writeMemBlock (gCore (), addr, (unsigned char *) symDat, len))
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
  if (!readReg (gCore (), regnum, regval))
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
  if (!writeReg (gCore (), regnum, Utils::hex2Reg (valstr)))
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

      sprintf (pkt->data, "QC%x", currentGThread);
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
      sprintf (pkt->data, "PacketSize=%x;qXfer:osdata:read+",
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

//! Reuse the incoming packet.

//! @todo We assume we can do everything in the first packet.

//! @param[in] isFirst  TRUE if we were a qfThreadInfo packet.
//-----------------------------------------------------------------------------
void
GdbServer::rspQThreadInfo (bool isFirst)
{
  if (isFirst)
    {
      ostringstream  os;
      map <int, CoreId>::iterator  it;

      for (it = thread2core.begin (); it != thread2core.end (); it++)
	{
	  if (it != thread2core.begin ())
	    os << ",";

	  os << hex << it->first;
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

//! Reuse the incoming packet.

//! @todo This should report the Core ID associated with the thread ID. For now
//!       this is just the currentThread - 1, but we need to fix this.
//-----------------------------------------------------------------------------
void
GdbServer::rspQThreadExtraInfo ()
{
  unsigned int tid;

  if (1 != sscanf (pkt->data, "qThreadExtraInfo,%x", &tid))
    {
      cerr << "Warning: Failed to recognize RSP qThreadExtraInfo command : "
	<< pkt->data << endl;
      pkt->packStr ("E01");
      return;
    }

  // Data about thread
  CoreId coreId = thread2core[tid];
  bool isHalted = isCoreHalted (coreId);
  bool isIdle = isCoreIdle (coreId);
  bool isGIntsEnabled = isCoreGIntsEnabled (coreId);

  char* buf = &(pkt->data[0]);
  string res = "Core: ";
  res += coreId;
  if (isIdle)
    res += isHalted ? ": idle, halted" : ": idle";
  else
    res += isHalted ? ": halted" : ": running";

  res += ", gints ";
  res += isGIntsEnabled ? "enabled" : "disabled";

  // Put each char as its ASCII representation
  for (string::iterator it = res.begin (); it != res.end (); it++)
    {
      sprintf (buf, "%02x", *it);
      buf += 2;
    }

  sprintf (buf, "00");
  pkt->setLen (strlen (pkt->data));
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

      cerr << dec <<
	"The debugger sent reset request" << endl;

      //reset
      targetSwReset ();

    }
  else if (strcmp ("hwreset", cmd) == 0)
    {

      char mess[] =
	"The debugger sent HW (platfrom) reset request, please restart other debug clients.\n";

      cerr << dec << mess
	<< endl;

      //HW reset (ESYS_RESET)
      targetHWReset ();

      //FIXME ???
      Utils::ascii2Hex (pkt->data, mess);

    }
  else if (strcmp ("halt", cmd) == 0)
    {

      cerr << dec <<
	"The debugger sent halt request," << endl;

      //target halt
      bool isHalted = targetHalt ();
      if (!isHalted)
	{
	  rspReportException (0, 0 /*all threads */ , TARGET_SIGNAL_HUP);
	}

    }
  else if (strcmp ("run", cmd) == 0)
    {

      cerr 
<< dec <<
	"The debugger sent start request," << endl;

      // target start (ILAT set)
      // ILAT set
      //! @todo Surely we should be doing this write to ILATST? Probably
      //        doesn't matter for reset.
      writeReg (cCore (), ILAT_REGNUM, TargetControl::ILAT_ILAT_SYNC);
    }
  else if (strcmp ("coreid", cmd) == 0)
    {

      uint32_t val = readCoreId (gCore ());

      char buf[256];
      sprintf (buf, "0x%x\n", val);

      Utils::ascii2Hex (pkt->data, buf);

    }
  else if (strcmp ("help", cmd) == 0)
    {

      Utils::ascii2Hex (pkt->data,
			(char *)
			"monitor commands: hwreset, coreid, swreset, halt, run, help\n");

    }
  else if (strcmp ("help-hidden", cmd) == 0)
    {

      Utils::ascii2Hex (pkt->data, (char *) "link,spi\n");

    }
  else
    {
      cerr << "Warning: received remote command " << cmd << ": ignored" <<
	endl;
    }

  pkt->setLen (strlen (pkt->data));
  rsp->putPkt (pkt);

}				// rspCommand()


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

	// Iterate through all processes
	for (set <ProcessInfo *>::iterator pit = mProcesses.begin ();
	     pit != mProcesses.end (); pit++)
	  {
	    ProcessInfo *process = *pit;
	    osProcessReply += "  <item>\n"
	    "    <column name=\"pid\">";
	    osProcessReply += process->pid ();
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

		osProcessReply += thread2core [*tit];
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

      map <CoreId, int>::iterator  it;

      for (it = core2thread.begin (); it != core2thread.end (); it++)
	{
	  osLoadReply +=
	    "  <item>\n"
	    "    <column name=\"coreid\">";
	  osLoadReply += it->first;
	  osLoadReply += "</column>\n";

	  osLoadReply +=
	    "    <column name=\"load\">";
	  osLoadReply += intStr (random () % 100, 10, 2);
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
      map <CoreId, int>::iterator  it;

      for (it = core2thread.begin (); it != core2thread.end (); it++)
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
	  inTraffic = intStr (random () % 100, 10, 2);
	  if (coreId.row () > 0)
	    outTraffic = intStr (random () % 100, 10, 2);
	  else
	    outTraffic = "--";
		  
	  osTrafficReply +=
	    "    <column name=\"North In\">";
	  osTrafficReply += inTraffic;
	  osTrafficReply += "</column>\n"
	    "    <column name=\"North Out\">";
	  osTrafficReply += outTraffic;
	  osTrafficReply += "</column>\n";

	  inTraffic = intStr (random () % 100, 10, 2);
	  if (coreId.row ()< maxRow)
	    outTraffic = intStr (random () % 100, 10, 2);
	  else
	    outTraffic = "--";

	  osTrafficReply +=
	    "    <column name=\"South In\">";
	  osTrafficReply += inTraffic;
	  osTrafficReply += "</column>\n"
	    "    <column name=\"South Out\">";
	  osTrafficReply += outTraffic;
	  osTrafficReply += "</column>\n";

	  inTraffic = intStr (random () % 100, 10, 2);
	  if (coreId.col () < maxCol)
	    outTraffic = intStr (random () % 100, 10, 2);
	  else
	    outTraffic = "--";

	  osTrafficReply +=
	    "    <column name=\"East In\">";
	  osTrafficReply += inTraffic;
	  osTrafficReply += "</column>\n"
	    "    <column name=\"East Out\">";
	  osTrafficReply += outTraffic;
	  osTrafficReply += "</column>\n";

	  inTraffic = intStr (random () % 100, 10, 2);
	  if (coreId.col () > 0)
	    outTraffic = intStr (random () % 100, 10, 2);
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
  writePc (cCore (), 0);

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
  CoreId  coreId = cCore ();		// Rude message if current thread is -1
  assert (isCoreHalted (coreId));

  if (!haveAddrP)
    {
      // @todo This should be done on a per-core basis for thread -1
      addr = readPc (coreId);
    }

  if (si->debugStopResumeDetail ())
    cerr << dec << "DebugStopResumeDetail: rspStep (" << addr << ", " << sig
	 << ")" << endl;

  TargetSignal exSig = getException (coreId);
  if (exSig != TARGET_SIGNAL_NONE)
    {
      // Already stopped due to some exception. Just report to GDB.

      // @todo This is commented as being during to a silicon problem
      rspReportException (addr, core2thread[coreId], exSig);
      return;
    }

  // Set the PC to the given address
  writePc (coreId, addr);
  assert (readPc (coreId) == addr);	// Do we really need this?

  uint16_t instr16 = readMem16 (coreId, addr);
  uint16_t opcode = getOpcode10 (instr16);

  // IDLE and TRAP need special treatment
  if (IDLE_INSTR == opcode)
    {
      if (si->debugStopResumeDetail ())
	cerr << dec << "DebugStopResumeDetail: IDLE found at " << addr << "."
	     << endl;

      //check if global ISR enable state
      uint32_t coreStatus = readStatus (coreId);

      uint32_t imaskReg = readReg (coreId, IMASK_REGNUM);
      uint32_t ilatReg = readReg (coreId, ILAT_REGNUM);

      //next cycle should be jump to IVT
      if (((coreStatus & TargetControl::STATUS_GID_MASK)
	   == TargetControl::STATUS_GID_ENABLED)
	  && (((~imaskReg) & ilatReg) != 0))
	{
	  // Interrupts globally enabled, and at least one individual
	  // interrupt is active and enabled.

	  // Next cycle should be jump to IVT. Take care of ISR call. Put a
	  // breakpoint in each IVT slot except SYNC (aka RESET)
	  saveIVT (coreId);

	  insertBkptInstr (TargetControl::IVT_SWE);
	  insertBkptInstr (TargetControl::IVT_PROT);
	  insertBkptInstr (TargetControl::IVT_TIMER0);
	  insertBkptInstr (TargetControl::IVT_TIMER1);
	  insertBkptInstr (TargetControl::IVT_MSG);
	  insertBkptInstr (TargetControl::IVT_DMA0);
	  insertBkptInstr (TargetControl::IVT_DMA1);
	  insertBkptInstr (TargetControl::IVT_WAND);
	  insertBkptInstr (TargetControl::IVT_USER);

	  // Resume which should hit the breakpoint in the IVT.
	  targetResume (coreId);

	  while (!isCoreHalted (coreId))
	    ;

	  //restore IVT
	  restoreIVT (coreId);

	  // @todo The old code had reads of STATUS, IMASK and ILAT regs here
	  //       which it did nothing with. Why?

	  // Report to gdb the target has been stopped.
	  addr = readPc (coreId) - BKPT_INSTLEN;
	  writePc (coreId, addr);
	  rspReportException (addr, core2thread[coreId], TARGET_SIGNAL_TRAP);
	  return;
	}
      else
	{
	  cerr << "ERROR: IDLE instruction at step, with no interrupt." << endl;
	  rspReportException (addr, core2thread[coreId], TARGET_SIGNAL_NONE);
	  return;
	}
    }
  else if (TRAP_INSTR == opcode)
    {
      if (si->debugStopResumeDetail ())
	cerr << dec << "DebugStopResumeDetail: TRAP found at " << addr << "."
	     << endl;

      // TRAP instruction triggers I/O
      fIsTargetRunning = false;
      redirectSdioOnTrap (getTrap (instr16));
      writePc (coreId, addr + TRAP_INSTLEN);
      return;
    }

  // Ordinary instructions to be stepped.
  uint16_t instrExt = readMem16 (coreId, addr + 2);
  uint32_t instr32 = (((uint32_t) instrExt) << 16) | (uint32_t) instr16;

  if (si->debugStopResumeDetail ())
    cerr << "DebugStopResumeDetail: instr16: 0x" << intStr (instr16, 16, 4)
	 << ", instr32: 0x" << intStr (instr32, 16, 8) << "." << endl;

  // put sequential breakpoint
  uint32_t bkptAddr = is32BitsInstr (instr16) ? addr + 4 : addr + 2;
  uint16_t bkptVal = readMem16 (coreId, bkptAddr);
  insertBkptInstr (bkptAddr);

  if (si->debugStopResumeDetail ())
    cerr << "DebugStopResumeDetail: Step (sequential) bkpt at " << bkptAddr
	 << ", existing value " << intStr (bkptVal, 16, 4) << "." << endl;


  uint32_t bkptJumpAddr;
  uint16_t bkptJumpVal;

  if (   getJump (coreId, instr16, addr, bkptJumpAddr)
      || getJump (coreId, instr32, addr, bkptJumpAddr))
    {
      // Put breakpoint to jump target
      bkptJumpVal = readMem16 (coreId, bkptJumpAddr);
      insertBkptInstr (bkptJumpAddr);

      if (si->debugStopResumeDetail ())
	cerr << "DebugStopResumeDetail: Step (branch) bkpt at " << bkptJumpAddr
	     << ", existing value " << intStr (bkptJumpVal, 16, 4) << "."
	     << endl;
    }
  else
    {
      bkptJumpAddr = bkptAddr;
      bkptJumpVal  = bkptVal;
    }

  // Take care of ISR call. Put a breakpoint in each IVT slot except
  // SYNC (aka RESET), but only if it doesn't overwrite the PC
  saveIVT (coreId);

  if (addr != TargetControl::IVT_SWE)
    insertBkptInstr (TargetControl::IVT_SWE);
  if (addr != TargetControl::IVT_PROT)
    insertBkptInstr (TargetControl::IVT_PROT);
  if (addr != TargetControl::IVT_TIMER0)
    insertBkptInstr (TargetControl::IVT_TIMER0);
  if (addr != TargetControl::IVT_TIMER1)
    insertBkptInstr (TargetControl::IVT_TIMER1);
  if (addr != TargetControl::IVT_MSG)
    insertBkptInstr (TargetControl::IVT_MSG);
  if (addr != TargetControl::IVT_DMA0)
    insertBkptInstr (TargetControl::IVT_DMA0);
  if (addr != TargetControl::IVT_DMA1)
    insertBkptInstr (TargetControl::IVT_DMA1);
  if (addr != TargetControl::IVT_WAND)
    insertBkptInstr (TargetControl::IVT_WAND);
  if (addr != TargetControl::IVT_USER)
    insertBkptInstr (TargetControl::IVT_USER);

  // Resume until halt
  targetResume (coreId);

  while (!isCoreHalted (coreId))
    ;

  addr = readPc (coreId);		// PC where we stopped

  if (si->debugStopResumeDetail ())
    cerr << "DebugStopResumeDetail: Step halted at " << addr << endl;

  restoreIVT (coreId);

  // If it's a breakpoint, then we need to back up one instruction, so
  // on restart we execute the actual instruction.
  addr -= BKPT_INSTLEN;
  writePc (coreId, addr);

  if ((addr != bkptAddr) && (addr != bkptJumpAddr))
    cerr << "Warning: Step stopped at " << addr << ", expected " << bkptAddr
	 << " or " << bkptJumpAddr << "." << endl;

  // Remove temporary breakpoint(s)
  writeMem16 (coreId, bkptAddr, bkptVal);
  if (bkptAddr != bkptJumpAddr)
    writeMem16 (coreId, bkptJumpAddr, bkptJumpVal);

  // report to GDB the target has been stopped
  rspReportException (addr, coreId, TARGET_SIGNAL_TRAP);

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
  unsigned int tid;

  if (1 != sscanf (pkt->data, "T%x", &tid))
    {
      cerr << "Warning: Failed to recognize RSP 'T' command : "
	   << pkt->data << endl;
      pkt->packStr ("E02");
      rsp->putPkt (pkt);
      return;
    }

  if (thread2core.find (tid) == thread2core.end ())
    pkt->packStr ("E01");
  else
    pkt->packStr ("OK");

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


//! Created as a wrapper to overcome external memory problems.

//! The original comment labelled this as NOT IN USE, but it is evidently used
//! in this class.
void
GdbServer::printfWrapper (char *result_str, const char *fmt,
			  const char *args_buf)
{
  char *p = (char *) fmt;
  char *b = (char *) fmt;
  //char *perc;        = (char *) fmt;
  char *p_args_buf = (char *) args_buf;

  unsigned a, a1, a2, a3, a4;

  int found_percent = 0;

  char buf[2048];
  char tmp_str[2048];

  strcpy (result_str, "");
  //sprintf(result_str, "");

  //sprintf(buf, fmt);

  //printf("fmt ----%d ----\n", strlen(fmt));

  //puts(fmt);

  //printf("Parsing\n");

  while (*p)
    {
      if (*p == '%')
	{
	  found_percent = 1;
	  //perc = p;
	}
      else if (*p == 's' && (found_percent == 1))
	{
	  found_percent = 0;

	  strncpy (buf, b, (p - b) + 1);
	  buf[p - b + 1] = '\0';
	  b = p + 1;

	  //puts(buf);

	  //printf("args_buf ----%d ----\n", strlen(p_args_buf));
	  //puts(p_args_buf);

	  sprintf (tmp_str, buf, p_args_buf);
	  sprintf (result_str, "%s%s", result_str, tmp_str);

	  p_args_buf = p_args_buf + strlen (p_args_buf) + 1;

	}
      else
	if ((*p == 'p' || *p == 'X' || *p == 'u' || *p == 'i' || *p == 'd'
	     || *p == 'x' || *p == 'f') && (found_percent == 1))
	{
	  found_percent = 0;

	  strncpy (buf, b, (p - b) + 1);
	  buf[p - b + 1] = '\0';
	  b = p + 1;

	  //print out buf
	  //puts(buf);

	  a1 = (p_args_buf[0]);
	  a1 &= 0xff;
	  a2 = (p_args_buf[1]);
	  a2 &= 0xff;
	  a3 = (p_args_buf[2]);
	  a3 &= 0xff;
	  a4 = (p_args_buf[3]);
	  a4 &= 0xff;

	  //printf("INT <a1> %x <a2> %x <a3> %x <a4> %x\n", a1, a2,a3, a4);
	  a = ((a1 << 24) | (a2 << 16) | (a3 << 8) | a4);

	  if (*p == 'i')
	    {
	      //printf("I %i\n", a);
	    }
	  else if (*p == 'd')
	    {
	      //printf("D %d\n", a);
	    }
	  else if (*p == 'x')
	    {
	      //printf("X %x\n", a);
	    }
	  else if (*p == 'f')
	    {
	      //printf("F %f \n", *((float*)&a));
	    }
	  else if (*p == 'f')
	    {
	      sprintf (tmp_str, buf, *((float *) &a));
	    }
	  else
	    {
	      sprintf (tmp_str, buf, a);
	    }
	  sprintf (result_str, "%s%s", result_str, tmp_str);

	  p_args_buf = p_args_buf + 4;
	}

      p++;
    }

  //tail
  //puts(b);
  sprintf (result_str, "%s%s", result_str, b);

  //printf("------------- %s ------------- %d ", result_str, strlen(result_str));
}	// printf_wrappper ()


//-----------------------------------------------------------------------------
//! Halt the target

//! Done by putting the processor into debug mode.

//! @return  TRUE if we halt successfully, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
GdbServer::targetHalt ()
{
  if (!writeReg (cCore (), DEBUGCMD_REGNUM,
		 TargetControl::DEBUGCMD_COMMAND_HALT))
    cerr << "Warning: targetHalt failed to write HALT to DEBUGCMD." << endl;

  if (si->debugStopResume ())
      cerr << "DebugStopResume: Wrote HALT to DEBUGCMD" << endl;

  if (!isCoreHalted (cCore ()))
    {
      sleep (1);

      // Try again, then give up
      if (!isCoreHalted (cCore ()))
	{
	  cerr << "Warning: Target has not halted after 1 sec " << endl;
	  uint32_t val;
	  if (readReg (cCore (), DEBUGSTATUS_REGNUM, val))
	    cerr << "         - core ID = " << cCore ()
		 << ", DEBUGSTATUS = 0x" << hex << setw (8) << setfill ('0')
		 << val << setfill (' ') << setw (0) << dec << endl;
	  else
	    cerr << "         - unable to access DEBUG register." << endl;

	  return false;
	}
    }

  if (si->debugStopResume ())
    cerr << "DebugStopResume: Target halted." << endl;

  return true;

}	// targetHalt ()


//---------------------------------------------------------------------------
//! Insert a breakpoint instruction.

//! @param [in] addr  Where to put the breakpoint
//-----------------------------------------------------------------------------
void
GdbServer::insertBkptInstr (uint32_t addr)
{
  writeMem16 (cCore (), addr, BKPT_INSTR);

  if (si->debugStopResumeDetail ())
    cerr << "DebugStopResumeDetail: insert breakpoint at " << addr
	 << endl;

}	// putBkptInstr


//---------------------------------------------------------------------------
//! Check if hit on Breakpoint instruction
//
//-----------------------------------------------------------------------------
bool
GdbServer::isHitInBreakPointInstruction (uint32_t bkpt_addr)
{
  uint16_t val;
  readMem16 (cCore (), bkpt_addr, val);
  return (BKPT_INSTR == val);

}


//-----------------------------------------------------------------------------
//! Check if core is halted.

//! @param[in] coreId  The core to inspect
//! @return  TRUE if the core is both halted and has no pending load or
//!          fetch. FALSE otherwise.
//-----------------------------------------------------------------------------
bool
GdbServer::isCoreHalted (CoreId  coreId)
{
  uint32_t debugstatus = readReg (coreId, DEBUGSTATUS_REGNUM);
  uint32_t haltStatus = debugstatus & TargetControl::DEBUGSTATUS_HALT_MASK;
  uint32_t extPendStatus =
    debugstatus & TargetControl::DEBUGSTATUS_EXT_PEND_MASK;

  bool isHalted = haltStatus == TargetControl::DEBUGSTATUS_HALT_HALTED;
  bool noPending = extPendStatus == TargetControl::DEBUGSTATUS_EXT_PEND_NONE;

  return isHalted && noPending;

}	// isCoreHalted ()


//-----------------------------------------------------------------------------
//! Check if core is idle.

//! @param[in] coreId  The core to inspect
//! @return  TRUE if the core is idle, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
GdbServer::isCoreIdle (CoreId  coreId)
{
  uint32_t status = readReg (coreId, STATUS_REGNUM);
  uint32_t idleStatus = status & TargetControl::STATUS_ACTIVE_MASK;

  // Warn if a software exception is pending
  uint32_t ex = status & TargetControl::STATUS_EXCAUSE_MASK;
  if (ex != TargetControl::STATUS_EXCAUSE_NONE)
      cerr << "Warning: Unexpected pending SW exception 0x" << hex
	   << (ex >> TargetControl::STATUS_EXCAUSE_SHIFT) << "." << endl;

  return idleStatus == TargetControl::STATUS_ACTIVE_IDLE;

}	// isCoreIdle ()


//-----------------------------------------------------------------------------
//! Check if global interrupts are enabled for core.

//! @param[in] coreId  The core to inspect
//! @return  TRUE if the core has global interrupts enabled, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
GdbServer::isCoreGIntsEnabled (CoreId  coreId)
{
  uint32_t status = readReg (coreId, STATUS_REGNUM);
  uint32_t gidStatus = status & TargetControl::STATUS_GID_MASK;

  return gidStatus == TargetControl::STATUS_GID_ENABLED;

}	// isCoreGIntsEnabled ()


//-----------------------------------------------------------------------------
//! Check is core has been stopped at exception state

//! @param[in] coreId  The core to check.
//! @return  The GDB signal corresponding to any exception.
//-----------------------------------------------------------------------------
GdbServer::TargetSignal
GdbServer::getException (CoreId coreId)
{
  uint32_t coreStatus = readStatus (coreId);
  uint32_t exbits = coreStatus & TargetControl::STATUS_EXCAUSE_MASK;

  switch (exbits)
    {
    case TargetControl::STATUS_EXCAUSE_NONE:
      return TARGET_SIGNAL_NONE;

    case TargetControl::STATUS_EXCAUSE_LDST:
      return TARGET_SIGNAL_BUS;

    case TargetControl::STATUS_EXCAUSE_FPU:
      return TARGET_SIGNAL_FPE;

    case TargetControl::STATUS_EXCAUSE_UNIMPL:
      return TARGET_SIGNAL_ILL;

    default:
      // @todo Can we get this? Corresponds to STATUS_EXCAUSE_LSTALL or
      //       STATUS_EXCAUSE_FSTALL being set.
      return TARGET_SIGNAL_ABRT;
    }
}	// getException ()


//-----------------------------------------------------------------------------
//! Put bkpt instructions into IVT

//! The single step mode can be broken when interrupt is fired. (ISR call)
//! The instructions in IVT should be saved and replaced by BKPT

//! @param[in] coreId  The core to save the IVT for.
//-----------------------------------------------------------------------------
void
GdbServer::saveIVT (CoreId coreId)
{
  readMemBlock (coreId, TargetControl::IVT_SYNC, fIVTSaveBuff,
		sizeof (fIVTSaveBuff));

}	// saveIVT ()


//-----------------------------------------------------------------------------
//! Restore instructions to IVT

//! The single step mode can be broken when interrupt is fired, (ISR call)
//! The BKPT instructions in IVT should be restored by real instructions

//! @param[in] coreId  The core to restore the IVT for.
//-----------------------------------------------------------------------------
void
GdbServer::restoreIVT (CoreId coreId)
{

  writeMemBlock (coreId, TargetControl::IVT_SYNC, fIVTSaveBuff,
		 sizeof (fIVTSaveBuff));

}	// restoreIVT ()


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
      // For now we don't support this.
      pkt->packStr ("");
      rsp->putPkt (pkt);
      return;
    }
  else if (0 == strncmp ("vCont", pkt->data, strlen ("vCont")))
    {
      // This shouldn't happen, because we've reported non-support via vCont?
      // above
      cerr << "Warning: RSP vCont not supported: ignored" << endl;
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
  if (!writeMemBlock (gCore (), addr, bindat, len))
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
  if (BKPT_INSTLEN != len)
    {
      cerr << "Warning: RSP matchpoint deletion length " << len
	<< " not valid: " << BKPT_INSTLEN << " assumed" << endl;
      len = BKPT_INSTLEN;
    }

  // Sort out the type of matchpoint
  switch (type)
    {
    case BP_MEMORY:
      //Memory breakpoint - replace the original instruction.
      if (mpHash->remove (type, addr, &instr))
	{
	  writeMem16 (cCore (), addr, instr);
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
}				// rspRemoveMatchpoint()


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
  if (BKPT_INSTLEN != len)
    {
      cerr << "Warning: RSP matchpoint insertion length " << len
	<< " not valid: " << BKPT_INSTLEN << " assumed" << endl;
      len = BKPT_INSTLEN;
    }

  // Sort out the type of matchpoint
  uint16_t bpMemVal;
  //bool bpMemValSt;
  switch (type)
    {
    case BP_MEMORY:
      // Memory breakpoint - substitute a BKPT instruction

      readMem16 (cCore (), addr, bpMemVal);
      mpHash->add (type, addr, bpMemVal);

      insertBkptInstr (addr);

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
}				// rspInsertMatchpoint()


//! Sotware reset of the processor

//! This is achieved by repeatedly writing 1 (RESET exception) and finally 0
//! to the  RESETCORE_REGNUM register
void
GdbServer::targetSwReset ()
{
  for (unsigned ncyclesReset = 0; ncyclesReset < 12; ncyclesReset++)
    (void) writeReg (cCore (), RESETCORE_REGNUM, 1);

  writeReg (cCore (), RESETCORE_REGNUM, 0);

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
//! Read a block of memory from the target

//! @param[in]  coreId  The core to read from
//! @param[in]  addr    The address to read from
//! @param[out] buf     Where to put the data read
//! @param[in]  len     The number of bytes to read
//! @return  TRUE on success, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
GdbServer::readMemBlock (CoreId  coreId,
			 uint32_t  addr,
			 uint8_t* buf,
			 size_t  len) const
{
  return fTargetControl->readBurst (coreId, addr, buf, len);

}	// readMemBlock ()


//-----------------------------------------------------------------------------
//! Write a block of memory to the target

//! @param[in] coreId  The core to write to
//! @param[in] addr    The address to write to
//! @param[in] buf     Where to get the data to be written
//! @param[in] len     The number of bytes to read
//! @return  TRUE on success, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
GdbServer::writeMemBlock (CoreId  coreId,
			  uint32_t  addr,
			  uint8_t* buf,
			  size_t  len) const
{
  return fTargetControl->writeBurst (coreId, addr, buf, len);

}	// writeMemBlock ()


//-----------------------------------------------------------------------------
//! Read a 32-bit value from memory in the target

//! In this version the caller is responsible for error handling.

//! @param[in]  coreId  The core to read from
//! @param[in]  addr    The address to read from.
//! @param[out] val     The value read.
//! @return  TRUE on success, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
GdbServer::readMem32 (CoreId  coreId,
		      uint32_t  addr,
		      uint32_t& val) const
{
  return fTargetControl->readMem32 (coreId, addr, val);

}	// readMem32 ()


//-----------------------------------------------------------------------------
//! Read a 32-bit value from memory in the target

//! In this version we print a warning if the read fails.

//! @param[in]  coreId  The core to read from
//! @param[in]  addr    The address to read from.
//! @return  The value read, undefined if there is a failure.
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readMem32 (CoreId  coreId,
		      uint32_t  addr) const
{
  uint32_t val;
  if (!fTargetControl->readMem32 (coreId, addr, val))
    cerr << "Warning: readMem32 failed." << endl;
  return val;

}	// readMem32 ()


//-----------------------------------------------------------------------------
//! Write a 32-bit value to memory in the target

//! The caller is responsible for error handling.

//! @param[in] coreId  The core to write to
//! @param[in]  addr   The address to write to.
//! @param[out] val    The value to write.
//! @return  TRUE on success, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
GdbServer::writeMem32 (CoreId  coreId,
		       uint32_t  addr,
		       uint32_t  val) const
{
  return fTargetControl->writeMem32 (coreId, addr, val);

}	// writeMem32 ()


//-----------------------------------------------------------------------------
//! Read a 16-bit value from memory in the target

//! In this version the caller is responsible for error handling.

//! @param[in]  coreId  The core to read from
//! @param[in]  addr    The address to read from.
//! @param[out] val     The value read.
//! @return  TRUE on success, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
GdbServer::readMem16 (CoreId  coreId,
		      uint32_t  addr,
		      uint16_t& val) const
{
  return fTargetControl->readMem16 (coreId, addr, val);

}	// readMem16 ()


//-----------------------------------------------------------------------------
//! Read a 16-bit value from memory in the target

//! In this version we print a warning if the read fails.

//! @param[in]  coreId  The core to read from
//! @param[in]  addr    The address to read from.
//! @return  The value read, undefined if there is a failure.
//-----------------------------------------------------------------------------
uint16_t
GdbServer::readMem16 (CoreId  coreId,
		      uint32_t  addr) const
{
  uint16_t val;
  if (!fTargetControl->readMem16 (coreId, addr, val))
    cerr << "Warning: readMem16 failed." << endl;
  return val;

}	// readMem16 ()


//-----------------------------------------------------------------------------
//! Write a 16-bit value to memory in the target

//! The caller is responsible for error handling.

//! @param[in] coreId  The core to write to
//! @param[in]  addr   The address to write to.
//! @param[out] val    The value to write.
//! @return  TRUE on success, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
GdbServer::writeMem16 (CoreId  coreId,
		       uint32_t  addr,
		       uint16_t  val) const
{
  return fTargetControl->writeMem16 (coreId, addr, val);

}	// writeMem16 ()


//-----------------------------------------------------------------------------
//! Read a 8-bit value from memory in the target

//! In this version the caller is responsible for error handling.

//! @param[in]  coreId  The core to read from
//! @param[in]  addr    The address to read from.
//! @param[out] val     The value read.
//! @return  TRUE on success, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
GdbServer::readMem8 (CoreId  coreId,
		     uint32_t  addr,
		     uint8_t& val) const
{
  return fTargetControl->readMem8 (coreId, addr, val);

}	// readMem8 ()


//-----------------------------------------------------------------------------
//! Read a 8-bit value from memory in the target

//! In this version we print a warning if the read fails.

//! @param[in]  coreId  The core to read from
//! @param[in]  addr    The address to read from.
//! @return  The value read, undefined if there is a failure.
//-----------------------------------------------------------------------------
uint8_t
GdbServer::readMem8 (CoreId  coreId,
		     uint32_t  addr) const
{
  uint8_t val;
  if (!fTargetControl->readMem8 (coreId, addr, val))
    cerr << "Warning: readMem8 failed." << endl;
  return val;

}	// readMem8 ()


//-----------------------------------------------------------------------------
//! Write a 8-bit value to memory in the target

//! The caller is responsible for error handling.

//! @param[in] coreId  The core to write to
//! @param[in]  addr   The address to write to.
//! @param[out] val    The value to write.
//! @return  TRUE on success, FALSE otherwise.
//-----------------------------------------------------------------------------
bool
GdbServer::writeMem8 (CoreId  coreId,
		      uint32_t  addr,
		      uint8_t   val) const
{
  return fTargetControl->writeMem8 (coreId, addr, val);

}	// writeMem8 ()


//-----------------------------------------------------------------------------
//! Read the value of an Epiphany register from hardware

//! This is just a wrapper for reading memory, since the GPR's are mapped into
//! core memory. In this version the user is responsible for error handling.

//! @param[in]   coreId  The core to read from
//! @param[in]   regnum  The GDB register number
//! @param[out]  regval  The value read
//! @return  True on success, false otherwise
//-----------------------------------------------------------------------------
bool
GdbServer::readReg (CoreId     coreId,
		    unsigned int regnum,
		    uint32_t&    regval) const
{
  return fTargetControl->readMem32 (coreId, regAddr (regnum), regval);

}	// readReg ()


//-----------------------------------------------------------------------------
//! Read the value of an Epiphany register from hardware

//! This is just a wrapper for reading memory, since the GPR's are mapped into
//! core memory. In this version, we print a warning if the read fails.

//! Overloaded version to return the value directly.

//! @param[in]   coreId  The core to read from
//! @param[in]   regnum  The GDB register number
//! @return  The value read
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readReg (CoreId     coreId,
		    unsigned int regnum) const
{
  uint32_t regval;
  if (!fTargetControl->readMem32 (coreId, regAddr (regnum), regval))
    cerr << "Warning: readReg failed." << endl;
  return regval;

}	// readReg ()


//-----------------------------------------------------------------------------
//! Write the value of an Epiphany register to hardware

//! This is just a wrapper for writing memory, since the GPR's are mapped into
//! core memory

//! @param[in]  coreId  The core to write to
//! @param[in]  regnum  The GDB register number
//! @param[in]  regval  The value to write
//! @return  True on success, false otherwise
//-----------------------------------------------------------------------------
bool
GdbServer::writeReg (CoreId  coreId,
		     unsigned int regnum,
		     uint32_t value) const
{
  return  fTargetControl->writeMem32 (coreId, regAddr (regnum), value);

}	// writeReg ()


//-----------------------------------------------------------------------------
//! Read the value of the Core ID

//! A convenience routine and internal to external conversion

//! @param[in]  coreId  The core to read from
//! @return  The value of the Core ID
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readCoreId (CoreId coreId) const
{
  return readReg (coreId, COREID_REGNUM);

}	// readCoreId ()


//-----------------------------------------------------------------------------
//! Read the value of the Core Status (a SCR)

//! A convenience routine and internal to external conversion

//! @param[in]  coreId  The core to read from
//! @return  The value of the Status
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readStatus (CoreId  coreId) const
{
  return readReg (coreId, STATUS_REGNUM);

}	// readStatus ()


//-----------------------------------------------------------------------------
//! Read the value of the Program Counter (a SCR)

//! A convenience routine and internal to external conversion

//! @param[in]  coreId  The core to read from
//! @return  The value of the PC
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readPc (CoreId  coreId) const
{
  return readReg (coreId, PC_REGNUM);

}	// readPc ()


//-----------------------------------------------------------------------------
//! Write the value of the Program Counter (a SCR)

//! A convenience function and internal to external conversion

//! @param[in] coreId  The core to write to
//! @param[in] addr    The address to write into the PC
//-----------------------------------------------------------------------------
void
GdbServer::writePc (CoreId  coreId,
		    uint32_t addr)
{
  writeReg (coreId, PC_REGNUM, addr);

}	// writePc ()


//-----------------------------------------------------------------------------
//! Read the value of the Link register (a GR)

//! A convenience routine and internal to external conversion

//! @param[in]  coreId  The core to read from
//! @return  The value of the link register
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readLr (CoreId  coreId) const
{
  return readReg (coreId, LR_REGNUM);

}	// readLr ()


//-----------------------------------------------------------------------------
//! Write the value of the Link register (GR)

//! A convenience function and internal to external conversion

//! @param[in] coreId  The core to write to
//! @param[in] addr    The address to write into the Link register
//-----------------------------------------------------------------------------
void
GdbServer::writeLr (CoreId  coreId,
		    uint32_t  addr)
{
  writeReg (coreId, LR_REGNUM, addr);

}	// writeLr ()


//-----------------------------------------------------------------------------
//! Read the value of the FP register (a GR)

//! A convenience routine and internal to external conversion

//! @param[in]  coreId  The core to read from
//! @return  The value of the frame pointer register
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readFp (CoreId  coreId) const
{
  return readReg (coreId, FP_REGNUM);

}	// readFp ()


//-----------------------------------------------------------------------------
//! Write the value of the Frame register (GR)

//! A convenience function and internal to external conversion

//! @param[in] coreId  The core to write to
//! @param[in] addr    The address to write into the Frame pointer register
//-----------------------------------------------------------------------------
void
GdbServer::writeFp (CoreId  coreId,
		    uint32_t addr)
{
  writeReg (coreId, FP_REGNUM, addr);

}	// writeFp ()


//-----------------------------------------------------------------------------
//! Read the value of the SP register (a GR)

//! A convenience routine and internal to external conversion

//! @param[in]  coreId  The core to read from
//! @return  The value of the frame pointer register
//-----------------------------------------------------------------------------
uint32_t
GdbServer::readSp (CoreId  coreId) const
{
  return readReg (coreId, SP_REGNUM);

}	// readSp ()


//-----------------------------------------------------------------------------
//! Write the value of the Stack register (GR)

//! A convenience function and internal to external conversion

//! @param[in] coreId  The core to write to
//! @param[in] addr    The address to write into the Stack pointer register
//-----------------------------------------------------------------------------
void
GdbServer::writeSp (CoreId  coreId,
		    uint32_t addr)
{
  writeReg (coreId, SP_REGNUM, addr);

}	// writeSp ()


//-----------------------------------------------------------------------------
//! Get the core used for step and continue

//! This is assumed to be a valid value. But we can't yield a single core for
//! values of -1 (gives a warning), while for 0 we just return the first core.

//! @return  A coreID
//-----------------------------------------------------------------------------
CoreId
GdbServer::cCore ()
{
  CoreId coreId;

  switch (currentCThread)
    {
    case -1:
      cerr << "Warning: Cannot give a single 'c' core for thread -1" << endl;
      coreId = thread2core.begin ()->first;

    case 0:
      coreId = thread2core.begin ()->first;

    default:
      coreId = thread2core[currentCThread];
    }

  return coreId;

}	// cCore ()


//-----------------------------------------------------------------------------
//! Get the core used for general access

//! This is assumed to be a valid value.

//! @return  A coreID
//-----------------------------------------------------------------------------
CoreId
GdbServer::gCore ()
{
  CoreId coreId;

  switch (currentGThread)
    {
    case -1:
      cerr << "Warning: Cannot give a single 'g' core for thread -1" << endl;
      coreId = thread2core.begin ()->first;

    case 0:
      coreId = thread2core.begin ()->first;

    default:
      coreId = thread2core[currentGThread];
    }

  return coreId;

}	// cCore ()


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

//! @param[in]  coreId    The core we are looking at
//! @param[in]  instr     16-bit instruction
//! @param[in]  addr      Address of instruction being examined
//! @param[out] destAddr  Destination address
//! @return  TRUE if this was a 16-bit jump destination
//-----------------------------------------------------------------------------
bool
GdbServer::getJump (CoreId  coreId,
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
      destAddr = readReg (coreId, R0_REGNUM + rn);
      return true;
    }
  else if (0x1d2 == getOpcode10 (instr))
    {
      // RTI
      destAddr = readReg (coreId, IRET_REGNUM);
      return true;
    }
  else
    return false;

}	// getJump ()


//-----------------------------------------------------------------------------
//! Get a 32-bit jump destination

//! Possibilites are a branch (immediate offset) or jump (register address)

//! @param[in]  coreId    The core we are looking at
//! @param[in]  instr     32-bit instruction
//! @param[in]  addr      Address of instruction being examined
//! @param[out] destAddr  Destination address
//! @return  TRUE if this was a 16-bit jump destination
//-----------------------------------------------------------------------------
bool
GdbServer::getJump (CoreId  coreId,
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
      destAddr = readReg (coreId, R0_REGNUM + rn);
      return true;
    }
  else
    return false;

}	// getJump ()


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


//! Map GDB register number to hardware register memory address

//! @param[in] regnum  GDB register number to look up
//! @return the (local) hardware address in memory of the register
uint32_t
GdbServer::regAddr (unsigned int  regnum) const
{
  static const uint32_t regs [NUM_REGS] = {
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

  assert (regnum < NUM_REGS);
  return regs[regnum];

}	// regAddr ()


//-----------------------------------------------------------------------------
//! Convenience function to turn an integer into a string

//! @param[in] val    The value to convert
//! @param[in] base   The base for conversion. Default 10, valid values 8, 10
//!                   or 16. Other values will reset the iostream flags.
//! @param[in] width  The width to pad (with zeros).
//-----------------------------------------------------------------------------
string
GdbServer::intStr (int  val,
		   int  base,
		   int  width) const
{
  ostringstream  os;

  os << setbase (base) << setfill ('0') << setw (width) << val;
  return os.str ();

}	// intStr ()


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
