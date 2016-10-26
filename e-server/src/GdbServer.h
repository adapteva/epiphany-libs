// GDB RSP server class: Declaration.

// Copyright (C) 2008, 2009, 2014 Embecosm Limited
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

//-----------------------------------------------------------------------------
// This RSP server for the Adapteva Epiphany was written by Jeremy Bennett on
// behalf of Adapteva Inc.

// Implementation is based on the Embecosm Application Note 4 "Howto: GDB
// Remote Serial Protocol: Writing a RSP Server"
// (http://www.embecosm.com/download/ean4.html).

// Note that the Epiphany is a little endian architecture.

// Commenting is Doxygen compatible.

#ifndef GDB_SERVER__H
#define GDB_SERVER__H

#include <string>
#include <map>

//! @todo We would prefer to use <cstdint> here, but that requires ISO C++ 2011.
#include <inttypes.h>

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "CoreId.h"
#include "MpHash.h"
#include "ProcessInfo.h"
#include "RspConnection.h"
#include "RspPacket.h"
#include "ServerInfo.h"
#include "TargetControl.h"
#include "GdbTid.h"


using std::string;
using std::map;
using std::vector;


class Thread;

//-----------------------------------------------------------------------------
//! Class implementing a GDB RSP server.

//! We listen for RSP requests, which are converted to requests to read and
//! write registers or memory or control the CPU in the debug unit
//-----------------------------------------------------------------------------
class GdbServer
{
public:

  //! Definition of GDB target signals. Data taken from the GDBsource. Only
  //! those we use defined here.
  enum TargetSignal
  {
    // Used some places (e.g. stop_signal) to record the concept that there is
    // no signal.
    TARGET_SIGNAL_NONE = 0,
    TARGET_SIGNAL_FIRST = 0,
    TARGET_SIGNAL_HUP = 1,
    TARGET_SIGNAL_INT = 2,
    TARGET_SIGNAL_QUIT = 3,
    TARGET_SIGNAL_ILL = 4,
    // Note: is it currently assumed that this maps to BKPT /
    // breakpoint, and nothing else.
    TARGET_SIGNAL_TRAP = 5,
    TARGET_SIGNAL_ABRT = 6,
    TARGET_SIGNAL_EMT = 7,
    TARGET_SIGNAL_FPE = 8,
    TARGET_SIGNAL_KILL = 9,
    TARGET_SIGNAL_BUS = 10,
    TARGET_SIGNAL_SEGV = 11,
    TARGET_SIGNAL_SYS = 12,
    TARGET_SIGNAL_PIPE = 13,
    TARGET_SIGNAL_ALRM = 14,
    TARGET_SIGNAL_TERM = 15,
    TARGET_SIGNAL_USR1 = 30,
    TARGET_SIGNAL_USR2 = 31,
  };

  enum vContAction
  {
    ACTION_STOP,
    ACTION_CONTINUE,
  };

  // Public architectural constants. Must be consistent with the target
  // hardware.
  static const unsigned int NUM_GPRS = 64;
  static const unsigned int NUM_SCRS = 42;
  static const unsigned int NUM_REGS = NUM_GPRS + NUM_SCRS;

  // Specific GDB register numbers - GPRs
  static const unsigned int R0_REGNUM = 0;
  static const unsigned int RV_REGNUM = 0;
  static const unsigned int SB_REGNUM = 9;
  static const unsigned int SL_REGNUM = 10;
  static const unsigned int FP_REGNUM = 11;
  static const unsigned int IP_REGNUM = 12;
  static const unsigned int SP_REGNUM = 13;
  static const unsigned int LR_REGNUM = 14;

  // Specific GDB register numbers - SCRs
  static const unsigned int CONFIG_REGNUM = NUM_GPRS;
  static const unsigned int STATUS_REGNUM = NUM_GPRS + 1;
  static const unsigned int PC_REGNUM = NUM_GPRS + 2;
  static const unsigned int DEBUGSTATUS_REGNUM = NUM_GPRS + 3;
  static const unsigned int IRET_REGNUM = NUM_GPRS + 7;
  static const unsigned int IMASK_REGNUM = NUM_GPRS + 8;
  static const unsigned int ILAT_REGNUM = NUM_GPRS + 9;
  static const unsigned int FSTATUS_REGNUM = NUM_GPRS + 13;
  static const unsigned int DEBUGCMD_REGNUM = NUM_GPRS + 14;
  static const unsigned int RESETCORE_REGNUM = NUM_GPRS + 15;
  static const unsigned int COREID_REGNUM = NUM_GPRS + 37;

  // 16-bit instruction fields for Epiphany (i.e. LS bytes in instruction)
  static const uint16_t NOP_INSTR  = 0x01a2;	//!< NOP instruction
  static const uint16_t IDLE_INSTR = 0x01b2;	//!< IDLE instruction
  static const uint16_t BKPT_INSTR = 0x01c2;	//!< BKPT instruction
  static const uint16_t TRAP_INSTR = 0x03e2;	//!< TRAP instruction

  //! Size of a 16-bit instruction in bytes
  static const size_t SHORT_INSTRLEN = 2;

  //! Size of a 32-bit instruction in bytes
  static const size_t LONG_INSTRLEN = 2;

  // Constructor and destructor
  GdbServer (ServerInfo* _si);
  ~GdbServer ();

  //! main loop for core
  void rspServer (TargetControl* TargetControl);


private:

  // A tuple representing a vCont action and the TID it applies to.
  struct vContTidAction
  {
    // The thread the action applies to.
    GdbTid tid;

    // The action.
    vContAction kind;

    // True if this action applies to thread TID.
    bool matches (int pid, int tid) const
    {
      if (this->tid.pid () == -1)
	return 1;
      else if (pid == this->tid.pid ())
	{
	  if (this->tid.tid () == -1)
	    return 1;
	  else if (tid == this->tid.tid ())
	    return 1;
	}
      return 0;
    }
  };

  typedef vector <vContTidAction> vContTidActionVector;

  //! Maximum size of RSP packet. Enough for all the registers as hex
  //! characters (8 per reg) + 1 byte end marker.
  static const int RSP_PKT_MAX = NUM_REGS * TargetControl::E_REG_BYTES * 2 + 1;

  //! Number of the idle process
  static const int IDLE_PID = 1;

  //! Our debug mode
  enum {
    NON_STOP,
    ALL_STOP
  } mDebugMode;

  //! Whether GDB is connected in extended-remote mode.
  bool mExtendedMode;

  //! Map of process ID to process info
  typedef map <int, ProcessInfo *> PidProcessInfoMap;

  // All processes in the system.
  PidProcessInfoMap mProcesses;

  // All processes we're attached to.
  PidProcessInfoMap mAttachedProcesses;

  //! The idle process
  ProcessInfo * mIdleProcess;

  //! Next process ID to use
  int  mNextPid;

  //! Map of thread ID to thread
  typedef map <int, Thread *> TidThreadMap;
  TidThreadMap mThreads;

  //! Map from core ID to thread ID
  map <CoreId, int> mCore2Tid;

  //! Current thread ID for general access.  Null means "all threads"
  //! (the equivalent of GDB's -1).  We have no equivalent of GDB's 0,
  //! meaning "any thread", since we immediately just pick a thread.
  Thread* mCurrentThread;

  //! Indicate if we are in the middle of a notification sequence.
  bool mNotifyingP;

  //! Local pointer to server info
  ServerInfo *si;

  //! Responsible for the memory operation commands in target
  TargetControl * fTargetControl;

  //! Our associated RSP interface (which we create)
  RspConnection *rsp;

  //! The packet pointer. There is only ever one packet in use at one time, so
  //! there is no need to repeatedly allocate and delete it.
  RspPacket *pkt;

  //! Hash table for matchpoints
  MpHash *mpHash;

  //! String for OS info
  string  osInfoReply;

  //! String for OS processes
  string  osProcessReply;

  //! String for OS core load
  string  osLoadReply;

  //! String for OS mesh traffic
  string  osTrafficReply;

  //! String for qXfer:threads
  string qXferThreadsReply;

  // Helper functions for setting up a connection
  void initProcesses ();
  bool haltAndActivateProcess (ProcessInfo *process);
  void haltAndActivateAllThreads ();
  void rspAttach ();
  void rspDetach ();

  // Main RSP request handler
  void rspClientRequest ();

  void detachProcess (ProcessInfo* process);
  void detachAllProcesses ();

  // Handle the various RSP requests
  void rspStatus ();
  string rspPrepareStopReply (Thread *thread, TargetSignal sig);
  void rspReportException (Thread* thread, TargetSignal sig);
  void rspReadAllRegs ();
  void rspWriteAllRegs ();
  void rspUnknownPacket ();
  void rspSetThread ();
  void rspReadMem ();
  void rspReadReg ();
  void rspWriteReg ();
  void rspQuery ();
  string rspThreadExtraInfo (Thread* thread);
  void rspCommand ();
  void rspCmdWorkgroup (char* cmd);

  void rspTransfer ();
  typedef string (GdbServer::* makeTransferReplyFtype) (void);
  void rspTransferObject (const char *object,
			  string *reply,
			  makeTransferReplyFtype maker,
			  unsigned int offset,
			  unsigned int length);
  string rspMakeTransferThreadsReply ();
  string rspMakeOsDataReply ();
  string rspMakeOsDataProcessesReply ();
  string rspMakeOsDataLoadReply ();
  string rspMakeOsDataTrafficReply ();

  void rspSet ();
  void rspRestart ();
  void rspIsThreadAlive ();
  void rspVpkt ();
  void rspVCont ();
  vContAction extractVContAction (const string &action);
  void waitAllThreads ();
  bool pendingStop (int  tid);
  void markPendingStops (Thread *reporting_thread);
  void setLastActionAllThreads (vContAction action);
  void continueThread (Thread* thread);
  void doContinue (Thread* thread);
  Thread* findStoppedThread ();
  void rspClientNotifications ();
  void rspVStopped ();
  TargetSignal findStopReason (Thread *thread);
  uint16_t  getStopInstr (Thread* thread);
  void rspWriteMemBin ();
  void rspRemoveMatchpoint ();
  void rspInsertMatchpoint ();
  void rspFileIOreply ();
  void rspSuspend ();

  bool hideBreakpoints (Thread *thread,
			uint32_t addr, uint8_t* buf, size_t len);
  bool unhideBreakpoints (Thread *thread,
			  uint32_t addr, uint8_t* buf, size_t len);

  // Convenience functions to control and report on the CPU
  void targetSwReset ();
  void targetHWReset ();

  // Accessors for processes and threads
  ProcessInfo* findAttachedProcess (int pid);
  ProcessInfo * getProcess (int  pid);
  Thread* getThread (int tid);
  Thread* firstThread ();

  //! Thread control
  bool haltProcessThreads (ProcessInfo* process);
  bool haltAllThreads ();
  bool resumeAllProcessThreads (ProcessInfo* process);
  bool resumeAllThreads ();

  void redirectStdioOnTrap (Thread*  thread,
			    uint8_t  trap);
  void hostWrite (const char* intro,
		  uint32_t    chan,
		  uint32_t    addr,
		  uint32_t    len);
  bool  is32BitsInstr (uint32_t iab_instr);

  //! Wrapper to avoid external memory problems.
  void printfWrapper (char *result_str, const char *fmt, const char *args_buf);

  //! Extraction opcode fields.
  uint32_t  getOpcode1_4 (uint32_t  instr);
  uint32_t  getOpcode1_5 (uint32_t  instr);
  uint16_t  getOpcode2_4 (uint16_t  instr);
  uint32_t  getOpcode2_4 (uint32_t  instr);
  uint16_t  getOpcode4 (uint16_t  instr);
  uint32_t  getOpcode4 (uint32_t  instr);
  uint32_t  getOpcode4_2_4 (uint32_t  instr);
  uint32_t  getOpcode4_5 (uint32_t  instr);
  uint32_t  getOpcode4_7 (uint32_t  instr);
  uint32_t  getOpcode4_10 (uint32_t  instr);
  uint16_t  getOpcode5 (uint16_t  instr);
  uint32_t  getOpcode5 (uint32_t  instr);
  uint16_t  getOpcode7 (uint16_t  instr);
  uint32_t  getOpcode7 (uint32_t  instr);
  uint16_t  getOpcode10 (uint16_t  instr);
  uint32_t  getOpcode10 (uint32_t  instr);
  uint8_t  getRd (uint16_t  instr);
  uint8_t  getRd (uint32_t  instr);
  uint8_t  getRm (uint16_t  instr);
  uint8_t  getRm (uint32_t  instr);
  uint8_t  getRn (uint16_t  instr);
  uint8_t  getRn (uint32_t  instr);
  uint8_t  getTrap (uint16_t  instr);
  int32_t  getBranchOffset (uint16_t  instr);
  int32_t  getBranchOffset (uint32_t  instr);
  bool  getJump (Thread*   thread,
		 uint16_t  instr,
		 uint32_t  addr,
		 uint32_t& destAddr);
  bool  getJump (Thread*   thread,
		 uint32_t  instr,
		 uint32_t  addr,
		 uint32_t& destAddr);

  // Debugging support.
  void  doBacktrace ();

  // YS - provide the SystemC equivalent to the bit range selection operator.
  uint8_t getfield (uint8_t x, int _lt, int _rt);
  uint16_t getfield (uint16_t x, int _lt, int _rt);
  uint32_t getfield (uint32_t x, int _lt, int _rt);
  uint64_t getfield (uint64_t x, int _lt, int _rt);
  void setfield (uint32_t & x, int _lt, int _rt, uint32_t val);


};				// GdbServer()

#endif // GDB_SERVER__H


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// show-trailing-whitespace: t
// End:
