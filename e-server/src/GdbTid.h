// GDB RSP tid class.

// Copyright (C) 2016, Pedro Alves

// Contributor: Pedro Alves <pedro@palves.net>

// This file is part of the Adapteva RSP server.

// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.

// You should have received a copy of the GNU General Public License along
// with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef GDB_TID_H
#define GDB_TID_H

#include <sstream>

class ProcessInfo;
class Thread;

class GdbTid
{
public:
  GdbTid()
    : mPid(-1), mTid(0)
  {}

  GdbTid (int pid, int tid)
    : mPid (pid), mTid (tid)
  {}

  GdbTid (ProcessInfo *process, Thread *thread);

  int pid () const { return mPid; }
  int tid () const { return mTid; }

  static GdbTid fromString (const char *str);

  friend std::ostream& operator<< (std::ostream &out, const GdbTid &gtid);

  static const GdbTid ALL_THREADS;

private:
  int mPid;
  int mTid;
};

#endif

// Local Variables:
// mode: C++
// c-file-style: "gnu"
// show-trailing-whitespace: t
// End:
