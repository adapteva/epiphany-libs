##############################################################################
## memman Makefile
##
BUILD_ROOT  	= ../..

LIBNAME			= memman

# Libraries to link with
LIBS			= 

EXTRACFLAGS  	= -fmessage-length=0 -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -fPIC

# List of include search paths
INCLUDES    = \
	-I./include

C_SRCS      = \
	memman.c

include $(BUILD_ROOT)/build_tools/make_lib.rules
