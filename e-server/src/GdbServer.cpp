// GDB RSP server class: Definition.

// Copyright (C) 2008, 2009, Embecosm Limited
// Copyright (C) 2009-2014, 2016 Adapteva Inc.
// Copyright (C) 2016 Pedro Alves

// Contributor: Oleg Raikhman <support@adapteva.com>
// Contributor: Yaniv Sapir <support@adapteva.com>
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
#include "GdbTid.h"

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
  mCurrentThread (NULL),
  mNotifyingP (false),
  si (_si),
  fTargetControl (NULL)
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
	  mDebugMode = ALL_STOP;
	  // Say we're in a notification sequence until '?' is fully
	  // processed.  This has the effect of inhibiting
	  // rspClientNotifications.
	  mNotifyingP = true;

	  // Reconnect and stall the processor on a new connection
	  if (!rsp->rspConnect ())
	    {
	      // Serious failure. Must abort execution.
	      cerr << "ERROR: Failed to reconnect to client. Exiting.";
	      exit (EXIT_FAILURE);
	    }

	  cout << "INFO: connected to port " << si->port () << endl;
	}

      // Get a RSP client request
      if (si->debugTranDetail ())
	cerr << "DebugTranDetail: Getting RSP client request." << endl;

      // Deal with any RSP client request.  May return immediately in
      // non-stop mode.
      rspClientRequest ();

      // We will have dealt with most requests.  In non-stop mode the
      // target will be running, but we have not yet responded.  In
      // all-stop mode we have stopped all threads and responded.
      if (si->debugTranDetail ())
	cerr << "DebugTranDetail: RSP client request complete" << endl;

      if (mDebugMode == NON_STOP)
	{
	  // Check if any thread stopped and if we thus need to send
	  // any notifications.
	  if (si->debugTranDetail ())
	    cerr << "DebugTranDetail: Sending RSP client notifications."
		 << endl;

	  rspClientNotifications ();
	}
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
  mProcesses[mNextPid] = mIdleProcess;
  mNextPid++;

  // Initialize a thread for each core. A thread is referenced by its thread
  // ID, and from the thread we can get the coreId (accessor). We need a
  // reverse map, to allow us to gte from core to thread ID if we ever need to
  // in the future.
  for (vector <CoreId>::iterator it = fTargetControl->coreIdBegin ();
       it != fTargetControl->coreIdEnd ();
       it++)
    {
      bool res;
      CoreId coreId = *it;
      int tid = (coreId.row () + 1) * 100 + coreId.col () + 1;
      Thread* thread = new Thread (coreId, fTargetControl, si, tid);

      mThreads[tid] = thread;
      mCore2Tid [coreId] = tid;

      // Add to idle process
      res = mIdleProcess->addThread (thread);
      assert (res);
    }

  mCurrentProcess = mIdleProcess;

}	// initProcesses ()


//-----------------------------------------------------------------------------
//! Halt and activate all threads of a process.
//-----------------------------------------------------------------------------
bool
GdbServer::haltAndActivateProcess (ProcessInfo *process)
{
  bool isHalted = true;

  for (set <Thread*>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    {
      Thread* thread = *it;
      isHalted &= thread->halt ();

      if (thread->isIdle ())
	{
	  if (thread->activate ())
	    {
	      if (si->debugStopResumeDetail ())
		cerr << "DebugStopResumeDetail: Thread " << thread->tid ()
		     << " idle on attach: forced active." << endl;
	    }
	  else
	    {
	      if (si->debugStopResumeDetail ())
		cerr << "DebugStopResumeDetail: Thread " << thread->tid ()
		     << " idle on attach: failed to force active." << endl;
	    }
	}
    }

  return isHalted;
}	// haltAndActivateProcess ()


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

  isHalted = haltAndActivateProcess (process);

  mCurrentProcess = process;
  mCurrentThread = *process->threadBegin ();

  Thread *thread = *process->threadBegin ();

  // @todo Does this belong here. Is TARGET_SIGNAL_HUP correct?
  if (!isHalted)
    rspReportException (thread, TARGET_SIGNAL_HUP);

  // At this point, no thread should be resumed until the client
  // tells us to.
  markAllStopped ();
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
      ProcessInfo* process = getProcess (pid);

      for (set <Thread*>::iterator it = process->threadBegin ();
	   it != process->threadEnd ();
	   it++)
	{
	  Thread* thread = *it;
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
//! Handle the '?' packet.
//-----------------------------------------------------------------------------
void
GdbServer::rspStatus ()
{
  // All-stop mode requires we halt on attaching.
  haltAndActivateProcess (mCurrentProcess);

  if (mDebugMode == ALL_STOP)
    {
      // Return last signal ID
      mCurrentThread = *mCurrentProcess->threadBegin ();
      rspReportException (mCurrentThread, mCurrentThread->pendingSignal ());
      markAllStopped ();
    }
  else
    {
      // In the case of non-stop mode this means refinding the
      // list of stopped threads.  Also, '?' behaves like a
      // notification.  We report a stopped thread now, and if
      // there are more, GDB will keep sending vStopped until it
      // gets an OK.
      mNotifyingP = true;

      ProcessInfo* process = mCurrentProcess;

      for (set <Thread*>::iterator it = process->threadBegin ();
	   it != process->threadEnd ();
	   it++)
	{
	  Thread* thread = *it;

	  thread->setLastAction (ACTION_CONTINUE);
	}

      rspVStopped ();
    }

}	// rspStatus ()


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
  if (mDebugMode == NON_STOP && !rsp->inputReady ())
    return;

  if (!rsp->getPkt (pkt))
    {
      rspDetach (mCurrentProcess->pid ());
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
      rspStatus ();
      break;

    case 'D':
      // Detach GDB. Do this by closing the client. The rules say that
      // execution should continue, so unstall the processor.
      rspDetach (mCurrentProcess->pid ());
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
      rsp->rspClose ();

      break;

    case 'F':

      // Parse the F reply packet
      rspFileIOreply ();

      // For all-stop mode we assume all threads need restarting.
      resumeAllThreads ();

      // Go back to waiting.
      waitAllThreads ();
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
      rspDetach (mCurrentProcess->pid ());
      rsp->rspClose ();			// Close the connection.

      break;

    case 'm':
      // Read memory (symbolic)
      rspReadMem ();
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
//! Prepare a stop reply.
//-----------------------------------------------------------------------------
string
GdbServer::rspPrepareStopReply (Thread *thread, TargetSignal sig)
{
  ostringstream oss;

  oss << "T" << Utils::intStr (sig, 16, 2)
      << "thread:" << GdbTid (mCurrentProcess, thread) << ";";

  if (sig == TARGET_SIGNAL_TRAP)
    oss << "swbreak:;";

  return oss.str ();
}				// rspPrepareStopReply()


//-----------------------------------------------------------------------------
//! Deal with any client notifications
//-----------------------------------------------------------------------------
void
GdbServer::rspClientNotifications ()
{
  Thread* thread;

  // If we're already notifying, hold until the client acks the
  // previous notification and finishes the vStopped sequence.
  if (mNotifyingP)
    return;

  thread = findStoppedThread ();
  if (thread != NULL)
    {
      mNotifyingP = true;

      TargetSignal sig = thread->pendingSignal ();

      string reply = "Stop:" + rspPrepareStopReply (thread, sig);
      pkt->packStr (reply.c_str ());;
      rsp->putNotification (pkt);

      thread->setPendingSignal (TARGET_SIGNAL_NONE);
      thread->setLastAction (ACTION_STOP);
    }
}	// rspClientNotifications ()

//-----------------------------------------------------------------------------
//! Send a packet acknowledging an exception has occurred

//! @param[in] thread  The thread we are acknowledging.
//! @param[in] sig     The signal to report back.
//-----------------------------------------------------------------------------
void
GdbServer::rspReportException (Thread *thread, TargetSignal sig)
{
  if (si->debugStopResume ())
    cerr << "DebugStopResume: Report exception  for thread " << thread->tid ()
	 << " with GDB signal " << sig << endl;

  // In all-stop mode, ensure all threads are halted.
  if (mDebugMode == ALL_STOP)
    {
      haltAllThreads ();

      markPendingStops (thread);

      mCurrentThread = thread;
    }

  string reply = rspPrepareStopReply (thread, sig);

  pkt->packStr (reply.c_str ());
  rsp->putPkt (pkt);

}	// rspReportException()

//-----------------------------------------------------------------------------
//! Find any/all stopped threads in a process.

//-----------------------------------------------------------------------------
//! Find the first thread that the client had set to continue that is
//! now halted.
//-----------------------------------------------------------------------------
Thread*
GdbServer::findStoppedThread ()
{
  ProcessInfo *process = mCurrentProcess;

  for (set <Thread*>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       ++it)
    {
      Thread *thread = *it;

      if (thread->lastAction () == ACTION_CONTINUE
	  && thread->isHalted ())
	{
	  TargetSignal sig = findStopReason (thread);
	  thread->setPendingSignal (sig);
	  return thread;
	}
    }

  return NULL;
}	// findStoppedThread()


//-----------------------------------------------------------------------------
//! Handle the "vStopped" packet.
//-----------------------------------------------------------------------------

void
GdbServer::rspVStopped ()
{
  Thread* thread;

  // Report any threads that are marked as stopped.  We can be called
  // when there are no stopped threads, in which case we return "OK".
  thread = findStoppedThread ();
  if (thread != NULL)
    {
      TargetSignal sig = thread->pendingSignal ();

      rspReportException (thread, sig);

      thread->setLastAction (ACTION_STOP);
      thread->setPendingSignal (TARGET_SIGNAL_NONE);
    }
  else
    {
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
      // Ready to start a new reporting sequence.
      mNotifyingP = false;
    }
}	// rspVStopped()


//-----------------------------------------------------------------------------
//! Stop in response to a ctrl-C

//! We just halt everything. Let GDB worry about it later.
//-----------------------------------------------------------------------------
void
GdbServer::rspSuspend ()
{
  // Halt all threads.
  if (!haltAllThreads ())
    cerr << "Warning: suspend failed to halt all threads." << endl;

  // Report to gdb the target has been stopped

  // Pick the first thread that is supposed to be continued.
  Thread *signal_thread = NULL;
  for (set <Thread*>::iterator it = mCurrentProcess->threadBegin ();
       it != mCurrentProcess->threadEnd ();
       ++it)
    {
      Thread* thread = *it;

      if (thread->lastAction () == ACTION_CONTINUE)
	{
	  signal_thread = thread;
	  break;
	}
    }

  if (signal_thread == NULL)
    {
      cerr << "Warning: suspend failed to find continued thread." << endl;
      return;
    }

  rspReportException (signal_thread, TARGET_SIGNAL_INT);

  // At this point, no thread should be resumed until the client
  // tells us to.
  markAllStopped ();
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
  Thread* thread = mCurrentThread;

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

//-----------------------------------------------------------------------------
//! Redirect the SDIO to client GDB.

//! The requests are sent using F packets. The open, write, read and close
//! system calls are supported

//! @param[in] thread   Thread making the I/O request
//! @param[in] trap  The number of the trap.
//-----------------------------------------------------------------------------
void
GdbServer::redirectStdioOnTrap (Thread *thread,
				uint8_t trap)
{
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
      rspReportException (thread, TARGET_SIGNAL_QUIT);
      break;
    case TRAP_PASS:
    case TRAP_FAIL:
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
  assert (mCurrentThread != NULL);

  // Start timing if debugging
  if (si->debugStopResumeDetail ())
    fTargetControl->startOfBaudMeasurement ();

  // Get each reg
  for (unsigned int r = 0; r < NUM_REGS; r++)
    {
      uint32_t val;
      unsigned int pktOffset = r * TargetControl::E_REG_BYTES * 2;

      // Not all registers are necessarily supported.
      if (mCurrentThread->readReg (r, val))
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
  assert (mCurrentThread != NULL);

  // All registers
  for (unsigned int r = 0; r < NUM_REGS; r++)
      (void) mCurrentThread->writeReg (r, Utils::hex2Reg (&(pkt->data[r * 8])));

  // Acknowledge (always OK for now).
  pkt->packStr ("OK");
  rsp->putPkt (pkt);

}				// rspWriteAllRegs()


//-----------------------------------------------------------------------------
//! Set the thread number of subsequent operations.

//! A tid of 0 means "any thread".  0 causes all sorts of problems
//! later, so we replace it by the first thread in the current
//! process.
//-----------------------------------------------------------------------------
void
GdbServer::rspSetThread ()
{
  char  c;
  Thread* thread;

  if (pkt->data[0] != 'H' || pkt->data[1] != 'g')
    {
      rspUnknownPacket ();
      return;
    }

  GdbTid tid = GdbTid::fromString (&pkt->data[2]);
  if (tid.tid () ==  0)
    thread = *(mCurrentProcess->threadBegin ());
  else
    thread = getThread (tid.tid ());

  mCurrentThread = thread;

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

  assert (mCurrentThread != NULL);

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

  // Read the bytes from memory
  {
    uint8_t buf[len];

    bool retReadOp =
      mCurrentThread->readMemBlock (addr, buf, len);

    if (!retReadOp)
      {
	pkt->packStr ("E01");
	rsp->putPkt (pkt);
	return;
      }

    hideBreakpoints (mCurrentThread, addr, buf, len);

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
//! Read a single register

//! The register is returned as a sequence of bytes in target endian order.

//! Each byte is packed as a pair of hex digits.

//! @note Not believed to be used by the GDB client at present.
//-----------------------------------------------------------------------------
void
GdbServer::rspReadReg ()
{
  assert (mCurrentThread != NULL);

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
  if (!mCurrentThread->readReg (regnum, regval))
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
  assert (mCurrentThread != NULL);

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
  if (!mCurrentThread->writeReg (regnum, Utils::hex2Reg (valstr)))
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
      // Return the current thread ID.

      if (mCurrentThread == NULL)
	mCurrentThread = *mCurrentProcess->threadBegin ();
      sprintf (pkt->data, "QCp%x.%x",
	       mCurrentProcess->pid (), mCurrentThread->tid ());
      pkt->setLen (strlen (pkt->data));
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
      sprintf (pkt->data,
	       "PacketSize=%x;"
	       "qXfer:osdata:read+;"
	       "qXfer:threads:read+;"
	       "swbreak+;"
	       "QNonStop+;"
	       "multiprocess+",
	       pkt->getBufSize ());
      pkt->setLen (strlen (pkt->data));
      rsp->putPkt (pkt);
    }
  else if (0 == strncmp ("qXfer:", pkt->data, strlen ("qXfer:")))
    {
      rspTransfer ();
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
//! Return extra info about a thread.
//-----------------------------------------------------------------------------
string
GdbServer::rspThreadExtraInfo (Thread* thread)
{
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
  return res;
}	// rspThreadExtraInfo ()

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
      CoreId absGCoreId = mCurrentThread->readCoreId ();
      CoreId relGCoreId = fTargetControl->abs2rel (absGCoreId);
      ostringstream  oss;

      oss << "General core ID: " << absGCoreId << " (absolute), "
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
  int pid = mNextPid;
  ProcessInfo *process = new ProcessInfo (pid);

  for (unsigned int r = 0; r < rows; r++)
    for (unsigned int c = 0; c < cols; c++)
      {
	CoreId coreId (row + r, col + c);
	Thread* thread = mThreads[mCore2Tid[coreId]];
	if (mIdleProcess->eraseThread (thread))
	  {
	    bool res = process->addThread (thread);
	    assert (res);
	  }
	else
	  {
	    // Yuk - blew up half way. Put all the threads back into the idle
	    // group, delete the process an give up.
	    for (set <Thread*>::iterator it = process->threadBegin ();
		 it != process->threadEnd ();
		 it++)
	      {
		bool res = process->eraseThread (*it);
		assert (res);
		res = mIdleProcess->addThread (*it);
		assert (res);
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

  mProcesses[pid] = process;
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
      mCurrentProcess = mProcesses[pid];

      ostringstream oss;
      oss << "Process ID now " << pid << "." << endl;

      // This may have invalidated the current thread. If so correct
      // it.  This is really a big dodgy - ultimately this needs
      // proper process handling.

      if ((NULL != mCurrentThread)
	  && (! mCurrentProcess->hasThread (mCurrentThread)))
	{
	  mCurrentThread = *(mCurrentProcess->threadBegin ());
	  oss << "- switching general thread to " << mCurrentThread << "."
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
//! Build the whole qXfer:threads:read reply string.
//-----------------------------------------------------------------------------

string
GdbServer::rspMakeTransferThreadsReply ()
{
  ostringstream  os;

  os << "<threads>\n";

  // Go through all threads in the current process.  Then for each
  // thread, build a <thread> element.
  for (set <Thread*>::iterator thr_it = mCurrentProcess->threadBegin ();
       thr_it != mCurrentProcess->threadEnd ();
       ++thr_it)
    {
      Thread* thread = *thr_it;

      os << "<thread";
      os << " id=\"" << GdbTid (mCurrentProcess, thread) << "\"";
      os << " core=\"" << hex << thread->coreId () << "\"";
      os << ">";
      os << rspThreadExtraInfo (thread);
      os << "</thread>\n";
    }

  os << "</threads>";

  return os.str ();
}

//-----------------------------------------------------------------------------
//! Handle an qXfer:$object:read request

//! @param[in] object  The object requested.
//! @param[in] reply A pointer to the reply string.  The reply is
//! rebuilt iff offset 0 is requested.
//! @param[in] maker   The factory function for the specified object.
//! @param[in] offset  Offset into the reply to send.
//! @param[in] length  Length of the reply to send.
//-----------------------------------------------------------------------------
void
GdbServer::rspTransferObject (const char *object,
			      string *reply,
			      makeTransferReplyFtype maker,
			      unsigned int offset,
			      unsigned int length)
{
  if (si->debugTrapAndRspCon ())
    {
      cerr << "RSP trace: qXfer:" << object << ":read:: offset 0x" << hex
	   << offset << ", length " << length << dec << endl;
    }

  // Get the data only for the first part of the reply.  The rest of
  // the time we are just sending the remainder of the string.
  if (0 == offset)
    *reply = (this->*maker) ();

  // Send the reply (or part reply) back
  unsigned int len = reply->size ();

  if (si->debugTrapAndRspCon ())
    {
      cerr << "RSP trace: " << object << " length " << len << endl;
      cerr << *reply << endl;
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

      pkt->packNStr (&(reply->c_str ()[offset]), pktlen, pkttype);
    }
}

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
	    rspTransferObject ("osdata", &osInfoReply,
			       &GdbServer::rspMakeOsDataReply, offset, length);
	  else if (0 == string ("processes").find (annex))
	    rspTransferObject ("osdata:processes", &osProcessReply,
			       &GdbServer::rspMakeOsDataProcessesReply, offset, length);
	  else if (0 == string ("load").find (annex))
	    rspTransferObject ("osdata:load", &osLoadReply,
			       &GdbServer::rspMakeOsDataLoadReply, offset, length);
	  else if (0 == string ("traffic").find (annex))
	    rspTransferObject ("osdata:traffic", &osTrafficReply,
			       &GdbServer::rspMakeOsDataTrafficReply, offset, length);
	}
      else if (0 == object.compare ("threads"))
	{
	  rspTransferObject ("threads", &qXferThreadsReply,
			     &GdbServer::rspMakeTransferThreadsReply, offset, length);
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

//! Return a list of all the tables we support
//-----------------------------------------------------------------------------

string
GdbServer::rspMakeOsDataReply ()
{
  string reply =
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

  return reply;
}

//-----------------------------------------------------------------------------
//! Make an OS processes request reply.

//! We need to return standard data, at this stage with all the cores. The
//! header and trailer part of the response is fixed.

//-----------------------------------------------------------------------------
string
GdbServer::rspMakeOsDataProcessesReply ()
{
  string reply =
    "<?xml version=\"1.0\"?>\n"
    "<!DOCTYPE target SYSTEM \"osdata.dtd\">\n"
    "<osdata type=\"processes\">\n";

  // Iterate through all processes.
  for (map <int, ProcessInfo *>::iterator pit = mProcesses.begin ();
       pit != mProcesses.end ();
       pit++)
    {
      ProcessInfo *process = pit->second;
      reply += "  <item>\n"
	"    <column name=\"pid\">";
      reply += Utils::intStr (process->pid ());
      reply += "</column>\n"
	"    <column name=\"user\">root</column>\n"
	"    <column name=\"command\"></column>\n"
	"    <column name=\"cores\">\n"
	"      ";

      for (set <Thread*>::iterator tit = process->threadBegin ();
	   tit != process->threadEnd (); tit++)
	{
	  if (tit != process->threadBegin ())
	    reply += ",";

	  reply += (*tit)->coreId ();
	}

      reply += "\n"
	"    </column>\n"
	"  </item>\n";
    }
  reply += "</osdata>";

  return reply;
}


//-----------------------------------------------------------------------------
//! Make an OS core load request reply.

//! This is epiphany specific.

//! @todo For now this is a stub which returns random values in the range 0 -
//! 99 for each core.

//-----------------------------------------------------------------------------
string
GdbServer::rspMakeOsDataLoadReply ()
{
  string reply =
    "<?xml version=\"1.0\"?>\n"
    "<!DOCTYPE target SYSTEM \"osdata.dtd\">\n"
    "<osdata type=\"load\">\n";

  for (map <CoreId, int>::iterator it = mCore2Tid.begin ();
       it != mCore2Tid.end ();
       it++)
    {
      reply +=
	"  <item>\n"
	"    <column name=\"coreid\">";
      reply += it->first;
      reply += "</column>\n";

      reply +=
	"    <column name=\"load\">";
      reply += Utils::intStr (random () % 100, 10, 2);
      reply += "</column>\n"
	"  </item>\n";
    }

  reply += "</osdata>";

  return reply;
}


//-----------------------------------------------------------------------------
//! Make an OS mesh load request reply.

//! This is epiphany specific.

//! When working out "North", "South", "East" and "West", the assumption is
//! that core (0,0) is at the North-East corner. We provide in and out traffic
//! for each direction.

//! @todo Currently only dummy data.

//-----------------------------------------------------------------------------
string
GdbServer::rspMakeOsDataTrafficReply ()
{
  string reply =
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

      reply +=
	"  <item>\n"
	"    <column name=\"coreid\">";
      reply += coreId;
      reply += "</column>\n";

      // See what adjacent cores we have.  Note that empty columns
      // confuse GDB! There is traffic on incoming edges, but not
      // outgoing.
      inTraffic = Utils::intStr (random () % 100, 10, 2);
      if (coreId.row () > 0)
	outTraffic = Utils::intStr (random () % 100, 10, 2);
      else
	outTraffic = "--";

      reply +=
	"    <column name=\"North In\">";
      reply += inTraffic;
      reply += "</column>\n"
	"    <column name=\"North Out\">";
      reply += outTraffic;
      reply += "</column>\n";

      inTraffic = Utils::intStr (random () % 100, 10, 2);
      if (coreId.row ()< maxRow)
	outTraffic = Utils::intStr (random () % 100, 10, 2);
      else
	outTraffic = "--";

      reply +=
	"    <column name=\"South In\">";
      reply += inTraffic;
      reply += "</column>\n"
	"    <column name=\"South Out\">";
      reply += outTraffic;
      reply += "</column>\n";

      inTraffic = Utils::intStr (random () % 100, 10, 2);
      if (coreId.col () < maxCol)
	outTraffic = Utils::intStr (random () % 100, 10, 2);
      else
	outTraffic = "--";

      reply +=
	"    <column name=\"East In\">";
      reply += inTraffic;
      reply += "</column>\n"
	"    <column name=\"East Out\">";
      reply += outTraffic;
      reply += "</column>\n";

      inTraffic = Utils::intStr (random () % 100, 10, 2);
      if (coreId.col () > 0)
	outTraffic = Utils::intStr (random () % 100, 10, 2);
      else
	outTraffic = "--";

      reply +=
	"    <column name=\"West In\">";
      reply += inTraffic;
      reply += "</column>\n"
	"    <column name=\"West Out\">";
      reply += outTraffic;
      reply += "</column>\n"
	"  </item>\n";
    }

  reply += "</osdata>";

  return reply;
}


//-----------------------------------------------------------------------------
//! Handle a RSP set request
//-----------------------------------------------------------------------------
void
GdbServer::rspSet ()
{
  if (strncmp ("QNonStop:0", pkt->data, strlen ("QNonStop:0")) == 0)
    {
      // Set all-stop mode
      mDebugMode = ALL_STOP;
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else if (strncmp ("QNonStop:1", pkt->data, strlen ("QNonStop:1")) == 0)
    {
      // Set non-stop mode
      mDebugMode = NON_STOP;
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
  mCurrentThread->writePc (0);

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
  GdbTid tid = GdbTid::fromString (&pkt->data[1]);

  if (tid.tid () <= 0)
    {
      cerr << "Warning: Can't request status for thread ID <= 0" << endl;
      pkt->packStr ("E02");
      rsp->putPkt (pkt);
      return;
    }

  // This will not find thread IDs 0 (any) or -1 (all), which seems to be what
  // we want.
  if (mCurrentProcess->hasThread (mThreads[tid.tid ()]))
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
      // Support this for multi-threading.  Note we have to report we
      // know about 's' and 'S', even though we don't support them
      // (GDB steps using software single-step).  If we don't, GDB
      // won't use vCont at all.
      pkt->packStr ("vCont;c;C;s;S;t");
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
  else if (0 == strcmp ("vStopped", pkt->data))
    {
      rspVStopped ();
    }
  else
    {
      rspUnknownPacket ();
    }
}				// rspVpkt()


//-----------------------------------------------------------------------------
//! Handle a vCont packet

//! Syntax is vCont[;action[:thread-id]]...
//-----------------------------------------------------------------------------
void
GdbServer::rspVCont ()
{
  ProcessInfo *process = mCurrentProcess;
  vContTidActionVector threadActions;

  stringstream    ss (pkt->data);
  vector <string> elements;		// To break out the packet elements
  string          item;

  // Break out the action/thread pairs
  while (getline (ss, item, ';'))
    elements.push_back (item);

  if (1 == elements.size ())
    {
      cerr << "Warning: No actions specified for vCont." << endl;
    }

  // Sort out the detail of each action

  for (size_t i = 1; i < elements.size (); i++)
    {
      vector <string> tokens;
      stringstream tss (elements [i]);

      // Break out the action and the thread
      while (getline (tss, item, ':'))
	tokens.push_back (item);

      if (1 == tokens.size ())
	{
	  // No thread ID, apply to all threads.
	  vContTidAction tid_action;

	  tid_action.tid = GdbTid::ALL_THREADS;
	  tid_action.kind = extractVContAction (tokens[0]);
	  threadActions.push_back (tid_action);
	}
      else if (2 == tokens.size ())
	{
	  vContTidAction tid_action;

	  tid_action.tid = GdbTid::fromString (tokens[1].c_str ());
	  tid_action.kind = extractVContAction (tokens[0]);
	  threadActions.push_back (tid_action);
	}
      else
	{
	  cerr << "Warning: Unrecognized vCont action of size "
	       << tokens.size () << ": " << elements[i] << endl;
	}
    }

  // We have a list of actions including which thread each applies to.
  // We may also have pending stops for previous actions.  So what we
  // need to do is:
  // 1. Continue any thread that was not already marked as stopped.
  // 2. Otherwise report the first stopped thread.

  // Continue any thread without a pendingStop to deal with.
  for (set <Thread*>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    {
      Thread* thread = *it;

      for (vContTidActionVector::const_iterator act_it = threadActions.begin ();
	   act_it != threadActions.end ();
	   ++act_it)
	{
	  const vContTidAction &action = *act_it;

	  if (action.matches (process->pid (), thread->tid ()))
	    {
	      if (action.kind == ACTION_CONTINUE
		  && thread->lastAction () == ACTION_STOP)
		{
		  thread->setLastAction (action.kind);
		  if (thread->pendingSignal () == TARGET_SIGNAL_NONE)
		    continueThread (thread);
		  break;
		}
	      else if (action.kind == ACTION_STOP
		  && thread->lastAction () == ACTION_CONTINUE)
		{
		  thread->halt ();
		}
	    }
	}
    }

  if (mDebugMode == NON_STOP)
    {
      // Return immediately.
      pkt->packStr ("OK");
      rsp->putPkt (pkt);
    }
  else
    {
      // Wait and report stop statuses.
      waitAllThreads ();
    }
}	// rspVCont ()


//-----------------------------------------------------------------------------
//! Wait for a thread to stop, and handle it.
//-----------------------------------------------------------------------------

void
GdbServer::waitAllThreads ()
{
  ProcessInfo *process = mCurrentProcess;

  // Deal with any pending stops before looking for new ones.
  for (set <Thread*>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    {
      Thread* thread = *it;

      if (thread->lastAction () == ACTION_CONTINUE
	  && thread->pendingSignal () != TARGET_SIGNAL_NONE)
	{
	  doContinue (thread);
	  return;
	}
    }

  // We must wait until a thread halts.
  while (true)
    {
      // Check for Ctrl-C.  Prioritize it over thread stops,
      // otherwise, e.g., "next" over a loop never manages to react to
      // the Ctrl-C, because a thread always manages to finish a
      // single-step before we look for the Ctrl-C request.
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

      for (set <Thread*>::iterator it = process->threadBegin ();
	   it != process->threadEnd ();
	   it++)
	{
	  Thread* thread = *it;

	  if (thread->lastAction () == ACTION_CONTINUE && thread->isHalted ())
	    {
	      thread->setPendingSignal (findStopReason (thread));
	      doContinue (thread);
	      return;
	    }
	}

      Utils::microSleep (100000);	// Every 100ms
    }
}	// waitAllThreads ()


//-----------------------------------------------------------------------------
//! Extract a vCont action

//! We don't support 'C' for now, so if we find it, we print a rude
//! message and treat it as 'c'.  We also print a rude message and
//! return ACTION_STOP.

//! @param[in] action  The string with the action
//! @return the vContAction indicating the action kind
//-----------------------------------------------------------------------------
GdbServer::vContAction
GdbServer::extractVContAction (const string &action)
{
  char  a = action.c_str () [0];

  switch (a)
    {
    case 'C':
      cerr << "Warning: 'C' action not supported for vCont: treated as 'c'."
	   << endl;
      return ACTION_CONTINUE;

    case 'c':
      // All good
      return ACTION_CONTINUE;

    case 't':
      return ACTION_STOP;

    default:
      cerr << "Warning: Unrecognized vCont action '" << a
	   << "': treating as stop." << endl;
      return ACTION_STOP;
    }

}	// extractVContAction ()


//-----------------------------------------------------------------------------
//! Mark any pending stops

//! At this point all threads should have been halted anyway. So we are
//! looking to see if a thread has halted at a breakpoint.

//! @todo  This assumes that in all-stop mode, all threads are restarted on
//!        vCont. Will need refinement for non-stop mode.

//! @param[in] reporting_thread      The thread we're reporting a stop for.
//-----------------------------------------------------------------------------
void
GdbServer::markPendingStops (Thread *reporting_thread)
{
  ProcessInfo* process = mCurrentProcess;

  for (set <Thread*>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    {
      Thread* thread = *it;

      if (thread != reporting_thread
	  && thread->lastAction () == ACTION_CONTINUE
	  && thread->pendingSignal () == TARGET_SIGNAL_NONE)
	{
	  assert (thread->isHalted ());

	  TargetSignal sig = findStopReason (thread);
	  if (sig != TARGET_SIGNAL_NONE)
	    {
	      thread->setPendingSignal (sig);

	      if (si->debugStopResume ())
		cerr << "DebugStopResume: marking " << thread->tid () << " pending."
		     << endl;
	    }
	  else
	    {
	      if (si->debugStopResume ())
		cerr << "DebugStopResume: " << thread->tid () << " NOT pending."
		     << endl;
	    }
	}
    }

  reporting_thread->setPendingSignal (TARGET_SIGNAL_NONE);

}	// markPendingStops ()


//-----------------------------------------------------------------------------
//! Mark all threads as stopped from the client's perspective.
//-----------------------------------------------------------------------------
void
GdbServer::markAllStopped ()
{
  ProcessInfo* process = mCurrentProcess;

  for (set <Thread*>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    {
      Thread* thread = *it;

      thread->setLastAction (ACTION_STOP);
    }

}	// markPendingStops ()


//-----------------------------------------------------------------------------
//! Continue execution of a thread

//! This is only half of continue processing - we'll worry about the thread
//! stopping later.

//! @param[in] thread  The thread to continue.
//-----------------------------------------------------------------------------
void
GdbServer::continueThread (Thread* thread)
{
  if (si->debugStopResume ())
    cerr << "DebugStopResume: continueThread (" << thread->tid () << ")."
	 << endl;

  thread->resume ();
}	// continueThread ()


//-----------------------------------------------------------------------------
//! Deal with a stopped thread after continue

//! @param[in] tid  The thread ID which stopped.
//-----------------------------------------------------------------------------
void
GdbServer::doContinue (Thread* thread)
{
  if (si->debugStopResume ())
    cerr << "DebugStopResume: doContinue (" << thread->tid () << ")." << endl;

  assert (thread->isHalted ());

  TargetSignal sig = thread->pendingSignal ();

  thread->setPendingSignal (TARGET_SIGNAL_NONE);

  // If it was a syscall, then do the relevant F packet return.
  if (sig == TARGET_SIGNAL_EMT)
    {
      uint16_t instr16 = getStopInstr (thread);

      haltAllThreads ();
      redirectStdioOnTrap (thread, getTrap (instr16));
      return;
    }

  rspReportException (thread, sig);

  // At this point, no thread should be resumed until the client
  // tells us to.
  markAllStopped ();
}	// doContinue ()

//-----------------------------------------------------------------------------
//! Why did we stop?

//! We know the thread is halted. Did it halt immediately after a
//! BKPT, TRAP or IDLE instruction.  Not quite as easy as it seems,
//! since TRAP may often be followed by NOP.  These are the signals
//! returned under various circumstances.

//!   - BKPT instruction. Return TARGET_SIGNAL_TRAP.

//!   - Software exception. Depends on the EXCAUSE field in the STATUS
//!     register

//!   - TRAP instruction. The result depends on the trap value.

//!       - 0-2 or 6.  Reserved, so should not be used.  Return
//!         TARGET_SIGNAL_SYS.

//!       - 3. Program exit.  Return TARGET_SIGNAL_QUIT.

//!       - 4. Success.  Return TARGET_SIGNAL_USR1.

//!       - 5. Failure.  Return TARGET_SIGNAL_USR2.

//!       - 7. System call.  If the value in R3 is valid, return
//!         TARGET_SIGNAL_EMT, otherwise return TARGET_SIGNAL_SYS.

//!   - Anything else.  We must have been stopped externally, so
//!     return TARGET_SIGNAL_NONE.

//! @param[in] thread  The thread to consider.
//! @return  The appropriate signal as described above.
//-----------------------------------------------------------------------------
GdbServer::TargetSignal
GdbServer::findStopReason (Thread *thread)
{
  assert (thread->isHalted ());
  assert (thread->pendingSignal () == TARGET_SIGNAL_NONE);

  // First see if we just hit a breakpoint or IDLE. Fortunately IDLE, BREAK,
  // TRAP and NOP are all 16-bit instructions.
  uint32_t pc = thread->readPc () - SHORT_INSTRLEN;
  uint16_t instr16 = thread->readMem16 (pc);

  if (instr16 == BKPT_INSTR)
    {
      // Decrement PC ourselves -- we support the "T05 swbreak" stop reason.
      thread->writePc (pc);
      return TARGET_SIGNAL_TRAP;
    }

  TargetSignal sig = thread->getException ();

  if (sig != TARGET_SIGNAL_NONE)
    return sig;

  // Is it a TRAP? Find the first preceding non-NOP instruction
  while (instr16 == NOP_INSTR)
    {
      pc -= SHORT_INSTRLEN;
      instr16 = thread->readMem16 (pc);
    }

  if (getOpcode10 (instr16) == TRAP_INSTR)
    {
      switch (getTrap (instr16))
	{
	case TRAP_WRITE:
	case TRAP_READ:
	case TRAP_OPEN:
	case TRAP_CLOSE:
	  return TARGET_SIGNAL_EMT;

	case TRAP_EXIT:
	  return TARGET_SIGNAL_QUIT;

	case TRAP_PASS:
	  return TARGET_SIGNAL_USR1;

	case TRAP_FAIL:
	  return TARGET_SIGNAL_USR2;

	case TRAP_SYSCALL:
	  // Look at R3 to see if the value is valid.
	  switch (thread->readReg (R0_REGNUM + 3))
	    {
	    case SYS_open:
	    case SYS_close:
	    case SYS_read:
	    case SYS_write:
	    case SYS_lseek:
	    case SYS_unlink:
	    case SYS_fstat:
	    case SYS_stat:
	      // Valid system calls
	      return TARGET_SIGNAL_EMT;

	    default:
	      // Invalid system call
	      return TARGET_SIGNAL_SYS;
	    }

	default:
	  // Undefined, so bad system call.
	  return TARGET_SIGNAL_SYS;
	}
    }

  // Must have been stopped externally
  return TARGET_SIGNAL_NONE;

}	// findStopReason ()


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
  Thread* thread = mCurrentThread;

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

  unhideBreakpoints (thread, addr, bindat, len);

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
  type = (MpType) (pkt->data[1] - '0');
  if (type != BP_MEMORY)
    {
      rspUnknownPacket ();
      return;
    }
  if (2 != sscanf (pkt->data + 2, ",%x,%1ud", &addr, &len))
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
	  ProcessInfo *process = mCurrentProcess;

	  for (set <Thread*>::iterator it = process->threadBegin ();
	       it != process->threadEnd ();
	       it++)
	    {
	      Thread* thread = *it;

	      if (mpHash->remove (type, addr, thread, &instr))
		thread->writeMem16 (addr, instr);
	    }
	}
      else
	{
	  // Shared memory we only need to remove once.
	  Thread* thread = mCurrentThread;

	  if (mpHash->remove (type, addr, thread, &instr))
	    thread->writeMem16 (addr, instr);
	}

      pkt->packStr ("OK");
      rsp->putPkt (pkt);
      return;

    default:
      assert ("unhandled matchpoint type" && 0);
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
  type = (MpType) (pkt->data[1] - '0');
  if (type != BP_MEMORY)
    {
      rspUnknownPacket ();
      return;
    }
  if (2 != sscanf (pkt->data + 2, ",%x,%1ud", &addr, &len))
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
	  ProcessInfo *process = mCurrentProcess;

	  for (set <Thread*>::iterator it = process->threadBegin ();
	       it != process->threadEnd ();
	       it++)
	    {
	      Thread* thread = *it;

	      thread->readMem16 (addr, bpMemVal);
	      mpHash->add (type, addr, thread, bpMemVal);
	      thread->insertBkptInstr (addr);
	    }
	}
      else
	{
	  // Shared memory we only need to insert once.
	  Thread* thread = mCurrentThread;

	  thread->readMem16 (addr, bpMemVal);
	  mpHash->add (type, addr, thread, bpMemVal);

	  thread->insertBkptInstr (addr);
	}


      pkt->packStr ("OK");
      rsp->putPkt (pkt);
      return;

    default:
      assert ("unhandled matchpoint type" && 0);
      return;
    }
}	// rspInsertMatchpoint()


//---------------------------------------------------------------------------*/
//! Align down PTR to ALIGN alignment.
//---------------------------------------------------------------------------*/

static inline uint32_t
alignDown (uint32_t ptr, size_t align)
{
  return ptr & -align;
}	// alignDown()


//---------------------------------------------------------------------------*/
//! Helper for hideBreakpoints/unhideBreakpoints.

//! Copies instruction INSN at BP_ADDR to the appropriate offset into
//! MEM_BUF, which is a memory buffer containing LEN bytes of target
//! memory starting at MEM_ADDR.  On output, if not NULL,
//! REPLACED_INSN contains the original contents of the buffer region
//! that was replaced by INSN.

//---------------------------------------------------------------------------*/
static void
copyInsn (uint32_t mem_addr, uint8_t* mem_buf, size_t len,
	  uint32_t bp_addr, const unsigned char *insn,
	  unsigned char* replaced_insn)
{
  const unsigned int bp_size = 2;
  uint32_t mem_end = mem_addr + len;
  uint32_t bp_end = bp_addr + bp_size;
  uint32_t copy_start, copy_end;
  int copy_offset, copy_len, buf_offset;

  copy_start = std::max (bp_addr, mem_addr);
  copy_end = std::min (bp_end, mem_end);

  copy_len = copy_end - copy_start;
  copy_offset = copy_start - bp_addr;
  buf_offset = copy_start - mem_addr;

  if (replaced_insn != NULL)
    memcpy (replaced_insn + copy_offset, mem_buf + buf_offset, copy_len);
  memcpy (mem_buf + buf_offset, insn + copy_offset, copy_len);
}	// copyInsn()


//-----------------------------------------------------------------------------
//! Hide breakpoint instructions from GDB

//! Replaces any breakpoint instruction found in the memory block of
//! LEN bytes starting at MEM_ADDR with the original instruction the
//! breakpoint replaced.  MEM_BUF contains a copy of the raw memory
//! from the target.
//-----------------------------------------------------------------------------
bool
GdbServer::hideBreakpoints (Thread *thread,
			    uint32_t mem_addr, uint8_t* mem_buf, size_t len)
{
  const unsigned int bp_size = 2;
  uint32_t mem_end = mem_addr + len;
  uint32_t check_addr = alignDown (mem_addr, bp_size);

  for (; check_addr < mem_end; check_addr += bp_size)
    {
      uint16_t orig_insn;

      if (mpHash->lookup (BP_MEMORY, check_addr, mCurrentThread, &orig_insn))
	{
	  unsigned char* shadow = (unsigned char *) &orig_insn;

	  copyInsn (mem_addr, mem_buf, len, check_addr, shadow, NULL);
	}
    }
}	// hideBreakpoints ()


//-----------------------------------------------------------------------------
//! Put back breakpoint instructions in the memory buffer we're about
//! to write to target memory.

//! MEM_BUF contains a copy of the raw memory from the target.
//-----------------------------------------------------------------------------
bool
GdbServer::unhideBreakpoints (Thread *thread,
			      uint32_t mem_addr, uint8_t* mem_buf, size_t len)
{
  const unsigned int bp_size = 2;
  uint32_t mem_end = mem_addr + len;
  uint32_t bp_addr = alignDown (mem_addr, bp_size);

  for (; bp_addr < mem_end; bp_addr += bp_size)
    {
      uint16_t orig_insn;

      if (mpHash->lookup (BP_MEMORY, bp_addr, thread, &orig_insn))
	{
	  uint16_t bkpt_instr = BKPT_INSTR;
	  unsigned char* bp_insn = (unsigned char *) &bkpt_instr;

	  copyInsn (mem_addr, mem_buf, len,
		    bp_addr, bp_insn, (unsigned char *) &orig_insn);

	  // Reinsert the breakpoint in the hash in order to replace
	  // the original shadow instruction.
	  if (fTargetControl->isLocalAddr (bp_addr))
	    {
	      ProcessInfo *process = mCurrentProcess;
	      for (set <Thread *>::iterator it = process->threadBegin ();
		   it != process->threadEnd ();
		   it++)
		{
		  Thread *thread = *it;

		  mpHash->add (BP_MEMORY, bp_addr, thread, orig_insn);
		}
	    }
	  else
	    {
	      mpHash->add (BP_MEMORY, bp_addr, thread, orig_insn);
	    }
	}
    }
}	// unhideBreakpoints ()


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
  Thread* thread = mCurrentThread;

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
//! Get a thread from a thread ID

//! Should only be called with a known good thread ID.

//! @param[in] tid  Thread ID
//! @return  Thread
//-----------------------------------------------------------------------------
Thread*
GdbServer::getThread (int tid)
{
  map <int, Thread*>::iterator it = mThreads.find (tid);

  assert (it != mThreads.end ());
  return it->second;

}	// getThread ()


//-----------------------------------------------------------------------------
//! Halt all threads in the current process.

//! @return  TRUE if all threads halt, FALSE otherwise
//-----------------------------------------------------------------------------
bool
GdbServer::haltAllThreads ()
{
  ProcessInfo *process = mCurrentProcess;
  bool allHalted = true;

  for (set <Thread*>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    {
      Thread* thread = *it;
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
  ProcessInfo *process = mCurrentProcess;
  bool allResumed = true;

  for (set <Thread*>::iterator it = process->threadBegin ();
       it != process->threadEnd ();
       it++)
    {
      Thread* thread = *it;

      if (thread->lastAction () == ACTION_CONTINUE
	  && thread->pendingSignal () == TARGET_SIGNAL_NONE)
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
