#BUILD_ROOT  	= $(EPIPHANY_HOME)/../
BUILD_ROOT  	= ../..

LIBNAME			= e-loader

# <REQUIRED - for shared object versioning
# LIBVERMAJ       = .1
# LIBVERMIN       = .0
# LIBVERBLD       = .0

# Libraries to link with
LIBS			= 

EXTRACFLAGS  	= -fmessage-length=0 -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast

# List of include search paths
INCLUDES    = \
	-I../										\
	-I../e-hal/src								\
	-I../e-loader/src							\
	-I$(PARALLELLA_LINUX_HOME)/include/uapi

C_SRCS      = \
	e-loader.c			\
	e-process-SREC.c

include $(BUILD_ROOT)/build_tools/make_sharedlib.rules
