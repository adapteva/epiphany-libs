#BUILD_ROOT  	= $(EPIPHANY_HOME)/../
BUILD_ROOT  	= ../..

LIBNAME			= e-hal

## Override CROSS_COMPILE on command line for a different cross-prefix
CROSS_COMPILE=arm-linux-gnueabihf-

# <REQUIRED - for shared object versioning
#LIBVERMAJ       = .1
#LIBVERMIN       = .0
#LIBVERBLD       = .0

# Libraries to link with
LIBS			= 

EXTRACFLAGS  	= -fmessage-length=0 -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast

# List of include search paths
INCLUDES    = \
	-I../						\
	-I./src						\
	-I../e-loader/src

C_SRCS      = \
	epiphany-hal-legacy.c 		\
	epiphany-hal.c

include $(BUILD_ROOT)/build_tools/make_sharedlib.rules
