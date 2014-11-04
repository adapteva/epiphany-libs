BUILD_ROOT      = ../../..

EXENAME         = e-clear-shmtable
EXTRACFLAGS     = -fmessage-length=0
EXTRALINKFLAGS  =

INCLUDES    = \
	-I$(BUILD_ROOT)/src/e-hal/src

LIB_PATHS   = \
    -L../../e-hal/Release

DBG_LIB_PATHS = \
    -L../../e-hal/Debug

LIBS        = \
	-le-hal		\
    -lpthread

C_SRCS      = \
    clear_shmtable.c

# Include the X3C rules for building executable binaries
include $(BUILD_ROOT)/build_tools/make_exe.rules
