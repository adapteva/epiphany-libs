BUILD_ROOT      = ../../..

EXENAME         = e-trace-server
EXTRACFLAGS     = -fmessage-length=0
EXTRALINKFLAGS  =

INCLUDES    = \
    -I./include                   		\
	-I$(BUILD_ROOT)/src/e-hal/src		\
	-I$(BUILD_ROOT)/src/e-loader/src	\
	-I$(BUILD_ROOT)/src/e-lib/include	\
	-I$(BUILD_ROOT)/src/e-utils/e-trace/include

LIB_PATHS   = \
    -L../../e-hal/Release       \
    -L../../e-loader/Release	\
	-L../../e-utils/e-trace/Release

DBG_LIB_PATHS = \
    -L../../e-hal/Debug         	\
    -L../../e-loader/Debug			\
	-L../../e-utils/e-trace/Debug

LIBS        = \
	-le-trace					\
    -le-hal                     \
	-le-loader					\
    -lpthread

C_SRCS      = \
    a_trace_server.c

## Unused sources
#    a_trace_dma.c				\

# Include the X3C rules for building executable binaries
include $(BUILD_ROOT)/build_tools/make_exe.rules
