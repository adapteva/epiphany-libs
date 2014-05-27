#BUILD_ROOT  	= $(EPIPHANY_HOME)/../
BUILD_ROOT  	= ../..

EXENAME			= e-server

## Override CROSS_COMPILE on command line for a different cross-prefix
CROSS_COMPILE=arm-linux-gnueabihf-

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
	-I../e-loader/src

SRCS      = \
	e_printf.cpp			\
	GdbserverCreate.cpp		\
	GdbServerSC.cpp			\
	main.cpp				\
	MpHash.cpp				\
	RspConnection.cpp		\
	RspPacket.cpp			\
	targetCntrlHardware.cpp	\
	Utils.cpp

include $(BUILD_ROOT)/build_tools/make_exe.rules
