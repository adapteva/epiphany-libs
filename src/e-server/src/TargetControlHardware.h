/* Target control specification for real hardware: Declaration.

  This file is part of the Epiphany Software Development Kit.

  Copyright (C) 2013 Adapteva, Inc.
  See AUTHORS for list of contributors.
  Support e-mail: <support@adapteva.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program (see the file COPYING).  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef TARGET_CONTROL_HARDWARE__H
#define TARGET_CONTROL_HARDWARE__H

#include <map>
#include <inttypes.h>
#include <set>

#include "MemRange.h"
#include "ServerInfo.h"
#include "TargetControl.h"

#include "epiphany_platform.h"
#include "epiphany-hal-data.h"


using std::map;
using std::pair;
using std::set;


class TargetControlHardware: public TargetControl
{
public:
  // Constructor
  TargetControlHardware (ServerInfo* _si);

  // Functions to access memory. All register access on the Epiphany is via
  // memory
  virtual bool readMem32 (CoreId coreId, uint32_t addr, uint32_t &);
  virtual bool readMem16 (CoreId coreId, uint32_t addr, uint16_t &);
  virtual bool readMem8 (CoreId coreId, uint32_t addr, uint8_t &);

  virtual bool writeMem32 (CoreId coreId, uint32_t addr, uint32_t value);
  virtual bool writeMem16 (CoreId coreId, uint32_t addr, uint16_t value);
  virtual bool writeMem8 (CoreId coreId, uint32_t addr, uint8_t value);

  // Read and write single words from target
  bool readMem (CoreId  coreId,
		uint32_t  addr,
		uint32_t& data,
		size_t    len);
  bool writeMem (CoreId coreId,
		 uint32_t addr,
		 uint32_t data,
		 size_t   len);

  // Burst write and read
  virtual bool writeBurst (CoreId coreId, uint32_t addr, uint8_t *buf,
			   size_t buff_size);
  virtual bool readBurst (CoreId coreId, uint32_t addr, uint8_t *buf,
			  size_t buff_size);

  // Functions to access data about the target
  virtual vector <CoreId>::iterator  coreIdBegin ();
  virtual vector <CoreId>::iterator  coreIdEnd ();
  virtual unsigned int  getNumRows ();
  virtual unsigned int  getNumCols ();
  virtual CoreId abs2rel (CoreId absCoreId);

  // Initialization functions
  void  initHwPlatform (platform_definition_t* platform);
  void  initMaps (platform_definition_t* platform);
  void  showMaps ();

  // Control functions
  virtual void platformReset ();
  virtual void resumeAndExit ();

  // VCD trace (null operation on real hardware)
  virtual bool initTrace ();
  virtual bool startTrace ();
  virtual bool stopTrace ();


protected:

  virtual string getTargetId ();
  virtual uint32_t convertAddress (CoreId relCoreId, uint32_t  address);


private:

  //! Maximum number of double word packets in a write burst
  static const size_t MAX_NUM_WRITE_PACKETS = 256;

  //! Maximum number of word packets in a read burst
  static const size_t MAX_NUM_READ_PACKETS = 64;

  //! Maximum number of bytes in a write burst
  static const size_t MAX_BURST_WRITE_BYTES =
    MAX_NUM_WRITE_PACKETS * E_DOUBLE_BYTES;

  //! Maximum number of bytes in a read burst
  static const size_t MAX_BURST_READ_BYTES =
    MAX_NUM_READ_PACKETS * E_WORD_BYTES;

  //! Local pointer to server info
  ServerInfo* si;

  //! Handle for the shared object libraries
  void *dsoHandle;

  //! Vector of all the relative CoreIds
  vector <CoreId> relCoreIds;

  //! Map of relative to absolute core ID
  map <CoreId, CoreId>  rel2absCore;

  //! Reverse map of absolute to relative core ID
  map <CoreId, CoreId>  abs2relCore;

  //! Map of memory range to absolute core ID
  map <MemRange, CoreId, MemRange> coreMemMap;

  //! Map of core ID to memory range
  map <CoreId, MemRange> reverseCoreMemMap;

  //! Set of all the external memory ranges
  set <MemRange, MemRange> extMemSet;

  //! The number of cores
  unsigned int  numCores;

  //! The number of rows
  unsigned int  numRows;

  //! The number of columns
  unsigned int  numCols;

  //! Current core being used for memory and register access.
  CoreId  currentCoreId;

  //! Current thread to use for general access to memory and registers.

  //! A value of -1 means all threads, 0 means any thread, any other value N,
  //! corresponds to Core ID N - 1. However a value of -1 is meaningless for
  //! access to memory and registers.
  int  threadIdGeneral;

  //! Current thread to use for execution.

  //! @see threadIdGeneral for details.
  int  threadIdExecute;

  // Handler for the BREAK signal
  static void breakSignalHandler (int signum);

  // Wrappers for dynamically loaded functions
  int initPlatform (platform_definition_t* platform,
		    unsigned int           verbose);
  int closePlatform ();
  size_t writeTo (unsigned int  address,
		  void*         buf,
		  size_t        burstSize);
  size_t readFrom (unsigned  address,
		   void*     buf,
		   size_t    burstSize);
  int hwReset ();
  int getDescription (char** targetIdp);

  // Convenience function
  void* findSharedFunc (const char *funcName);

  // pointers to the dynamically loaded functions.
  int (*initPlatformFunc) (platform_definition_t* platform,
			   unsigned int           verbose);
  int (*closePlatformFunc) ();
  int (*writeToFunc) (unsigned int  address,
		      void*         buf,
		      size_t        burstSize);
  int (*readFromFunc) (unsigned  address,
		       void*     buf,
		       size_t    burstSize);
  int (*hwResetFunc) ();
  int (*getDescriptionFunc) (char** targetIdp);

  //! Integer to string conversion
  string  intStr (int  val,
		  int  base = 10,
		  int  width = 0) const;

};	// TargetControlHardware

#endif /* TARGET_CONTROL_HARDWARE__H */


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// show-trailing-whitespace: t
// End:
