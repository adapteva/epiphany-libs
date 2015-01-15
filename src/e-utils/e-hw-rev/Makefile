#BUILD_ROOT  	= $(EPIPHANY_HOME)/../
BUILD_ROOT  	= ../../..

EXENAME		= e-hw-rev
EXTRACXXFLAGS	=
EXTRALINKFLAGS	= -fmessage-length=0

INCLUDES	= -I../../e-hal/src
LIB_PATHS	= -L../../e-hal/Release

DBG_LIB_PATHS	= -L../../e-hal/Debug

LIBS		= -le-hal

C_SRCS		= e-hw-rev.c

all: symlink_exe

.PHONY: symlink_exe
symlink_exe:
	ln -sf Debug/$(EXENAME)


# REQUIRED - Include the X3C rules for building executable binaries
include $(BUILD_ROOT)/build_tools/make_exe.rules
