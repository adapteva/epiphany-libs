// Ios utils.

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

#ifndef IOSUTILS_H
#define IOSUTILS_H

#include <iostream>

// RAII class that saves/restores a stream's format control flags.
// Not unlike boost::io::ios_flags_saver.

class IosFlagsSaver
{
public:
  explicit IosFlagsSaver (std::ostream& ios)
    : mIos (ios), mFlags (ios.flags ())
  {}

  ~IosFlagsSaver()
  { mIos.flags (mFlags); }

  /* Not implemented / deleted.  */
  IosFlagsSaver (const IosFlagsSaver &rhs);
  IosFlagsSaver& operator= (const IosFlagsSaver& rhs);

private:
  std::ostream& mIos;
  std::ios::fmtflags mFlags;
};

#endif

// Local Variables:
// mode: C++
// c-file-style: "gnu"
// show-trailing-whitespace: t
// End:
