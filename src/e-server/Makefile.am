AUTOMAKE_OPTIONS = foreign

ACLOCAL_AMFLAGS = -I m4

LIBS =                                  \
$(top_builddir)/../e-hal/libe-hal.la    \
$(top_builddir)/../e-hal/libe-loader.la \
$(top_builddir)/../e-xml/libe-xml.la

bin_PROGRAMS = e-server

e_server_SOURCES =                      \
src/CoreId.cpp                          \
src/CoreId.h                            \
src/GdbServer.cpp                       \
src/GdbServer.h                         \
src/libgloss_syscall.h                  \
src/maddr_defs.h                        \
src/main.cpp                            \
src/MemRange.cpp                        \
src/MemRange.h                          \
src/MpHash.cpp                          \
src/MpHash.h                            \
src/ProcessInfo.cpp                     \
src/ProcessInfo.h                       \
src/RspConnection.cpp                   \
src/RspConnection.h                     \
src/RspPacket.cpp                       \
src/RspPacket.h                         \
src/ServerInfo.cpp                      \
src/ServerInfo.h                        \
src/TargetControl.cpp                   \
src/TargetControl.h                     \
src/TargetControlHardware.cpp           \
src/TargetControlHardware.h             \
src/Thread.cpp                          \
src/Thread.h                            \
src/Utils.cpp                           \
src/Utils.h

e_server_LDADD   = -ldl $(LIBS)
