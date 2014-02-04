// Target control specification for real hardware: Definition.

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

#include "target_param.h"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <map>
#include <string>
#include <utility>

#include <dlfcn.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>

#include "maddr_defs.h"
#include "TargetControlHardware.h"


using std::cerr;
using std::cout;
using std::dec;
using std::endl;
using std::hex;
using std::map;
using std::pair;
using std::setw;


pthread_mutex_t targetControlHWAccess_m = PTHREAD_MUTEX_INITIALIZER;


//! Constructor

//! @param[in] _si             Server information about flags etc.
TargetControlHardware::TargetControlHardware (ServerInfo*   _si) :
  TargetControl (),
  si (_si),
  dsoHandle (NULL),
  numCores (0),
  currentCoreId (0)
{
}	// TargetControlHardware ()


bool
TargetControlHardware::readMem32 (uint32_t addr, uint32_t & data)
{
  bool retSt = false;
  uint32_t data32;

  retSt = readMem (addr, data32, 4);
  data = data32;

  return retSt;
}


bool
TargetControlHardware::readMem16 (uint32_t addr, uint16_t & data)
{
  bool retSt = false;
  uint32_t data32;

  retSt = readMem (addr, data32, 2);
  data = data32 & 0x0000ffff;

  return retSt;
}


bool
TargetControlHardware::readMem8 (uint32_t addr, uint8_t & data)
{
  bool retSt = false;
  uint32_t data32;

  retSt = readMem (addr, data32, 1);
  data = data32 & 0x000000ff;

  return retSt;
}


bool
TargetControlHardware::writeMem32 (uint32_t addr, uint32_t value)
{
  bool retSt = false;
  uint32_t data32 = value;

  retSt = writeMem (addr, data32, 4);

  return retSt;
}


bool
TargetControlHardware::writeMem16 (uint32_t addr, uint16_t value)
{
  bool retSt = false;
  uint32_t data32 = 0;

  data32 = value & 0x0000ffff;
  retSt = writeMem (addr, data32, 2);

  return retSt;
}


bool
TargetControlHardware::writeMem8 (uint32_t addr, uint8_t value)
{
  bool retSt = false;
  uint32_t data32 = 0;

  data32 = value & 0x000000ff;
  retSt = writeMem (addr, data32, 1);

  return retSt;
}


// see target driver

// burst read
bool
TargetControlHardware::readBurst (uint32_t addr, uint8_t *buf,
				  size_t buff_size)
{
  bool ret = true;

  uint32_t fullAddr = convertAddress (addr);

  // cerr << "READ burst " << hex << fullAddr << " Size " << dec << buff_size << endl;

  if (fullAddr || !si->checkHwAddr())
    {
      if ((fullAddr % E_WORD_BYTES) == 0)
	{
	  pthread_mutex_lock (&targetControlHWAccess_m);

	  // struct timeval start_time;
	  // StartOfBaudMeasurement(start_time);
	  for (unsigned k = 0;
	       k < buff_size / (MAX_NUM_READ_PACKETS * E_WORD_BYTES); k++)
	    {
	      int res =
		(*read_from) (fullAddr + k * MAX_NUM_READ_PACKETS * E_WORD_BYTES,
			      (void *) (buf +
					k * MAX_NUM_READ_PACKETS * E_WORD_BYTES),
			      (MAX_NUM_READ_PACKETS * E_WORD_BYTES));

	      if (res != (MAX_NUM_READ_PACKETS * E_WORD_BYTES))
		{
		  cerr << "ERROR (" << res <<
		    "): memory read failed for full address " << hex <<
		    fullAddr << dec << endl;
		  ret = false;
		}
	    }

	  unsigned trailSize = (buff_size % (MAX_NUM_READ_PACKETS * E_WORD_BYTES));
	  if (trailSize != 0)
	    {
	      unsigned int res =
		(*read_from) (fullAddr + buff_size - trailSize,
			      (void *) (buf + buff_size - trailSize),
			      trailSize);

	      if (res != trailSize)
		{
		  cerr << "ERROR (" << res <<
		    "): memory read failed for full address " << hex <<
		    fullAddr << dec << endl;
		  ret = false;
		}
	    }

	  // cerr << " done nbytesToSend " << buff_size << " msec " << EndOfBaudMeasurement(start_time) << endl;
	  pthread_mutex_unlock (&targetControlHWAccess_m);
	}
      else
	{
	  for (unsigned i = 0; i < buff_size; i++)
	    {
	      ret = ret && readMem8 (fullAddr + i, buf[i]);
	    }
	}
    }
  else
    {
      cerr << "WARNING (READ_BURST ignored): The address " << hex << addr <<
	" is not in the valid range for target " << this->
	getTargetId () << dec << endl;
      ret = false;
    }

  return ret;
}


// burst write
bool
TargetControlHardware::writeBurst (uint32_t addr, uint8_t *buf,
				   size_t buff_size)
{
  bool ret = true;

  if (buff_size == 0)
    return true;

  pthread_mutex_lock (&targetControlHWAccess_m);

  uint32_t fullAddr = convertAddress (addr);

  // cerr << "---Write burst " << hex << fullAddr << " Size " << dec << buff_size << endl;

  assert (buff_size > 0);

  if (fullAddr || !si->checkHwAddr())
    {
      if ((buff_size == E_WORD_BYTES) && ((fullAddr % E_WORD_BYTES) == 0))
	{
	  // register access -- should be word transaction
	  // cerr << "---Write WORD " << hex << fullAddr << " Size " << dec << E_WORD_BYTES << endl;
	  unsigned int res = (*write_to) (fullAddr, (void *) (buf),
					  E_WORD_BYTES);
	  if (res != E_WORD_BYTES)
	    {
	      cerr << "ERROR (" << res <<
		"): mem write failed for full address " << hex << fullAddr <<
		dec << endl;
	      ret = false;
	    }
	}
      else
	{
	  // head up to double boundary
	  if ((fullAddr % E_DOUBLE_BYTES) != 0)
	    {
	      unsigned int headSize = E_DOUBLE_BYTES - (fullAddr % E_DOUBLE_BYTES);

	      if (headSize > buff_size)
		{
		  headSize = buff_size;
		}

	      // head
	      for (unsigned n = 0; n < headSize; n++)
		{
		  // cerr << "head fullAddr " << hex << fullAddr << " size " << 1 << endl;
		  int res = (*write_to) (fullAddr, (void *) (buf), 1);
		  if (res != 1)
		    {
		      cerr << "ERROR (" << res <<
			"): mem write failed for full address " << hex <<
			fullAddr << dec << endl;
		      ret = false;
		    }
		  buf += 1;
		  fullAddr += 1;
		  buff_size = buff_size - 1;
		}
	    }

	  assert (buff_size == 0 || (fullAddr % E_DOUBLE_BYTES) == 0);
	  assert ((MAX_NUM_WRITE_PACKETS % E_DOUBLE_BYTES) == 0);
	  size_t numMaxBurst = buff_size / (MAX_NUM_WRITE_PACKETS * E_DOUBLE_BYTES);

	  for (unsigned k = 0; k < numMaxBurst; k++)
	    {
	      unsigned cBufSize = (MAX_NUM_WRITE_PACKETS * E_DOUBLE_BYTES);

	      // cerr << "BIG DOUBLE BURST " << k << " fullAddr " << hex << fullAddr << " size " << cBufSize << endl;

	      size_t res = (*write_to) (fullAddr, (void *) (buf), cBufSize);
	      if (res != cBufSize)
		{
		  cerr << "ERROR (" << res <<
		    "): mem write failed for full address " << hex << fullAddr
		    << dec << endl;
		  ret = false;
		}
	      fullAddr += cBufSize;
	      buf += cBufSize;
	      buff_size = buff_size - cBufSize;
	    }

	  size_t trailSize = buff_size % E_DOUBLE_BYTES;
	  if (buff_size > trailSize)
	    {
	      // cerr << "LAST DOUBLE BURST " << " fullAddr " << hex << fullAddr << " size " << buff_size-trailSize << endl;
	      size_t res =
		(*write_to) (fullAddr, (void *) (buf), buff_size - trailSize);
	      if (res != buff_size - trailSize)
		{
		  cerr << "ERROR (" << res <<
		    "): mem write failed for full address " << hex << fullAddr
		    << dec << endl;
		  ret = false;
		}
	      fullAddr += buff_size - trailSize;
	      buf += buff_size - trailSize;
	    }

	  // trail
	  if (trailSize > 0)
	    {
	      for (unsigned n = 0; n < trailSize; n++)
		{
		  // cerr << "TRAIL " << " fullAddr " << hex << fullAddr << " size " << 1 << endl;
		  int res = (*write_to) (fullAddr, (void *) (buf), 1);
		  if (res != 1)
		    {
		      cerr << "ERROR (" << res <<
			"): mem write failed for full address " << hex <<
			fullAddr << dec << endl;
		      ret = false;
		    }

		  buf += 1;
		  fullAddr += 1;
		}
	    }
	}
    }
  else
    {
      cerr << "WARNING (WRITE_BURST ignored): The address " << hex << addr <<
	" is not in the valid range for target " << this->
	getTargetId () << dec << endl;
      ret = false;
    }

  pthread_mutex_unlock (&targetControlHWAccess_m);

  return ret;
}


//! initialize the attached core ID

//! Reset the platform
void
TargetControlHardware::platformReset ()
{
  pthread_mutex_lock (&targetControlHWAccess_m);

  hw_reset ();			// ESYS_RESET

  pthread_mutex_unlock (&targetControlHWAccess_m);

}	// platformReset ()


//! Resume and exit

//! This is only supported in simulation targets.
void
TargetControlHardware::resumeAndExit ()
{
  cerr << "Warning: Resume and detach not supported in real hardware: ignored."
       << endl;

}	// resumeAndExit ()


//! Initialize VCD tracing (null operation in real hardware)

// @return TRUE to indicate tracing was successfully initialized.
bool
TargetControlHardware::initTrace ()
{
  return true;

}	// initTrace ()


//! Start VCD tracing (null operation in real hardware)

// @return TRUE to indicate tracing was successfully stopped.
bool
TargetControlHardware::startTrace ()
{
  return true;

}	// startTrace ()


//! Stop VCD tracing (null operation in real hardware)

// @return TRUE to indicate tracing was successfully stopped.
bool
TargetControlHardware::stopTrace ()
{
  return true;

}	// stopTrace ()


//! Close the target due to Ctrl-C signal

//! @todo Have reset from client

//! @param[in] Signal number
void
TargetControlHardware::breakSignalHandler (int signum)
{
  cerr << " Get OS signal .. exiting ..." << endl;
  // give chance to finish usb drive
  // sleep(1);

  // hw_reset();

  // sleep(1);

  // None of this works in a static function - sigh
  // if (handle)
  //   {
  //     close_platform ();
  //     // e_close(pEpiphany);
  //     dlclose (handle);
  //   }

  exit (0);
}


//! Initialize the hardware platform

//! This involves setting up the shared object functions first, then calling
//! the system init and resetting if necessary.
void
TargetControlHardware::initHwPlatform (platform_definition_t * platform)
{
  if (NULL == (dsoHandle = dlopen (platform->lib, RTLD_LAZY)))
    {
      cerr << "ERROR: Can't open hardware platform library " << platform->lib
	   << ": " << dlerror () << endl;
      exit (EXIT_FAILURE);
    }

  // Find the shared functions
  *(void **) (&init_platform) = findSharedFunc ("esrv_init_platform");
  *(void **) (&close_platform) = findSharedFunc ("esrv_close_platform");
  *(void **) (&write_to) = findSharedFunc ("esrv_write_to");
  *(void **) (&read_from) = findSharedFunc ("esrv_read_from");
  *(void **) (&e_open) = findSharedFunc ("e_open");
  *(void **) (&e_close) = findSharedFunc ("e_close");
  *(void **) (&e_write) = findSharedFunc ("e_write");
  *(void **) (&e_read) = findSharedFunc ("e_read");
  *(void **) (&get_description) = findSharedFunc ("esrv_get_description");
  *(void **) (&hw_reset) = findSharedFunc ("esrv_hw_reset");
  *(void **) (&e_set_host_verbosity) = findSharedFunc ("e_set_host_verbosity");


  // add signal handler to close target connection
  if (signal (SIGINT, breakSignalHandler) < 0)
    {
      cerr << "ERROR: Failed to register BREAK signal handler: "
	   << strerror (errno) << "." << endl;
      exit (EXIT_FAILURE);
    }

  // Initialize target platform.
  int res = (*init_platform) (platform, si->halDebug());

  if (res < 0)
    {
      cerr << "ERROR: Can't initialize target device: Error code " << res << "."
	   << endl;
      exit (EXIT_FAILURE);
    }

  // Optionally reset the platform
  if (si->skipPlatformReset())
    {
      cerr << "Warning: No hardware reset sent to target" << endl;
    }
  else if (hw_reset () != 0)
    {
      cerr << "ERROR: Cannot reset the hardware." << endl;
      exit (EXIT_FAILURE);
    }
}


//! Create maps and sets for the platform.

//! We set up the following
//! - a map from relative to absolute core ID.
//! - a map from memory range to absolute coreID.
//! - a set of all the external memory ranges.

//! @param[in] platform  The platform definition.
void
TargetControlHardware::initMaps (platform_definition_t* platform)
{
  numCores = 0;

  // Iterate through each core on each chip
  for (unsigned int  chipNum = 0; chipNum < platform->num_chips; chipNum++)
    {
      chip_def_t  chip = platform->chips[chipNum];

      for (unsigned int row = 0; row < chip.num_rows; row++)
	{
	  assert (row < (0x1 << 6));		// Max 6 bits

	  for (unsigned int col = 0; col < chip.num_cols; col++)
	    {
	      assert (col < (0x1 << 6));	// Max 6 bits
	      
	      // Set up the relative to absolute core ID map entry
	      uint16_t relId = (row << 6) | col;
	      uint16_t absRow = chip.yid + row;
	      uint16_t absCol = chip.xid + col;
	      uint16_t absId = (absRow << 6) | absCol;

	      coreMap[relId] = absId;

	      // Set up the memory map to and from absolute core ID map
	      // entries.
	      uint32_t  minAddr = (((uint32_t) absRow) << 26)
		| (((uint32_t) absCol) << 20);
	      uint32_t  maxAddr = minAddr + chip.core_memory_size - 1;
	      uint32_t  minRegAddr = minAddr + 0xf0000;
	      uint32_t  maxRegAddr = minAddr + 0xf1000 - 1;
	      MemRange r (minAddr, maxAddr, minRegAddr, maxRegAddr);

	      coreMemMap[r] = absId;
	      reverseCoreMemMap [absId] = r;

	      // One more core done
	      numCores++;
	    }
	}
    }

  // Populate external memory map set
  for (unsigned int  bankNum = 0; bankNum < platform->num_banks; bankNum++)
    {
      mem_def_t  bank = platform->ext_mem[bankNum];
      uint32_t  minAddr = bank.base;
      uint32_t  maxAddr = bank.base + bank.size - 1;
      MemRange r (minAddr, maxAddr);

      if (extMemSet.find (r) == extMemSet.end())
	(void) extMemSet.insert (r);
      else
	{
	  cerr << "ERROR: Duplicate or overlapping extenal memory bank: [0x"
	       << hex << setw (8) << r.minAddr () << ", 0x"
	       << r.maxAddr () << "]." << dec << setw (0) << endl;
	  exit (EXIT_FAILURE);
	}
    }
}	// initMaps ()


//! Print out the maps
void
TargetControlHardware::showMaps ()
{
  // Iterate over all the cores
  cout << "Core details:" << endl;

  for (map <uint16_t, uint16_t>::iterator it = coreMap.begin ();
       it != coreMap.end ();
       it++)
    {
      uint16_t relId = it->first;
      uint16_t absId = it->second;

      uint16_t relRow = relId >> 6;
      uint16_t relCol = relId & 0x3f;
      uint16_t absRow = absId >> 6;
      uint16_t absCol = absId & 0x3f;

      // We really can't have a core without a memory map, but do a sanity
      // check.
      assert (reverseCoreMemMap.find (absId) != reverseCoreMemMap.end());
      MemRange r = reverseCoreMemMap[absId];
      uint32_t  minAddr = r.minAddr ();
      uint32_t  maxAddr = r.maxAddr ();
      uint32_t  minRegAddr = r.minRegAddr ();
      uint32_t  maxRegAddr = r.maxRegAddr ();

      cout << "  relative -> absolute core ID (" << relRow << ", " << relCol
	   << ") ->  (" << absRow << ", " << absCol << ")" << endl;
      cout << "    memory range   [0x" << hex << setw (8) << minAddr << ", 0x"
	   << maxAddr << "]" << endl;
      cout << "    register range [0x" << minRegAddr << ", 0x" << maxRegAddr
	   << "]" << dec << setw (0) << endl;
    }

  // Iterate over all the memories
  cout << endl;
  cout << "External memories" << endl;

  for (set <MemRange>::iterator it = extMemSet.begin ();
       it != extMemSet.end ();
       it++)
    {
      MemRange r = *it;
      uint32_t  minAddr = r.minAddr ();
      uint32_t  maxAddr = r.maxAddr ();

      cout << "  [" << hex << setw (8) << minAddr << ", 0x" << maxAddr << "]"
	   << endl;
    }
}	// showMaps ()


string
TargetControlHardware::getTargetId ()
{
  char *targetId;
  get_description (&targetId);

  return string (targetId);
}


//! Convert a local address to a global one.

//! If we have a local address, then convert it to a global address based on
//! the current thread being used.
uint32_t
TargetControlHardware::convertAddress (uint32_t address)
{
  assert (dsoHandle);

  if (address < CORE_SPACE)
    {
      return (((uint32_t) currentCoreId) << 20) | (address & 0x000fffff);
    }

  // Validate any global address if requested
  if (si->checkHwAddr ())
    {
      MemRange r (address, address);
      if (coreMemMap.find (r) != coreMemMap.end ())
	return address;			// Another core
      else if (extMemSet.find (r) != extMemSet.end ())
	return address;			// External memory
      else
	{
	  uint16_t absRow = currentCoreId >> 6;
	  uint16_t absCol = currentCoreId & 0x3f;

	  cerr << "ERROR: core ID (" << absRow << ", " << absCol 
	       << "): invalid address 0x" << hex << setw (8) << address
	       << setw (0) << dec << "." << endl;
	  exit (EXIT_FAILURE);
	}
    }
  else
    return address;

}	// convertAddress ()


bool
TargetControlHardware::readMem (uint32_t addr, uint32_t & data,
				unsigned burst_size)
{
  bool retSt = false;

  //struct timeval start_t;
  // StartOfBaudMeasurement(start_t);

  pthread_mutex_lock (&targetControlHWAccess_m);

  uint32_t fullAddr = convertAddress (addr);
  // bool iSAligned = (fullAddr == ());
  if (fullAddr || si->checkHwAddr())
    {
      // supported only word size or smaller
      assert (burst_size <= 4);
      char buf[8];
      unsigned int res = (*read_from) (fullAddr, (void *) buf, burst_size);

      if (res != burst_size)
	{
	  cerr << "ERROR (" << res << "): mem read failed for addr " << hex <<
	    addr << dec << endl;
	}
      else
	{
	  // pack returned data
	  for (unsigned i = 0; i < burst_size; i++)
	    {
	      data = (data & (~(0xff << (i * 8)))) | (buf[i] << (i * 8));
	    }
	  if (si->debugTargetWr ())
	    {
	      cerr << "TARGET READ (" << burst_size << ") " << hex << fullAddr
		<< " >> " << data << dec << endl;
	    }
	}
      retSt = (res == burst_size);
    }
  else
    {
      cerr << "WARNING (READ_MEM ignored): The address " << hex << addr <<
	" is not in the valid range for target " << this->
	getTargetId () << dec << endl;
    }

  // double mes = EndOfBaudMeasurement(start_t);
  // cerr << "--- READ milliseconds: " << mes << endl;

  pthread_mutex_unlock (&targetControlHWAccess_m);

  return retSt;
}


bool
TargetControlHardware::writeMem (uint32_t addr, uint32_t data,
				 unsigned burst_size)
{
  bool retSt = false;
  pthread_mutex_lock (&targetControlHWAccess_m);
  uint32_t fullAddr = convertAddress (addr);

  // bool iSAligned = (fullAddr == ());
  if (fullAddr || si->checkHwAddr())
    {
      assert (burst_size <= 4);
      char buf[8];

      for (unsigned i = 0; i < burst_size; i++)
	{
	  buf[i] = (data >> (i * 8)) & 0xff;
	}

      // struct timeval start_t;
      // StartOfBaudMeasurement(start_t);

      unsigned int res = (*write_to) (fullAddr, (void *) buf, burst_size);

      // double mes = EndOfBaudMeasurement(start_t);
      // cerr << "--- WRITE (writeMem)(" << burst_size << ") milliseconds: " << mes << endl;

      if (si->debugTargetWr ())
	{
	  cerr << "TARGET WRITE (" << burst_size << ") " << hex << fullAddr <<
	    " >> " << data << dec << endl;
	}

      if (res != burst_size)
	{
	  cerr << "ERROR (" << res << "): mem write failed for addr " << hex
	    << fullAddr << dec << endl;
	}

      retSt = (res == burst_size);
    }
  else
    {
      cerr << "WARNING (WRITE_MEM ignored): The address " << hex << addr <<
	" is not in the valid range for target " << this->
	getTargetId () << dec << endl;
    }

  pthread_mutex_unlock (&targetControlHWAccess_m);

  return retSt;
}


//! Find a function from a shared library.

//! Convenience function which loads the function and exits with an error
//! message in the event of any failures.

//! @param[in] funcName  The function to find
//! @return  The pointer to the function.
void *
TargetControlHardware::findSharedFunc (const char *funcName)
{
  (void) dlerror ();			// Clear any old error

  void *funcPtr = dlsym (dsoHandle, funcName);
  char *error = dlerror ();

  if (error != NULL)
    {
      cerr << "ERROR: Failed to load shared function" << funcName << ": "
	   << error << endl;
      exit (EXIT_FAILURE);
    }

  return funcPtr;

}	// loadSharedFunc ()


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
