##############################################################################
## e-trace library Makefile
##
BUILD_ROOT  	= ../../..

LIBNAME			= e-trace

# <REQUIRED - for shared object versioning>
#LIBVERMAJ       = .1
#LIBVERMIN       = .0
#LIBVERBLD       = .0

# Libraries to link with
LIBS			= 

EXTRACFLAGS  	= -fmessage-length=0 -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast

# List of include search paths
INCLUDES    = \
	-I./include							\
	-I$(BUILD_ROOT)/src/e-loader/src	\
	-I$(BUILD_ROOT)/src/e-hal/src

C_SRCS      = \
	a_trace.c

include $(BUILD_ROOT)/build_tools/make_sharedlib.rules
