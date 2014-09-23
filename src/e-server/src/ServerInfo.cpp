// Server information class: Definition

// This file is part of the Epiphany Software Development Kit.

// Copyright (C) 2013-2014 Adapteva, Inc.

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

#include <iostream>

#include "ServerInfo.h"


using std::cerr;
using std::endl;


//! Constructor.

//! Initialize all the values to defaults
ServerInfo::ServerInfo () :
  hdfFileName (NULL),
  ttyOutHandle (NULL),
  portNum (ServerInfo::DEFAULT_RSP_PORT),
  debugFlags (DEBUG_NONE),
  halDebugLevel (H_D0),
  showMemoryMapFlag (false),
  skipPlatformResetFlag (false),
  checkHwAddrFlag (false),
  haltOnAttachFlag (true)
{
}	// ServerInfo ()


//! Destructor
ServerInfo::~ServerInfo ()
{
}	// ~ServerInfo ()


//! Set the HDF file name
void
ServerInfo::hdfFile (const char *_hdfFileName)
{
  hdfFileName = _hdfFileName;

}	// hdfFile ()


//! Get the HDF file name
const char*
ServerInfo::hdfFile () const
{
  return  hdfFileName;

}	// hdfFile ()


//! Set the TTY output handle
void
ServerInfo::ttyOut (FILE *_ttyOutHandle)
{
  ttyOutHandle = _ttyOutHandle;

}	// ttyOut ()


//! Get the TTY output handle
FILE*
ServerInfo::ttyOut () const
{
  return ttyOutHandle;

}	// ttyOut ()


//! Set the port number
void
ServerInfo::port (unsigned int  _portNum)
{
  portNum = _portNum;

}	// port ()


//! Get the port number
unsigned int
ServerInfo::port () const
{
  return portNum;

}	// port ()


//! Do we have a valid port number
bool
ServerInfo::validPort () const
{
  return (0 < portNum) && (portNum <= MAX_PORT_NUM);

}	// validPort ()


//! Set the HAL debug level, with warning
void
ServerInfo::halDebug (e_hal_diag_t  _halDebugLevel)
{
  switch (_halDebugLevel)
    {
    case H_D0:
    case H_D1:
    case H_D2:
    case H_D3:
    case H_D4:
      halDebugLevel = _halDebugLevel;
      return;

    default:
      cerr << "Warning: HAL debug level must in the the range " << H_D0
	   << " to " << H_D4 << ". Set to " << H_D4 << "." << endl;
      halDebugLevel = H_D4;
    }
}	// halDebug ()


//! Get the HAL debug level
e_hal_diag_t
ServerInfo::halDebug () const
{
  return halDebugLevel;

}	// halDebug ()


//! Enable or disable the stop/resume debug flag
void
ServerInfo::debugStopResume (const bool  enable)
{
  if (enable)
    debugFlags |= ServerInfo::DEBUG_STOP_RESUME;
  else
    debugFlags &= ~ServerInfo::DEBUG_STOP_RESUME;

}	// debugStopResume ()


//! Determine if stop/resume debug is enabled.
bool
ServerInfo::debugStopResume () const
{
  return (debugFlags & ServerInfo::DEBUG_STOP_RESUME)
    == ServerInfo::DEBUG_STOP_RESUME;

}	// debugStopResume ()


//! Enable or disable the trap/RSP continue debug flag
void
ServerInfo::debugTrapAndRspCon (const bool  enable)
{
  if (enable)
    debugFlags |= ServerInfo::DEBUG_TRAP_AND_RSP_CON;
  else
    debugFlags &= ~ServerInfo::DEBUG_TRAP_AND_RSP_CON;

}	// debugTrapAndRspCon ()


//! Determine if trap/RSP continue debug is enabled.
bool
ServerInfo::debugTrapAndRspCon () const
{
  return (debugFlags & ServerInfo::DEBUG_TRAP_AND_RSP_CON)
    == ServerInfo::DEBUG_TRAP_AND_RSP_CON;

}	// debugTrapAndRspCon ()


//! Enable or disable the stop/resume detail debug flag
void
ServerInfo::debugStopResumeDetail (const bool  enable)
{
  if (enable)
    debugFlags |= ServerInfo::DEBUG_STOP_RESUME_DETAIL;
  else
    debugFlags &= ~ServerInfo::DEBUG_STOP_RESUME_DETAIL;

}	// debugStopResumeDetail ()


//! Determine if stop/resume detail debug is enabled.
bool
ServerInfo::debugStopResumeDetail () const
{
  return (debugFlags & ServerInfo::DEBUG_STOP_RESUME_DETAIL)
    == ServerInfo::DEBUG_STOP_RESUME_DETAIL;

}	// debugStopResumeDetail ()


//! Enable or disable the target write debug flag
void
ServerInfo::debugTargetWr (const bool  enable)
{
  if (enable)
    debugFlags |= ServerInfo::DEBUG_TARGET_WR;
  else
    debugFlags &= ~ServerInfo::DEBUG_TARGET_WR;

}	// debugTargetWr ()


//! Determine if target write debug is enabled.
bool
ServerInfo::debugTargetWr () const
{
  return (debugFlags & ServerInfo::DEBUG_TARGET_WR)
    == ServerInfo::DEBUG_TARGET_WR;

}	// debugTargetWr ()


//! Enable or disable the ctrl-C/wait debug flag
void
ServerInfo::debugCtrlCWait (const bool  enable)
{
  if (enable)
    debugFlags |= ServerInfo::DEBUG_CTRL_C_WAIT;
  else
    debugFlags &= ~ServerInfo::DEBUG_CTRL_C_WAIT;

}	// debugCtrlCWait ()


//! Determine if ctrl-C/wait debug is enabled.
bool
ServerInfo::debugCtrlCWait () const
{
  return (debugFlags & ServerInfo::DEBUG_CTRL_C_WAIT)
    == ServerInfo::DEBUG_CTRL_C_WAIT;

}	// debugCtrlCWait ()


//! Enable or disable the transaction detail debug flag
void
ServerInfo::debugTranDetail (const bool  enable)
{
  if (enable)
    debugFlags |= ServerInfo::DEBUG_TRAN_DETAIL;
  else
    debugFlags &= ~ServerInfo::DEBUG_TRAN_DETAIL;

}	// debugTranDetail ()


//! Determine if transaction detail debug is enabled.
bool
ServerInfo::debugTranDetail () const
{
  return (debugFlags & ServerInfo::DEBUG_TRAN_DETAIL)
    == ServerInfo::DEBUG_TRAN_DETAIL;

}	// debugTranDetail ()


//! Enable or disable the hardware detail debug flag
void
ServerInfo::debugHwDetail (const bool  enable)
{
  if (enable)
    debugFlags |= ServerInfo::DEBUG_HW_DETAIL;
  else
    debugFlags &= ~ServerInfo::DEBUG_HW_DETAIL;

}	// debugHwDetail ()


//! Determine if hardware detail debug is enabled.
bool
ServerInfo::debugHwDetail () const
{
  return (debugFlags & ServerInfo::DEBUG_HW_DETAIL)
    == ServerInfo::DEBUG_HW_DETAIL;

}	// debugHwDetail ()


//! Enable or disable the timing debug flag
void
ServerInfo::debugTiming (const bool  enable)
{
  if (enable)
    debugFlags |= ServerInfo::DEBUG_HW_DETAIL;
  else
    debugFlags &= ~ServerInfo::DEBUG_HW_DETAIL;

}	// debugTiming ()


//! Determine if timing debug is enabled.
bool
ServerInfo::debugTiming () const
{
  return (debugFlags & ServerInfo::DEBUG_HW_DETAIL)
    == ServerInfo::DEBUG_HW_DETAIL;

}	// debugTiming ()


//! Set the show memory map flag
void
ServerInfo::showMemoryMap (const bool _showMemoryMapFlag)
{
  showMemoryMapFlag = _showMemoryMapFlag;

}	// showMemoryMap ()


//! Get the show memory map flag
bool
ServerInfo::showMemoryMap () const
{
  return  showMemoryMapFlag;

}	// showMemoryMap ()


//! Set the skip platform reset flag
void
ServerInfo::skipPlatformReset (const bool _skipPlatformResetFlag)
{
  skipPlatformResetFlag = _skipPlatformResetFlag;

}	// skipPlatformReset ()


//! Get the skip platform reset flag
bool
ServerInfo::skipPlatformReset () const
{
  return  skipPlatformResetFlag;

}	// skipPlatformReset ()


//! Set the check hardware address flag
void
ServerInfo::checkHwAddr (const bool _checkHwAddrFlag)
{
  checkHwAddrFlag = _checkHwAddrFlag;

}	// checkHwAddr ()


//! Get the check hardware addressflag
bool
ServerInfo::checkHwAddr () const
{
  return  checkHwAddrFlag;

}	// checkHwAddr ()


//! Set the halt on attach flag
void
ServerInfo::haltOnAttach (const bool _haltOnAttachFlag)
{
  haltOnAttachFlag = _haltOnAttachFlag;

}	// haltOnAttach ()


//! Get the halt on attach flag
bool
ServerInfo::haltOnAttach () const
{
  return  haltOnAttachFlag;

}	// haltOnAttach ()


//! Set the chip version
void
ServerInfo::chipVersion (const int  version)
{
  mChipVersion = version;

}	// chipVersion ()


//! Get the chip version
int
ServerInfo::chipVersion (void) const
{
  return  mChipVersion;

}	// chipVersion ()


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// show-trailing-whitespace: t
// End:
