// Server information class: Declaration

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

#ifndef SERVER_INFO__H
#define SERVER_INFO__H

#include <cstdio>
#include "e-hal/src/epiphany-hal-data.h"


//! A class for carrying around general information about the GDB server.
class ServerInfo
{
public:

  // Constructor and destructor
  ServerInfo ();
  ~ServerInfo ();

  // Functions to access info
  void  hdfFile (const char* _hdfFileName);
  const char* hdfFile () const;
  void  ttyOut (FILE* _ttyOutHandle);
  FILE* ttyOut () const;
  void  port (unsigned int _portNum);
  unsigned int  port () const;
  bool  validPort () const;
  void  halDebug (e_hal_diag_t  h_alDebugLevel);
  e_hal_diag_t halDebug () const;
    
  // Debug level accessors
  void  debugStopResume (const bool  enable);
  bool  debugStopResume () const;
  void  debugTrapAndRspCon (const bool  enable);
  bool  debugTrapAndRspCon () const;
  void  debugStopResumeDetail (const bool  enable);
  bool  debugStopResumeDetail () const;
  void  debugTargetWr (const bool  enable);
  bool  debugTargetWr () const;
  void  debugCtrlCWait (const bool  enable);
  bool  debugCtrlCWait () const;
  void  debugTranDetail (const bool  enable);
  bool  debugTranDetail () const;
  void  debugHwDetail (const bool  enable);
  bool  debugHwDetail () const;

  // Flag accessors
  void showMemoryMap (const bool _showMemoryMapFlag);
  bool showMemoryMap () const;
  void skipPlatformReset (const bool _skipPlatformResetFlag);
  bool skipPlatformReset () const;
  void checkHwAddr (const bool _checkHwAddrFlag);
  bool checkHwAddr () const;
  void haltOnAttach (const bool _haltOnAttachFlag);
  bool haltOnAttach () const;

private:

  // Debug flag bits
  static const unsigned int DEBUG_NONE               = 0x0000;
  static const unsigned int DEBUG_STOP_RESUME        = 0x0001;
  static const unsigned int DEBUG_TRAP_AND_RSP_CON   = 0x0002;
  static const unsigned int DEBUG_STOP_RESUME_DETAIL = 0x0004;
  static const unsigned int DEBUG_TARGET_WR          = 0x0008;
  static const unsigned int DEBUG_CTRL_C_WAIT        = 0x0010;
  static const unsigned int DEBUG_TRAN_DETAIL        = 0x0020;
  static const unsigned int DEBUG_HW_DETAIL          = 0x0040;

  //! Maximum permissible port number
  static const unsigned int MAX_PORT_NUM = 0xffff;

  //! Default port
  static const unsigned int DEFAULT_RSP_PORT = 51000;

  //! The name of the HDF file or NULL if not set
  const char* hdfFileName;

  //! The TTY output handle or NULL if not set
  FILE* ttyOutHandle;

  //! The port number to communicate on. Must be in the range 1 to 65535 to be
  //! valid.
  unsigned int  portNum;

  //! The debug flags
  unsigned int  debugFlags;

  //! HAL debug level
  e_hal_diag_t  halDebugLevel;

  // Command line flags
  bool showMemoryMapFlag;		//!< Show memory and register maps
  bool skipPlatformResetFlag;           //!< Don't reset on init
  bool checkHwAddrFlag;			//!< Check HW address when used
  bool haltOnAttachFlag;		//!< Don't halt processor when attaching

};	// ServerInfo

#endif	// SERVER_INFO__H


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
