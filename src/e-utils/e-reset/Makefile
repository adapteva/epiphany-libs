BUILD_ROOT  	= ../../..

EXENAME			= e-reset
EXTRACFLAGS  	= -fmessage-length=0
EXTRALINKFLAGS  =

INCLUDES    = \
	-I../../e-hal/src							\
	-I../../e-loader/src						\
	-I$(PARALLELLA_LINUX_HOME)/include/uapi

LIB_PATHS   = \
	-L../../e-hal/Release

DBG_LIB_PATHS = \
	-L../../e-hal/Debug

LIBS        = \
	-le-hal		\
	-lpthread

C_SRCS      = \
	e-reset.c

all: symlink_exe

.PHONY: symlink_exe
symlink_exe: 
	ln -sf Debug/$(EXENAME) 

# REQUIRED - Include the X3C rules for building executable binaries
include $(BUILD_ROOT)/build_tools/make_exe.rules
