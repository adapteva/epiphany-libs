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

#include "TargetControlHardware.h"


using std::cerr;
using std::cout;
using std::dec;
using std::endl;
using std::hex;
using std::map;
using std::pair;
using std::setfill;
using std::setw;


//! Constructor

//! @param[in] _si             Server information about flags etc.
TargetControlHardware::TargetControlHardware (ServerInfo*   _si) :
  TargetControl (),
  si (_si),
  dsoHandle (NULL),
  numCores (0)
{
}	// TargetControlHardware ()


bool
TargetControlHardware::readMem32 (uint16_t coreId,
				  uint32_t addr,
				  uint32_t& data)
{
  bool retSt = false;
  uint32_t data32;

  retSt = readMem (coreId, addr, data32, 4);
  data = data32;

  return retSt;
}


bool
TargetControlHardware::readMem16 (uint16_t coreId,
				  uint32_t addr,
				  uint16_t & data)
{
  bool retSt = false;
  uint32_t data32;

  retSt = readMem (coreId, addr, data32, 2);
  data = data32 & 0x0000ffff;

  return retSt;
}


bool
TargetControlHardware::readMem8 (uint16_t coreId, 
				 uint32_t addr,
				 uint8_t & data)
{
  bool retSt = false;
  uint32_t data32;

  retSt = readMem (coreId, addr, data32, 1);
  data = data32 & 0x000000ff;

  return retSt;
}


bool
TargetControlHardware::writeMem32 (uint16_t coreId,
				   uint32_t addr,
				   uint32_t value)
{
  bool retSt = false;
  uint32_t data32 = value;

  retSt = writeMem (coreId, addr, data32, 4);

  return retSt;
}


bool
TargetControlHardware::writeMem16 (uint16_t coreId,
				   uint32_t addr,
				   uint16_t value)
{
  bool retSt = false;
  uint32_t data32 = 0;

  data32 = value & 0x0000ffff;
  retSt = writeMem (coreId, addr, data32, 2);

  return retSt;
}


bool
TargetControlHardware::writeMem8 (uint16_t coreId,
				  uint32_t addr,
				  uint8_t value)
{
  bool retSt = false;
  uint32_t data32 = 0;

  data32 = value & 0x000000ff;
  retSt = writeMem (coreId, addr, data32, 1);

  return retSt;
}


//! Read up to 4 bytes from memory

//! @todo Appears to be no requirement for alignment. Is this true?

//! @param[in]  coreId  Relative core ID to read from.
//! @param[in]  addr    Address (local or global) to read from.
//! @param[out] data    Where to put the result.
//! @param[in]  len     Size of data to read (in bytes)
//! @return  TRUE on success, FALSE otherwise.
bool
TargetControlHardware::readMem (uint16_t  coreId,
				uint32_t  addr,
				uint32_t& data,
				size_t    len)
{
  assert (len <= E_WORD_BYTES);

  uint32_t fullAddr = convertAddress (coreId, addr);
  unsigned char buf[E_DOUBLE_BYTES];	// May read a double word

  size_t res = readFrom (fullAddr, (void *) buf, len);
  if (res != len)
    {
      cerr << "Warning: readMem failed for addr " << (void *) addr
	   << ", length " << len << ", result " << res << endl;
      return false;
    }
  else
    {
      // pack returned data
      data = 0;
      for (unsigned i = 0; i < len; i++)
	data = (data & (~(0xff << (i * 8)))) | (buf[i] << (i * 8));

      if (si->debugTargetWr ())
	cerr << "DebugTargetWr: readMem (" << coreId << ", " << (void *) addr
	     << ":" << (void *) fullAddr << ", " << (void *) data << ", "
	     << len << ") -> " << (void *) data << endl;
      
      return true;
    }
}	// readMem ()


//! Write up to 4 bytes to memory

//! @todo Appears to be no requirement for alignment. Is this true?

//! @param[in] coreId  Relative core ID to read from.
//! @param[in] addr    Address (local or global) to read from.
//! @param[in] data    The data to write
//! @param[in] len     Size of data to read (in bytes)
//! @return  TRUE on success, FALSE otherwise.
bool
TargetControlHardware::writeMem (uint16_t  coreId,
				 uint32_t  addr,
				 uint32_t  data,
				 size_t    len)
{
  assert (len <= E_WORD_BYTES);

  uint32_t fullAddr = convertAddress (coreId, addr);
  char buf[8];

  for (unsigned i = 0; i < len; i++)
    buf[i] = (data >> (i * 8)) & 0xff;

  if (si->debugTargetWr ())
    cerr << "DebugTargetWr: writeMem (" << coreId << ", " << (void *) addr
	 << ", " << (void *) data << ", " << len << ")" << endl;

  size_t res = writeTo (fullAddr, (void *) buf, len);
  if (res != len)
    {
      cerr << "Warning: writeMem failed for addr " << (void *) addr
	   << ":" << (void *) fullAddr << ", length " << len << ", result "
	   << res << endl;
      return false;
    }

  return true;

}	// writeMem ()


//! Burst read

//! @todo This only seems to do a burst read for word aligned blocks. Why not
//!       for all blocks, as with writeBurst?

//! @param[in]  coreId  The relative core to read from.
//! @param[in]  addr    The address (local or global) to read from.
//! @param[out] buf     Where to put the results.
bool
TargetControlHardware::readBurst (uint16_t coreId,
				  uint32_t addr,
				  uint8_t *buf,
				  size_t burstSize)
{
  uint32_t fullAddr = convertAddress (coreId, addr);

  if (si->debugTargetWr ())
    cerr << "DebugTargetWr: readBurst (" << coreId << ", " << (void *) addr
	 << ", " << (void *) buf << ", " << burstSize << ")" << endl;
      

  if ((fullAddr % E_WORD_BYTES) == 0)
    {
      // Read aligned in blocks
      // @todo Why not for large unaligned blocks as well, cf writeBurst
      for (unsigned k = 0; k < burstSize / (MAX_BURST_READ_BYTES); k++)
	{
	  uint32_t startAddr = fullAddr + k * MAX_BURST_READ_BYTES;
	  uint8_t* startBuf = buf + k * MAX_BURST_READ_BYTES;
	  size_t res = readFrom (startAddr, (void *) startBuf,
				 MAX_BURST_READ_BYTES);

	  if (res != MAX_BURST_READ_BYTES)
	    {
	      cerr << "ERROR: Maximal read burst failed for full address "
		   << (void *) fullAddr << ", burst size " << burstSize
		   << ", result " << res << endl;
	      return false;
	    }
	}

      unsigned trailSize = burstSize % MAX_BURST_READ_BYTES;
      if (trailSize != 0)
	{
	  size_t res = readFrom (fullAddr + burstSize - trailSize,
				 (void *) (buf + burstSize - trailSize),
				 trailSize);

	  if (res != trailSize)
	    {
	      cerr << "ERROR: Trailing read burst failed for full address "
		   << (void *) fullAddr << ", burst size " << burstSize
		   << ", result " << res << endl;
	      return false;
	    }
	}
    }
  else
    {
      // Unaligned read a byte at a time
      for (unsigned i = 0; i < burstSize; i++)
	{
	  if (!readMem8 (coreId, fullAddr + i, buf[i]))
	    {
	      cerr << "ERROR: Unaligned read burst failed for full address "
		   << (void *) fullAddr << ", burst size " << burstSize
		   << ", byte " << i << endl;
	      return false;
	    }
	}
    }

  return true;

}	// readBurst ()


//! Burst write

//! @param[in] addr     Address to write to (full or local)
//! @param[in] buf      Data to write
//! @param[in] bufSize  Number of bytes of data to write
//! @return  TRUE on success, FALSE otherwise.
bool
TargetControlHardware::writeBurst (uint16_t coreId,
				   uint32_t addr,
				   uint8_t *buf,
				   size_t bufSize)
{
  if (bufSize == 0)
    return true;

  uint32_t fullAddr = convertAddress (coreId, addr);

  if (si->debugTargetWr ())
    {
      cerr << "DebugTargetWr: Write burst to 0x" << hex << setw (8)
	   << setfill ('0') << addr << " (0x" << fullAddr << "), size "
	   << setfill (' ') << setw (0) << dec << bufSize << " bytes." << endl;
    }
  
  if ((bufSize == E_WORD_BYTES) && ((fullAddr % E_WORD_BYTES) == 0))
    {
      // Single aligned word write. Typically register access
      if (si->debugTargetWr ())
	cerr << "DebugTargetWr: Write burst single word" << endl;

      unsigned int res = writeTo (fullAddr, buf, E_WORD_BYTES);
      if (res == E_WORD_BYTES)
	return true;
      else
	{
	  cerr << "Warning: WriteBurst of single word to address 0x" << hex
	       << setw (8) << setfill ('0') << fullAddr
	       << " failed with result " << res << "." << setfill (' ')
	       << setw (0) << dec << endl;
	  return  false;
	}
    }
  else
    {
      if ((fullAddr % E_DOUBLE_BYTES) != 0)
	{
	  // Not double word alinged. Head up to double boundary
	  unsigned int headSize = E_DOUBLE_BYTES - (fullAddr % E_DOUBLE_BYTES);
	  headSize = (headSize > bufSize) ? bufSize : headSize;

	  // Write out bytes up to head
	  for (unsigned int n = 0; n < headSize; n++)
	    {
	      if (si->debugTargetWr ())
		{
		  cerr << "DebugTargetWr: Write burst head byte " << n
		       << " to 0x" << hex << setw (8) << setfill ('0') 
		       << fullAddr << "." << setfill (' ') << setw (0)
		       << dec << endl;
		}

	      int res = writeTo (fullAddr, buf, 1);
	      if (res == 1)
		{
		  buf++;
		  fullAddr++;
		  bufSize--;
		}
	      else
		{
		  cerr << "Warning: Write burst of 1 header byte to address 0x"
		       << hex << setw (8) << setfill ('0') << fullAddr
		       << " failed with result " << setfill (' ') << setw (0)
		       << dec << res << "." << endl;
		  return  false;
		}
	    }
	}

      if (0 == bufSize)
	return  true;

      // We should now be double word aligned. Second assertion should be
      // optimized out at compile time.
      assert ((fullAddr % E_DOUBLE_BYTES) == 0);
      assert ((MAX_NUM_WRITE_PACKETS % E_DOUBLE_BYTES) == 0);

      // Break up into maximum size packets
      size_t numMaxBurst = bufSize / MAX_BURST_WRITE_BYTES;

      for (unsigned k = 0; k < numMaxBurst; k++)
	{
	  // Send a maximal packet
	  size_t  cBufSize = (MAX_BURST_WRITE_BYTES);

	  if (si->debugTargetWr ())
	    {
	      cerr << "DebugTargetWr: Maximal write burst " << k
		   << " to full address 0x" << hex << setw (8) << setfill ('0')
		   << fullAddr << setfill (' ') << setw (0) << dec
		   << ", size " << cBufSize << "bytes." << endl;
	    }

	  size_t  res = writeTo (fullAddr, (void *) (buf), cBufSize);
	  if (res == cBufSize)
	    {
	      fullAddr += cBufSize;
	      buf += cBufSize;
	      bufSize = bufSize - cBufSize;
	    }
	  else
	    {
	      cerr << "Warning: Maximal write burst of " << cBufSize
		   << " bytes to address 0x" << hex << setw (8)
		   << setfill ('0') << fullAddr << " failed with result "
		   << setfill (' ') << setw (0) << dec << res << "." << endl;
	      return false;
	    }
	}

      // Remaining double word transfers
      size_t trailSize = bufSize % E_DOUBLE_BYTES;
      if (bufSize > trailSize)
	{
	  // Send the last double word packet
	  size_t lastDoubleSize = bufSize - trailSize;
	  if (si->debugTargetWr ())
	    {
	      cerr << "DebugTargetWr: Last double word write burst "
		   << "to full address 0x" << hex << setw (8) << setfill ('0')
		   << fullAddr << setfill (' ') << setw (0) << dec
		   << ", size " << lastDoubleSize << " bytes." << endl;
	    }

	  size_t res = writeTo (fullAddr, buf, lastDoubleSize);
	  if (res == lastDoubleSize)
	    {
	      fullAddr += lastDoubleSize;
	      buf += lastDoubleSize;
	    }
	  else
	    {
	      cerr << "Warning: Last double write burst of " << lastDoubleSize
		   << " bytes to address 0x" << hex << setw (8)
		   << setfill ('0') << fullAddr << " failed with result "
		   << setfill (' ') << setw (0) << dec << res << "." << endl;
	      return false;
	    }
	}

      // Final partial word
      for (unsigned n = 0; n < trailSize; n++)
	{
	  if (si->debugTargetWr ())
	    {
	      cerr << "DebugTargetWr: Write burst trail byte " << n
		   << " to 0x" << hex << setw (8) << setfill ('0') << fullAddr
		   << "." << setfill (' ') << setw (0) << dec << endl;
	    }

	  int res = writeTo (fullAddr, buf, 1);
	  if (res == 1)
	    {
	      buf++;
	      fullAddr++;
	    }
	  else
	    {
	      cerr << "Warning: Write burst of 1 trailer byte to address 0x"
		   << hex << setw (8) << setfill ('0') << fullAddr
		   << " failed with result " << setfill (' ') << setw (0)
		   << dec << res << "." << endl;
	      return  false;
	    }
	}

      return true;
    }
}	// writeBurst ()


//! initialize the attached core ID

//! Reset the platform
void
TargetControlHardware::platformReset ()
{
  hwReset ();			// ESYS_RESET

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

  // hwReset();

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


//! Return a vector of all the (relative) CoreIds we know about.
vector <uint16_t>
TargetControlHardware::listCoreIds ()
{
  return relCoreIds;

}	// listCoreIds ()


//! Return the number of rows
unsigned int
TargetControlHardware::getNumRows ()
{
  return  numRows;

}	// numRows ();


//! Return the number of columns
unsigned int
TargetControlHardware::getNumCols ()
{
  return  numCols;

}	// numCols ();


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
  *(void **) (&initPlatformFunc) = findSharedFunc ("esrv_init_platform");
  *(void **) (&closePlatformFunc) = findSharedFunc ("esrv_close_platform");
  *(void **) (&writeToFunc) = findSharedFunc ("esrv_write_to");
  *(void **) (&readFromFunc) = findSharedFunc ("esrv_read_from");
  *(void **) (&getDescriptionFunc) = findSharedFunc ("esrv_get_description");
  *(void **) (&hwResetFunc) = findSharedFunc ("esrv_hw_reset");

  // add signal handler to close target connection
  if (signal (SIGINT, breakSignalHandler) < 0)
    {
      cerr << "ERROR: Failed to register BREAK signal handler: "
	   << strerror (errno) << "." << endl;
      exit (EXIT_FAILURE);
    }

  // Initialize target platform.
  int res = initPlatform (platform, si->halDebug());

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
  else if (hwReset () != 0)
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
  // Clear everything
  numCores = 0;
  numRows = 0;
  numCols = 0;

  relCoreIds.clear ();
  coreMap.clear ();
  coreMemMap.clear ();
  reverseCoreMemMap.clear ();
  extMemSet.clear ();

  // Iterate through each core on each chip
  for (unsigned int  chipNum = 0; chipNum < platform->num_chips; chipNum++)
    {
      chip_def_t  chip = platform->chips[chipNum];

      numRows += chip.num_rows;	// This really only works for one chip
      numCols += chip.num_rows;

      for (unsigned int row = 0; row < chip.num_rows; row++)
	{
	  assert (row < (0x1 << 6));		// Max 6 bits

	  for (unsigned int col = 0; col < chip.num_cols; col++)
	    {
	      assert (col < (0x1 << 6));	// Max 6 bits

	      // Record the relative core ID.
	      uint16_t relId = (row << 6) | col;
	      relCoreIds.push_back (relId);

	      // Set up the relative to absolute core ID map entry
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
	       << hex << setw (8) << setfill ('0') << r.minAddr () << ", 0x"
	       << r.maxAddr () << "]." << dec << setfill (' ') << setw (0)
	       << endl;
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
      cout << "    memory range   [0x" << hex << setw (8) << setfill ('0')
	   << minAddr << ", 0x" << maxAddr << "]" << endl;
      cout << "    register range [0x" << minRegAddr << ", 0x" << maxRegAddr
	   << "]" << dec << setfill (' ') << setw (0) << endl;
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

      cout << "  [" << hex << setw (8) << setfill ('0') << minAddr << ", 0x"
	   << maxAddr << "]" << endl;
    }
}	// showMaps ()


string
TargetControlHardware::getTargetId ()
{
  char *targetId;
  getDescription (&targetId);

  return string (targetId);
}


//! Convert a local address to a global one.

//! If we have a local address, then convert it to a global address based on
//! the core to be targeted.

//! @param[in] relCoreId  Relative core ID of core we want the address for.
//! @param[in] address    The address to convert (may be local or global)
//! @return  The global address
uint32_t
TargetControlHardware::convertAddress (uint16_t relCoreId,
				       uint32_t address)
{
  uint16_t absCoreId = coreMap [relCoreId];

  if (address < CORE_MEM_SPACE)
    {
      return (((uint32_t) absCoreId) << 20) | (address & 0x000fffff);
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
	  uint16_t absRow = absCoreId >> 6;
	  uint16_t absCol = absCoreId & 0x3f;

	  cerr << "ERROR: core ID (" << absRow << ", " << absCol 
	       << "): invalid address 0x" << hex << setw (8) << setfill ('0') << address
	       << setfill (' ') << setw (0) << dec << "." << endl;
	  exit (EXIT_FAILURE);
	}
    }
  else
    return address;

}	// convertAddress ()


//! Wrapper for the dynamically linked platform initialization

//! @param[in] platform  The description of the platform
//! @param[in] verbose   The level of debug messages from HAL
//! @return  0 on success, 1 on error, 2 on warning.
int
TargetControlHardware::initPlatform (platform_definition_t* platform,
				     unsigned int           verbose)
{
  if (si->debugHwDetail ())
      cerr << "DebugHwDetail: initPlatform (" << (void *) platform << ", "
	   << verbose << ")" << endl;
  
  return (*initPlatformFunc) (platform, verbose);

}	// initPlatform ()


//! Wrapper for the dynamically linked platform close function

//! @return  0 on success, 1 on error, 2 on warning.
int
TargetControlHardware::closePlatform ()
{
  if (si->debugHwDetail ())
      cerr << "DebugHwDetail: closePlatform ()" << endl;
  
  return (*closePlatformFunc) ();

}	// closePlatform ()


//! Wrapper for the dynamically linked write to target

//! @param[in] address    The (global) address to write to.
//! @param[in] buf        The data to write.
//! @param[in] burstSize  The number of bytes to write.
//! @return  The number of bytes written.
size_t
TargetControlHardware::writeTo (unsigned int  address,
				void*         buf,
				size_t        burstSize)
{
  if (si->debugHwDetail ())
    cerr << "DebugHwDetail: writeTo (" << (void *) address << ", "
	 << (void *) buf << ", " << burstSize << ")" << endl;
  
  return (*writeToFunc) (address, buf, burstSize);

}	// writeTo ()


//! Wrapper for the dynamically linked read from target

//! @param[in]  address    The (global) address to read from.
//! @param[out] buf        The data read.
//! @param[in]  burstSize  The number of bytes to read.
//! @return  The number of bytes read.
size_t
TargetControlHardware::readFrom (unsigned  address,
				 void*     buf,
				 size_t    burstSize)
{
  if (si->debugHwDetail ())
      cerr << "DebugHwDetail: readFrom (" << (void *) address << ", "
	   << (void *) buf << ", " << burstSize << ")" << endl;

  return (*readFromFunc) (address, buf, burstSize);

}	// readFrom ()


//! Wrapper for the dynamically linked platform reset function

//! @return  0 on success, 1 on error, 2 on warning.
int
TargetControlHardware::hwReset ()
{
  if (si->debugHwDetail ())
      cerr << "DebugHwDetail: hwReset ()" << endl;

  return (*hwResetFunc) ();

}	// hwReset ()


//! Wrapper for the dynamically linked function to get the platform name.

//! @param[out] targetIdp  The platform name.
//! @return  0 on success, 1 on error, 2 on warning.
int
TargetControlHardware::getDescription (char** targetIdp)
{
  if (si->debugHwDetail ())
      cerr << "DebugHwDetail: getDescription (" << (void *) targetIdp << ")"
	   << endl;

  return (*getDescriptionFunc) (targetIdp);

}	// getDescription ()


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
