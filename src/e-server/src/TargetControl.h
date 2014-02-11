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

  // Epiphany sizes
  static const unsigned int E_BYTE_BYTES   = 1;
  static const unsigned int E_SHORT_BYTES  = 2;
  static const unsigned int E_WORD_BYTES   = 4;
  static const unsigned int E_DOUBLE_BYTES = 8;

  //! The core we are attached to
  unsigned int fAttachedCoreId;

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
