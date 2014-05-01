#BUILD_ROOT  	= $(EPIPHANY_HOME)/../
BUILD_ROOT  	= ../../..

EXENAME			= e-read
EXTRACFLAGS  	= -fmessage-length=0
EXTRALINKFLAGS  =

INCLUDES    = \
	-I../../e-hal/src		\
	-I../../e-loader/src

LIB_PATHS   = \
	-L../../e-hal/Release

LIBS        = \
	-le-hal

C_SRCS      = \
	e-reset.c

# REQUIRED - Include the X3C rules for building executable binaries
include $(BUILD_ROOT)/build_tools/make_exe.rules
