// GDB RSP server class: Declaration.

// Copyright (C) 2008, 2009, 2014 Embecosm Limited
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


using std::string;
using std::map;


//! Class implementing a GDB RSP server.

//! A thread listens for RSP requests, which are converted to requests to read
//! and write registers or memory or control the CPU in the debug unit
class GdbServer
{
public:

  // Constructor and destructor
  GdbServer (ServerInfo* _si);
  ~GdbServer ();

  //! main loop for core
  void rspServer (TargetControl* TargetControl);


private:

  // Public architectural constants. Must be consistent with the target
  // hardware.
  static const unsigned int NUM_GPRS = 64;
  static const unsigned int NUM_SCRS = 42;
  static const unsigned int NUM_REGS = NUM_GPRS + NUM_SCRS;

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
  };


  enum SPECIAL_INSTR_OPCODES
  {
  };

  enum EPIPHANY_EX_CASE
  {
    E_UNALIGMENT_LS = 0x2,
    E_FPU = 0x3,
    E_UNIMPL = 0x4
  };

  //! Maximum size of RSP packet. Enough for all the registers as hex
  //! characters (8 per reg) + 1 byte end marker.
  static const int RSP_PKT_MAX = NUM_REGS * TargetControl::E_REG_BYTES * 2 + 1;

  //! Number of entries in IVT table
  static const uint32_t IVT_ENTRIES = 10;

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

  //! GDB register nu

  // 16-bit instruction fields for Epiphany (i.e. LS bytes in instruction)
  static const uint16_t NOP_INSTR  = 0x01a2;	//!< NOP instruction
  static const uint16_t IDLE_INSTR = 0x01b2;	//!< IDLE instruction
  static const uint16_t BKPT_INSTR = 0x01c2;	//!< BKPT instruction
  static const uint16_t TRAP_INSTR = 0x03e2;	//!< TRAP instruction

  //! Size of the breakpoint instruction (in bytes)
  static const size_t BKPT_INSTLEN = 2;

  //! Size of the trap instruction (in bytes)
  static const size_t TRAP_INSTLEN = 2;

  //! Number of the idle process
  static const int IDLE_PID = 1;

  //! Set of processes
  set <ProcessInfo *> mProcesses;

  //! The idle process
  ProcessInfo * mIdleProcess;

  //! Next process ID to use
  int  mNextPid;

  //! Map from core to thread
  map <CoreId, int> core2thread;

  //! Map from thread to core
  map <int, CoreId> thread2core;

  //! Current thread for continue/step
  int  currentCThread;
  int  currentGThread;

  //! Local pointer to server info
  ServerInfo *si;

  //! Responsible for the memory operation commands in target
  TargetControl * fTargetControl;

  //! Used in cont command to support CTRL-C from gdb client
  bool fIsTargetRunning;

  //! Our associated RSP interface (which we create)
  RspConnection *rsp;

  //! The packet pointer. There is only ever one packet in use at one time, so
  //! there is no need to repeatedly allocate and delete it.
  RspPacket *pkt;

  //IVT save buffer
  uint8_t fIVTSaveBuff[IVT_ENTRIES * TargetControl::E_INSTR_BYTES];

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

  // Helper functions for setting up a connection
  void initProcesses ();
  void rspAttach ();
  void rspDetach ();

  // Main RSP request handler
  void rspClientRequest ();

  // Handle the various RSP requests
  void rspReportException (uint32_t stoppedPC, int threadId,
			   TargetSignal exCause);
  void rspContinue ();
  void rspContinue (uint32_t except);
  void rspContinue (uint32_t addr, uint32_t except);
  void rspReadAllRegs ();
  void rspWriteAllRegs ();
  void rspSetThread ();
  void rspReadMem ();
  void rspWriteMem ();
  void rspReadReg ();
  void rspWriteReg ();
  void rspQuery ();
  void rspQThreadInfo (bool isFirst);
  void rspQThreadExtraInfo ();
  void rspCommand ();
  void rspTransfer ();
  void rspOsData (unsigned int offset,
		  unsigned int length);
  void rspOsDataProcesses (unsigned int offset,
			   unsigned int length);
  void rspOsDataLoad (unsigned int offset,
		      unsigned int length);
  void rspOsDataTraffic (unsigned int offset,
			 unsigned int length);
  void rspSet ();
  void rspRestart ();
  void rspStep ();
  void rspStep (bool haveAddrP, uint32_t addr, TargetSignal except);
  void rspIsThreadAlive ();
  void targetResume (CoreId coreId);
  void rspVpkt ();
  void rspWriteMemBin ();
  void rspRemoveMatchpoint ();
  void rspInsertMatchpoint ();
  void rspFileIOreply ();
  void rspSuspend ();

  // Convenience functions to control and report on the CPU
  void targetSwReset ();
  void targetHWReset ();

  // Main functions for reading and writing memory
  bool readMemBlock (CoreId  coreId,
		     uint32_t  addr,
		     uint8_t* buf,
		     size_t  len) const;
  bool writeMemBlock (CoreId  coreId,
		      uint32_t  addr,
		      uint8_t* buf,
		      size_t  len) const;
  bool  readMem32 (CoreId  coreId,
		   uint32_t  addr,
		   uint32_t& val) const;
  uint32_t  readMem32 (CoreId  coreId,
		       uint32_t  addr) const;
  bool  writeMem32 (CoreId  coreId,
		    uint32_t  addr,
		    uint32_t val) const;
  bool  readMem16 (CoreId  coreId,
		   uint32_t  addr,
		   uint16_t& val) const;
  uint16_t  readMem16 (CoreId  coreId,
		       uint32_t  addr) const;
  bool  writeMem16 (CoreId  coreId,
		    uint32_t  addr,
		    uint16_t val) const;
  bool  readMem8 (CoreId  coreId,
		  uint32_t  addr,
		  uint8_t& val) const;
  uint8_t  readMem8 (CoreId  coreId,
		     uint32_t  addr) const;
  bool  writeMem8 (CoreId  coreId,
		   uint32_t  addr,
		   uint8_t val) const;

  // Main functions for reading and writing registers
  bool readReg (CoreId  coreId,
		unsigned int regnum,
		uint32_t& regval) const;
  uint32_t  readReg (CoreId  coreId,
		     unsigned int regnum) const;
  bool writeReg (CoreId  coreId,
		 unsigned int regNum,
		 uint32_t value) const;

  // Convenience functions for reading and writing various common registers
  CoreId readCoreId (CoreId  coreId) const;
  uint32_t readStatus (CoreId  coreId) const;
  uint32_t readPc (CoreId  coreId) const;
  void writePc (CoreId  coreId,
		uint32_t addr);
  uint32_t readLr (CoreId  coreId) const;
  void writeLr (CoreId  coreId,
		uint32_t addr);
  uint32_t readFp (CoreId  coreId) const;
  void writeFp (CoreId  coreId,
		uint32_t addr);
  uint32_t readSp (CoreId  coreId) const;
  void writeSp (CoreId  coreId,
		uint32_t  addr);

  // Accessors for the core IDs of threads
  CoreId cCore ();
  CoreId gCore ();

  void insertBkptInstr (uint32_t addr);
  bool isHitInBreakPointInstruction (uint32_t bkptAddr);
  bool isCoreHalted (CoreId coreId);
  bool isCoreIdle (CoreId  coreId);
  bool isCoreGIntsEnabled (CoreId  coreId);
  TargetSignal getException (CoreId  coreId);
  bool targetHalt (CoreId  coreId);

  void saveIVT (CoreId coreId);
  void restoreIVT (CoreId coreId);

  //! Thread control
  void NanoSleepThread (unsigned long timeout);

  void redirectSdioOnTrap (uint8_t trapNumber);

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
  bool  getJump (CoreId  coreId,
		 uint16_t  instr,
		 uint32_t  addr,
		 uint32_t& destAddr);
  bool  getJump (CoreId  coreId,
		 uint32_t  instr,
		 uint32_t  addr,
		 uint32_t& destAddr);

  // YS - provide the SystemC equivalent to the bit range selection operator.
  uint8_t getfield (uint8_t x, int _lt, int _rt);
  uint16_t getfield (uint16_t x, int _lt, int _rt);
  uint32_t getfield (uint32_t x, int _lt, int _rt);
  uint64_t getfield (uint64_t x, int _lt, int _rt);
  void setfield (uint32_t & x, int _lt, int _rt, uint32_t val);

  //! Map GDB register number to hardware register memory address
  uint32_t regAddr (unsigned int  regnum) const;

  //! Integer to string conversion
  string  intStr (int  val,
		  int  base = 10,
		  int  width = 0) const;

};				// GdbServer()

#endif // GDB_SERVER__H


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
