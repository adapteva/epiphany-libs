# Copyright (C) 2010-2014 Adapteva Inc
# Copyright (C) 2014 Embecosm Limited

# Contributor Oleg Rhaikonen, Adapteva
# Contributor Yaniv Sapir, Adapteva
# Contributor Jeremy Bennett <jeremy.bennett@embecosm.com>
# Contributor Ben Chaco <bchaco@x3-c.com>

# This file is part of the Embecosm build system for Epiphany

# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 3 of the License, or (at your option)
# any later version.

# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.

# You should have received a copy of the GNU General Public License along
# with this program.  If not, see <http://www.gnu.org/licenses/>.

#BUILD_ROOT  	= $(EPIPHANY_HOME)/../
BUILD_ROOT  	= ../..

EXENAME			= e-server

# <REQUIRED - for shared object versioning
#LIBVERMAJ       = .1
#LIBVERMIN       = .0
#LIBVERBLD       = .0

LIB_PATHS       = \
	-L../e-xml/Release

DBG_LIB_PATHS = \
	-L../e-xml/Debug

# Libraries to link with
LIBS			= \
	-lpthread			\
	-ldl				\
	-le-xml

EXTRACFLAGS  	= -fmessage-length=0

# List of include search paths
INCLUDES    = \
	-I../						\
	-I../e-hal/src				\
	-I../e-loader/src \
	-I../e-xml/src

SRCS      = \
        CoreId.cpp \
        GdbServer.cpp \
        MemRange.cpp \
        MpHash.cpp \
        ProcessInfo.cpp \
        RspConnection.cpp \
        RspPacket.cpp \
        ServerInfo.cpp \
        TargetControl.cpp \
        TargetControlHardware.cpp \
        Thread.cpp \
        Utils.cpp \
        main.cpp

include $(BUILD_ROOT)/build_tools/make_exe.rules
