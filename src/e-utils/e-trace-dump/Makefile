BUILD_ROOT      = ../../..

EXENAME         = e-trace-dump
EXTRACFLAGS     = -fmessage-length=0
EXTRALINKFLAGS  =

INCLUDES    = \
    -I./include                 	  					\
	-I$(BUILD_ROOT)/src/e-utils/e-trace/include	        \
	-I$(BUILD_ROOT)/src/e-loader/src					\
	-I$(BUILD_ROOT)/src/e-hal/src

LIB_PATHS   = \
	-L$(BUILD_ROOT)/src/e-utils/e-trace/Release			\
	-L$(BUILD_ROOT)/src/e-loader/Release				\
	-L$(BUILD_ROOT)/src/e-hal/Release

DBG_LIB_PATHS = \
	-L$(BUILD_ROOT)/src/e-utils/e-trace/Debug			\
	-L$(BUILD_ROOT)/src/e-loader/Debug					\
	-L$(BUILD_ROOT)/src/e-hal/Debug

LIBS        = \
	-le-trace											\
	-le-loader											\
	-le-hal												\
	-lpthread

C_SRCS      = \
    a_trace_bin_to_txt.c


# Include the X3C rules for building executable binaries
include $(BUILD_ROOT)/build_tools/make_exe.rules
