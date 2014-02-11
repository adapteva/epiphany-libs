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

#include <e-xml/src/epiphany_platform.h>
#include <e-hal/src/epiphany-hal-data.h>


using std::map;
using std::pair;
using std::set;


class TargetControlHardware: public TargetControl
{
public:
  // Constructor
  TargetControlHardware (ServerInfo* _si);

  // Functions to access memory. All register access on the ATDSP is via memory
  virtual bool readMem32 (uint32_t addr, uint32_t &);
  virtual bool readMem16 (uint32_t addr, uint16_t &);
  virtual bool readMem8 (uint32_t addr, uint8_t &);

  virtual bool writeMem32 (uint32_t addr, uint32_t value);
  virtual bool writeMem16 (uint32_t addr, uint16_t value);
  virtual bool writeMem8 (uint32_t addr, uint8_t value);

  // Burst write and read
  virtual bool writeBurst (uint32_t addr, uint8_t *buf,
			   size_t buff_size);
  virtual bool readBurst (uint32_t addr, uint8_t *buf,
			  size_t buff_size);

  // Functions to access data about the target
  virtual vector <uint16_t>  listCoreIds ();

  // Functions to deal with threads (which correspond to cores)
  virtual bool setThreadGeneral (int threadId);
  virtual bool setThreadExecute (int threadId);

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
  virtual uint32_t convertAddress (uint32_t  address);


private:

  //! Local pointer to server info
  ServerInfo* si;

  //! Handle for the shared object libraries
  void *dsoHandle;

  //! Map of relative to absolute core ID
  map <uint16_t, uint16_t> coreMap;

  //! Map of memory range to absolute core ID
  map <MemRange, uint16_t, MemRange> coreMemMap;

  //! Map of core ID to memory range
  map <uint16_t, MemRange> reverseCoreMemMap;

  //! Set of all the external memory ranges
  set <MemRange, MemRange> extMemSet;

  //! The number of cores
  unsigned int numCores;

  //! Current core being used for memory and register access.
  uint16_t  currentCoreId;

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

  // Read and write from target
  bool readMem (uint32_t addr, uint32_t & data, unsigned burst_size);
  bool writeMem (uint32_t addr, uint32_t data, unsigned burst_size);

  // Wrappers for dynamically loaded functions
  int initPlatform (platform_definition_t* platform,
		    unsigned int           verbose);
  int closePlatform ();
  int writeTo (unsigned int  address,
	       void*         buf,
	       size_t        burstSize);
  int readFrom (unsigned  address,
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

};	// TargetControlHardware

#endif /* TARGET_CONTROL_HARDWARE__H */


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
