/* Abstract class for control of target: Declaration

  This file is part of the Epiphany Software Development Kit.

  Copyright (C) 2013-2014 Adapteva, Inc.

  Contributor: Oleg Raikhman <support@adapteva.com>
  Contributor: Yaniv Sapir <support@adapteva.com>
  Contributor: Jeremy Bennett <jeremy.bennett@embecosm.com>

  This program is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation, either version 3 of the License, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program (see the file COPYING).  If not, see
  <http://www.gnu.org/licenses/>.  */

#include "TargetControl.h"


//! Constructor

//! Set a default start time value, so endOfBaudMeasurement will return a
//! reasonable value.
TargetControl::TargetControl ()
{
  startOfBaudMeasurement ();

}	// TargetControl ()


//! Destructor

//! Default does nothing
TargetControl::~TargetControl ()
{
}	// ~TargetControl ()


//! Reset the platform

//! Default implementation does nothing.
void
TargetControl::platformReset ()
{
}	// platformReset ()


//! Utility to start timing
void
TargetControl::startOfBaudMeasurement ()
{
  gettimeofday (&startTime, NULL);

}	// startOfBaudMeasurement ()


//! Utility to end timing

//! @todo The original code added 0.5 milliseconds, presumably as some
//!       perceived rounding. However that makes an assumption about the type
//!       of tv_usec being an integral truncated to the nearest millisecond,
//!       which is not defined to be the case.

//! @return Time since startOfBaudMeasurement () in milliseconds.
double
TargetControl::endOfBaudMeasurement ()
{
  struct timeval endTime;
  struct timeval diffTime;

  gettimeofday (&endTime, NULL);
  timersub (&endTime, &startTime, &diffTime);

  startOfBaudMeasurement ();		// Reset, ready for next time

  return ((double) (diffTime.tv_sec)) * 1000.0
    + ((double) (diffTime.tv_usec)) / 1000.0;

}	// endOfBaudMeasurement


//! Is this a valid address in the Epiphany address space?

//! @param[in] addr  Address
//! @return  true if addr is a valid Epiphany address, false otherwise.

bool
TargetControl::isValidAddr (uint32_t  addr) const
{
  return (isCoreMem (addr) || isExternalMem (addr));
}	// isValidAddr ();
