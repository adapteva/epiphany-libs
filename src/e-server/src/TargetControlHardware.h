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

  //! Current core being operated on
  uint16_t  currentCoreId;

  // Handler for the BREAK signal
  static void breakSignalHandler (int signum);

  // Read and write from target
  bool readMem (uint32_t addr, uint32_t & data, unsigned burst_size);
  bool writeMem (uint32_t addr, uint32_t data, unsigned burst_size);

  // Convenience function
  void* findSharedFunc (const char *funcName);

  // pointers to the dynamically loaded functions.
  int (*init_platform) (platform_definition_t* platform,
			unsigned int           verbose);
  int (*close_platform) ();
  int (*write_to) (unsigned int  address,
		   void*         buf,
		   size_t        burst_size);
  int (*read_from) (unsigned  address,
		    void*     data,
		    size_t    burst_size);
  int (*hw_reset) ();
  int (*get_description) (char** targetIdp);

  int (*e_open) (e_epiphany_t* dev,
		 unsigned int  row,
		 unsigned int  col,
		 unsigned int  rows,
		 unsigned int  cols);
  int (*e_close) (e_epiphany_t* dev);
  ssize_t (*e_read) (e_epiphany_t* dev,
		     int           corenum,
		     const off_t   from_addr,
		     void*         buf,
		     size_t        count);
  ssize_t (*e_write) (e_epiphany_t *dev,
		      int           corenum,
		      off_t         to_addr,
		      const void*   buf,
		      size_t        count);
  int (*e_reset) (e_epiphany_t * pEpiphany);
  void (*e_set_host_verbosity) (int verbose);	// @todo Wrong arg type

};	// TargetControlHardware

#endif /* TARGET_CONTROL_HARDWARE__H */


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
