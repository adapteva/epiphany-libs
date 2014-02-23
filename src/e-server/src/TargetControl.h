// Abstract class for control of target: Declaration

// This file is part of the Epiphany Software Development Kit.

// Copyright (C) 2013-2014 Adapteva, Inc.

// Contributor: Oleg Raikhman <support@adapteva.com>
// Contributor: Yaniv Sapir <support@adapteva.com>
// Contributor: Jeremy Bennett <jeremy.bennett@embecosm.com>

// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.

// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.

// You should have received a copy of the GNU General Public License along
// with this program (see the file COPYING).  If not, see
// <http://www.gnu.org/licenses/>.

#ifndef TARGET_CONTROL__H
#define TARGET_CONTROL__H


#include <iostream>
#include <string>
#include <cstdio>
#include <vector>

//! @todo We would prefer to use <cstdint> here, but that requires ISO C++ 2011.
#include <inttypes.h>
#include <sys/time.h>


using std::cerr;
using std::endl;
using std::string;
using std::vector;


//! Abstract base class for an Epiphany target
class TargetControl
{
public:

  //! Maximum address space of an individual core
  static const uint32_t  CORE_MEM_SPACE = 0x00100000;

  // Epiphany sizes
  static const unsigned int E_BYTE_BYTES   = 1;
  static const unsigned int E_SHORT_BYTES  = 2;
  static const unsigned int E_WORD_BYTES   = 4;
  static const unsigned int E_DOUBLE_BYTES = 8;
  static const unsigned int E_REG_BYTES    = E_WORD_BYTES;
  static const unsigned int E_INSTR_BYTES  = E_WORD_BYTES;

  // Register constants - eCore registers
  static const uint32_t R0          = 0xf0000;
  static const uint32_t R63         = 0xf00fc;
  static const uint32_t CONFIG      = 0xf0400;
  static const uint32_t STATUS      = 0xf0404;
  static const uint32_t PC          = 0xf0408;
  static const uint32_t DEBUGSTATUS = 0xf040c;
  static const uint32_t LC          = 0xf0414;
  static const uint32_t LS          = 0xf0418;
  static const uint32_t LE          = 0xf041c;
  static const uint32_t IRET        = 0xf0420;
  static const uint32_t IMASK       = 0xf0424;
  static const uint32_t ILAT        = 0xf0428;
  static const uint32_t ILATST      = 0xf042c;
  static const uint32_t ILATCL      = 0xf0430;
  static const uint32_t IPEND       = 0xf0434;
  static const uint32_t FSTATUS     = 0xf0440;
  static const uint32_t DEBUGCMD    = 0xf0448;
  static const uint32_t RESETCORE   = 0xf070c;

  // Register constants - event timer registers
  static const uint32_t CTIMER0 = 0xf0438;
  static const uint32_t CTIMER1 = 0xf043c;

  // Register constants - processor control registers
  static const uint32_t MEMSTATUS  = 0xf0604;
  static const uint32_t MEMPROTECT = 0xf0608;

  // Register constants - DMA registers
  static const uint32_t DMA0CONFIG  = 0xf0500;
  static const uint32_t DMA0STRIDE  = 0xf0504;
  static const uint32_t DMA0COUNT   = 0xf0508;
  static const uint32_t DMA0SRCADDR = 0xf050c;
  static const uint32_t DMA0DSTADDR = 0xf0510;
  static const uint32_t DMA0AUTO0   = 0xf0514;
  static const uint32_t DMA0AUTO1   = 0xf0518;
  static const uint32_t DMA0STATUS  = 0xf051c;
  static const uint32_t DMA1CONFIG  = 0xf0520;
  static const uint32_t DMA1STRIDE  = 0xf0524;
  static const uint32_t DMA1COUNT   = 0xf0528;
  static const uint32_t DMA1SRCADDR = 0xf052c;
  static const uint32_t DMA1DSTADDR = 0xf0530;
  static const uint32_t DMA1AUTO0   = 0xf0534;
  static const uint32_t DMA1AUTO1   = 0xf0538;
  static const uint32_t DMA1STATUS  = 0xf053c;

  // Register constants - mesh node control registers
  static const uint32_t MESHCONFIG = 0xf0700;
  static const uint32_t COREID     = 0xf0704;
  static const uint32_t MULTICAST  = 0xf0708;
  static const uint32_t CMESHROUTE = 0xf0710;
  static const uint32_t XMESHROUTE = 0xf0714;
  static const uint32_t RMESHROUTE = 0xf0718;

  // Fields in registers. Generically _MASK is the mask for the field and
  // _SHIFT is the number of places to shift right to remove any less
  // significant bits. Any other suffices are particular field values.

  // STATUS register
  static const unsigned int STATUS_ACTIVE_SHIFT  = 0;
  static const unsigned int STATUS_GID_SHIFT     = 1;
  static const unsigned int STATUS_WAND_SHIFT    = 3;
  static const unsigned int STATUS_AZ_SHIFT      = 4;
  static const unsigned int STATUS_AN_SHIFT      = 5;
  static const unsigned int STATUS_AC_SHIFT      = 6;
  static const unsigned int STATUS_AV_SHIFT      = 7;
  static const unsigned int STATUS_BZ_SHIFT      = 8;
  static const unsigned int STATUS_BN_SHIFT      = 9;
  static const unsigned int STATUS_BV_SHIFT      = 10;
  static const unsigned int STATUS_AVS_SHIFT     = 12;
  static const unsigned int STATUS_BIS_SHIFT     = 13;
  static const unsigned int STATUS_BVS_SHIFT     = 14;
  static const unsigned int STATUS_BUS_SHIFT     = 15;
  static const unsigned int STATUS_EXCAUSE_SHIFT = 16;

  static const uint32_t STATUS_ACTIVE_MASK   = 0x00000001;
  static const uint32_t STATUS_GID_MASK      = 0x00000002;
  static const uint32_t STATUS_WAND_MASK     = 0x00000008;
  static const uint32_t STATUS_AZ_MASK       = 0x00000010;
  static const uint32_t STATUS_AN_MASK       = 0x00000020;
  static const uint32_t STATUS_AC_MASK       = 0x00000040;
  static const uint32_t STATUS_AV_MASK       = 0x00000080;
  static const uint32_t STATUS_BZ_MASK       = 0x00000100;
  static const uint32_t STATUS_BN_MASK       = 0x00000200;
  static const uint32_t STATUS_BV_MASK       = 0x00000400;
  static const uint32_t STATUS_AVS_MASK      = 0x00001000;
  static const uint32_t STATUS_BIS_MASK      = 0x00002000;
  static const uint32_t STATUS_BVS_MASK      = 0x00004000;
  static const uint32_t STATUS_BUS_MASK      = 0x00008000;
  static const uint32_t STATUS_EXCAUSE_MASK  = 0x000f0000;
  static const uint32_t STATUS_RESERVED_MASK = 0xfff00804;
    
  static const uint32_t STATUS_ACTIVE_ACTIVE = 0x00000001;
  static const uint32_t STATUS_ACTIVE_IDLE   = 0x00000000;
  static const uint32_t STATUS_GID_ENABLED   = 0x00000000;
  static const uint32_t STATUS_GID_DISABLED  = 0x00000002;
  static const uint32_t STATUS_EXCAUSE_NONE  = 0x00000000;

  // DEBUGSTATUS register
  static const int DEBUGSTATUS_HALT_SHIFT       = 0;
  static const int DEBUGSTATUS_EXT_PEND_SHIFT   = 1;
  static const int DEBUGSTATUS_MBKPT_FLAG_SHIFT = 2;

  static const uint32_t DEBUGSTATUS_HALT_MASK       = 0x00000001;
  static const uint32_t DEBUGSTATUS_EXT_PEND_MASK   = 0x00000002;
  static const uint32_t DEBUGSTATUS_MBKPT_FLAG_MASK = 0x00000004;
  static const uint32_t DEBUGSTATUS_RESERVED_MASK   = 0xfffffff8;

  static const uint32_t DEBUGSTATUS_HALT_RUNNING      = 0x00000000;
  static const uint32_t DEBUGSTATUS_HALT_HALTED       = 0x00000001;
  static const uint32_t DEBUGSTATUS_EXT_PEND_NONE     = 0x00000000;
  static const uint32_t DEBUGSTATUS_EXT_PEND_PENDING  = 0x00000002;
  static const uint32_t DEBUGSTATUS_MBKPT_FLAG_NONE   = 0x00000000;
  static const uint32_t DEBUGSTATUS_MBKPT_FLAG_ACTIVE = 0x00000004;

  // ILAT register
  static const int ILAT_ILAT_SHIFT = 0;

  static const uint32_t ILAT_ILAT_MASK = 0x000003ff;
  static const uint32_t ILAT_RESERVED_MASK = 0xfffffc00;

  static const uint32_t ILAT_ILAT_SYNC   = 0x00000001;
  static const uint32_t ILAT_ILAT_SWE    = 0x00000002;
  static const uint32_t ILAT_ILAT_PROT   = 0x00000004;
  static const uint32_t ILAT_ILAT_TIMER0 = 0x00000008;
  static const uint32_t ILAT_ILAT_TIMER1 = 0x00000010;
  static const uint32_t ILAT_ILAT_MSG    = 0x00000020;
  static const uint32_t ILAT_ILAT_DMA0   = 0x00000040;
  static const uint32_t ILAT_ILAT_DMA1   = 0x00000080;
  static const uint32_t ILAT_ILAT_WAND   = 0x00000100;
  static const uint32_t ILAT_ILAT_USER   = 0x00000200;

  // DEBUGCMD register
  static const int DEBUGCMD_COMMAND_SHIFT = 0;

  static const uint32_t DEBUGCMD_COMMAND_MASK  = 0x00000003;
  static const uint32_t DEBUGCMD_RESERVED_MASK = 0xfffffffc;

  static const uint32_t DEBUGCMD_COMMAND_RUN      = 0x00000000;
  static const uint32_t DEBUGCMD_COMMAND_HALT     = 0x00000001;
  static const uint32_t DEBUGCMD_COMMAND_EMUL_ON  = 0x00000002;  //!< Undoc!
  static const uint32_t DEBUGCMD_COMMAND_EMUL_OFF = 0x00000003;  //!< Undoc!

  // IVT addresses
  static const uint32_t IVT_SYNC   = 0x00000000;
  static const uint32_t IVT_SWE    = 0x00000004;
  static const uint32_t IVT_PROT   = 0x00000008;
  static const uint32_t IVT_TIMER0 = 0x0000000c;
  static const uint32_t IVT_TIMER1 = 0x00000010;
  static const uint32_t IVT_MSG    = 0x00000014;
  static const uint32_t IVT_DMA0   = 0x00000018;
  static const uint32_t IVT_DMA1   = 0x0000001c;
  static const uint32_t IVT_WAND   = 0x00000020;
  static const uint32_t IVT_USER   = 0x00000024;

  // Constructor and destructor
  TargetControl ();
  ~TargetControl ();

  // Functions to access memory. All register access on the Epiphany is also
  // via memory.
  virtual bool readMem32 (uint32_t addr, uint32_t &) = 0;
  virtual bool readMem16 (uint32_t addr, uint16_t &) = 0;
  virtual bool readMem8 (uint32_t addr, uint8_t &) = 0;


  virtual bool writeMem32 (uint32_t addr, uint32_t value) = 0;
  virtual bool writeMem16 (uint32_t addr, uint16_t value) = 0;
  virtual bool writeMem8 (uint32_t addr, uint8_t value) = 0;

  virtual bool writeBurst (uint32_t addr, uint8_t *buf,
			   size_t buff_size) = 0;
  virtual bool readBurst (uint32_t addr, uint8_t *buf,
			  size_t buff_size) = 0;

  // Functions to access data about the target
  virtual vector <uint16_t>  listCoreIds () = 0;
  virtual unsigned int  getNumRows () = 0;
  virtual unsigned int  getNumCols () = 0;

  // Functions to deal with threads (which correspond to cores)
  virtual bool setThreadGeneral (int threadId) = 0;
  virtual bool setThreadExecute (int threadId) = 0;

  // Control functions
  virtual void platformReset ();
  virtual void resumeAndExit () = 0;
  virtual void startOfBaudMeasurement ();
  virtual double endOfBaudMeasurement ();

  // trace support for vcd Dump
  virtual bool initTrace () = 0;
  virtual bool startTrace () = 0;
  virtual bool stopTrace () = 0;

protected:

  virtual string getTargetId () = 0;
  virtual uint32_t convertAddress (uint32_t  address) = 0;

private:

  //! The start time
  struct timeval startTime;

};

#endif	// TARGET_CONTROL__H


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
