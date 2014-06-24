#BUILD_ROOT  	= $(EPIPHANY_HOME)/../
BUILD_ROOT  	= ../../..

EXENAME			= e-read
EXTRACFLAGS  	= -fmessage-length=0
EXTRALINKFLAGS  =

INCLUDES    = \
	-I../../e-hal/src 			\
	-I../../e-loader/src

LIB_PATHS   = \
	-L../../e-hal/Release 		\
    -L../../e-loader/Release

DBG_LIB_PATHS = \
	-L../../e-hal/Debug 		\
    -L../../e-loader/Debug

LIBS        = \
	-le-hal						\
	-le-loader


C_SRCS      = \
	e-read.c

# REQUIRED - Include the X3C rules for building executable binaries
include $(BUILD_ROOT)/build_tools/make_exe.rules
