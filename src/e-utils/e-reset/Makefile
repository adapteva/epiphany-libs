#BUILD_ROOT  	= $(EPIPHANY_HOME)/../
BUILD_ROOT  	= ../../..

EXENAME			= e-reset
EXTRACFLAGS  	= -fmessage-length=0
EXTRALINKFLAGS  =

## Override CROSS_COMPILE on command line for a different cross-prefix
CROSS_COMPILE=arm-linux-gnueabihf-

INCLUDES    = \
	-I../../e-hal/src		\
	-I../../e-loader/src

LIB_PATHS   = \
	-L../../e-hal/Release

DBG_LIB_PATHS = \
	-L../../e-hal/Debug

LIBS        = \
	-le-hal

C_SRCS      = \
	e-reset.c

all: symlink_exe

.PHONY: symlink_exe
symlink_exe: 
	ln -sf Debug/$(EXENAME) 

# REQUIRED - Include the X3C rules for building executable binaries
include $(BUILD_ROOT)/build_tools/make_exe.rules
