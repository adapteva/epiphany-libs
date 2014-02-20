// GDB RSP server class: Declaration.

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

//-----------------------------------------------------------------------------
// This RSP server for the Adapteva ATDSP was written by Jeremy Bennett on
// behalf of Adapteva Inc.

// Implementation is based on the Embecosm Application Note 4 "Howto: GDB
// Remote Serial Protocol: Writing a RSP Server"
// (http://www.embecosm.com/download/ean4.html).

// Note that the ATDSP is a little endian architecture.

// Commenting is Doxygen compatible.

#ifndef GDB_SERVER__H
#define GDB_SERVER__H

#include <string>
#include <vector>

//! @todo We would prefer to use <cstdint> here, but that requires ISO C++ 2011.
#include <inttypes.h>

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "RspConnection.h"
#include "MpHash.h"
#include "RspPacket.h"

#include "ServerInfo.h"
#include "TargetControl.h"


using std::string;
using std::vector;


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
    IDLE_OPCODE = 0x1b2
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

  // Values for the Debug register. The value can be read to determine the
  // state, or written to force the state.
  enum AtdspDebugStates
  {
    ATDSP_DEBUG_RUN = 0,	//!< Is running/should run
    ATDSP_DEBUG_HALT = 1,	//!< Is halted/should halt
    ATDSP_DEBUG_EMUL_MODE_IN = 2,	//! Enter Emulation mode, can access to all registers
    ATDSP_DEBUG_EMUL_MODE_OUT = 3,	//! Leave Emulation mode
  };
  // Need to check the outstanding transaction status, the core is in the debug state if no pending transaction to external memory
  enum AtdspOutStandingTransationPendingState
  {
    ATDSP_OUT_TRAN_TRUE = 1,
    ATDSP_OUT_TRAN_FALSE = 0
  };


  //! Number entries in IVT table
  static const uint32_t ATDSP_NUM_ENTRIES_IN_IVT = 8;

  //! Location in core memory where the GPRs are mapped:
  static const uint32_t ATDSP_GPR_MEM_BASE = 0x000ff000;

  //! Location in core memory where the SCRs are mapped:
  static const uint32_t ATDSP_SCR_MEM_BASE = 0x000ff100;

  // Specific GDB register numbers - GPRs
  static const int R0_REGNUM = 0;
  static const int RV_REGNUM = 0;
  static const int SB_REGNUM = 9;
  static const int SL_REGNUM = 10;
  static const int FP_REGNUM = 11;
  static const int IP_REGNUM = 12;
  static const int SP_REGNUM = 13;
  static const int LR_REGNUM = 14;

  // Specific GDB register numbers - SCRs
  static const int CONFIG_REGNUM = NUM_GPRS;
  static const int STATUS_REGNUM = NUM_GPRS + 1;
  static const int PC_REGNUM = NUM_GPRS + 2;
  static const int DEBUGSTATUS_REGNUM = NUM_GPRS + 3;
  static const int IRET_REGNUM = NUM_GPRS + 7;
  static const int IMASK_REGNUM = NUM_GPRS + 8;
  static const int ILAT_REGNUM = NUM_GPRS + 9;
  static const int DEBUGCMD_REGNUM = NUM_GPRS + 14;
  static const int RESETCORE_REGNUM = NUM_GPRS + 15;
  static const int COREID_REGNUM = NUM_GPRS + 37;

  //! GDB register nu

  // Bits in the status register
  static const uint32_t ATDSP_SCR_STATUS_STALLED = 0x00000001;	//!< Stalled

  // 16-bit instructions for ATDSP
  static const uint16_t ATDSP_NOP_INSTR = 0x01a2;	//!< NOP instruction
//      static const uint16_t ATDSP_IDLE_INSTR = 0x01b2; //!< IDLE instruction
  static const uint16_t ATDSP_BKPT_INSTR = 0x01c2;	//!< BKPT instruction
  static const uint16_t ATDSP_RTI_INSTR = 0x01d2;	//!< RTI instruction
  static const uint16_t ATDSP_TRAP_INSTR = 0x03e2;	//!< TRAP instruction

  // Instruction lengths
  static const int ATDSP_INST16LEN = 2;	//!< Short instruction
  static const int ATDSP_INST32LEN = 4;	//!< Long instruction

  //! Size of the breakpoint instruction (in bytes)
  static const int ATDSP_BKPT_INSTLEN = ATDSP_INST16LEN;
  //! Size of the trap instruction (in bytes)
  static const int ATDSP_TRAP_INSTLEN = ATDSP_BKPT_INSTLEN;

  //! Interrupt vector bits
  static const uint32_t ATDSP_EXCEPT_RESET = 0x00000001;
  static const uint32_t ATDSP_EXCEPT_NMI = 0x00000002;
  static const uint32_t ATDSP_EXCEPT_FPE = 0x00000004;
  static const uint32_t ATDSP_EXCEPT_IRQH = 0x00000008;
  static const uint32_t ATDSP_EXCEPT_TIMER = 0x00000010;
  static const uint32_t ATDSP_EXCEPT_DMA = 0x00000020;
  static const uint32_t ATDSP_EXCEPT_IRQL = 0x00000040;
  static const uint32_t ATDSP_EXCEPT_SWI = 0x00000080;

  //! Interrupt vector locations
  static const uint32_t ATDSP_VECTOR_RESET = 0x00000000;
  static const uint32_t ATDSP_VECTOR_NMI = 0x00000004;
  static const uint32_t ATDSP_VECTOR_FPE = 0x00000008;
  static const uint32_t ATDSP_VECTOR_IRQH = 0x0000000c;
  static const uint32_t ATDSP_VECTOR_TIMER = 0x00000010;
  static const uint32_t ATDSP_VECTOR_DMA = 0x00000014;
  static const uint32_t ATDSP_VECTOR_IRQL = 0x00000018;
  static const uint32_t ATDSP_VECTOR_SWI = 0x0000001c;

  enum EDebugState
  { CORE_RUNNING, CORE_ON_DEBUG };

  //! Thread ID used by ATDSP
  static const int ATDSP_TID = 1;

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
  unsigned char fIVTSaveBuff[ATDSP_NUM_ENTRIES_IN_IVT * 4];

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

  // Main RSP request handler
  void rspClientRequest ();

  // Handle the various RSP requests
  void rspReportException (unsigned stoppedPC, unsigned threadID,
			   unsigned exCause);
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
  void rspStep (uint32_t except);
  void rspStep (uint32_t addr, uint32_t except);
  void targetResume ();
  void rspVpkt ();
  void rspWriteMemBin ();
  void rspRemoveMatchpoint ();
  void rspInsertMatchpoint ();

  void rspQThreadExtraInfo ();
  void rspThreadSubOperation ();
  void rspFileIOreply ();
  void rspSuspend ();

  void rspAttach ();
  void rspDetach ();

  // Convenience functions to control and report on the CPU
  void targetSwReset ();
  void targetHWReset ();

  // Main functions for reading and writing registers
  bool readReg (unsigned int regnum,
		uint32_t& regval) const;
  uint32_t  readReg (unsigned int regnum) const;
  bool writeReg (unsigned int regNum,
		 uint32_t value) const;

  // Convenience functions for reading and writing various common registers
  uint32_t readCoreId ();
  uint32_t readStatus ();

  uint32_t readPc ();
  void writePc (uint32_t addr);

  uint32_t readLr ();
  void writeLr (uint32_t addr);

  uint32_t readFp ();
  void writeFp (uint32_t addr);

  uint32_t readSp ();
  void writeSp (uint32_t addr);

  void putBreakPointInstruction (unsigned long);
  bool isHitInBreakPointInstruction (unsigned long);
  bool isTargetInDebugState ();
  bool isTargetIdle ();
  bool isTargetExceptionState (unsigned &);
  bool targetHalt ();

  void saveIVT ();
  void restoreIVT ();

  //! Thread control
  void NanoSleepThread (unsigned long timeout);

  void redirectSdioOnTrap (uint8_t trapNumber);

  bool  is32BitsInstr (uint32_t iab_instr);

  //! Wrapper to avoid external memory problems. 
  void printfWrapper (char *result_str, const char *fmt, const char *args_buf);

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

  //! CoreId to string conversion
  string  coreIdStr (uint16_t  coreId) const;

};				// GdbServer()

#endif // GDB_SERVER__H


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
