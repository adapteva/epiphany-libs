#BUILD_ROOT  	= $(EPIPHANY_HOME)/../
BUILD_ROOT  	= ../..

LIBNAME			= e-xml

# <REQUIRED - for shared object versioning
# LIBVERMAJ       = .1
# LIBVERMIN       = .0
# LIBVERBLD       = .0

EXTRACXXFLAGS  	= -fmessage-length=0 -DPIC -Wno-write-strings -DLINUX -DUNIX

# List of include search paths
INCLUDES    = \
	-I../e-hal/src				\
	-I../e-loader/src

SRCS      = \
	epiphany_xml.cpp 			\
	parse.cpp 					\
	xml.cpp 


include $(BUILD_ROOT)/build_tools/make_sharedlib.rules


CC=arm-linux-gnueabihf-g++
