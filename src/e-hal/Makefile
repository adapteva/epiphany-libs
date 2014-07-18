##############################################################################
## e-hal Makefile
##
## NOTE: the environment variable PARALLELLA_LINUX_HOME must be defined and
## must point to the parrallella-linux-adi source root in order to pickup
## the Epiphany driver include files 
BUILD_ROOT  	= ../..

LIBNAME			= e-hal

# <REQUIRED - for shared object versioning
#LIBVERMAJ       = .1
#LIBVERMIN       = .0
#LIBVERBLD       = .0

# Libraries to link with
LIBS			= 

EXTRACFLAGS  	= -fmessage-length=0 -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast

# List of include search paths
INCLUDES    = \
	-I../						\
	-I./src						\
	-I../e-loader/src			\
	-I$(PARALLELLA_LINUX_HOME)/include/uapi

C_SRCS      = \
	epiphany-shm-manager.c		\
	epiphany-hal-legacy.c 		\
	epiphany-hal.c

include $(BUILD_ROOT)/build_tools/make_sharedlib.rules
