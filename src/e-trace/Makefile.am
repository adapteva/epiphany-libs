AUTOMAKE_OPTIONS = foreign

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST =                            \
src/e-trace-dma.c

DEP_LIBS =                              \
$(top_builddir)/../e-hal/libe-hal.la    \
$(top_builddir)/../e-hal/libe-loader.la

include_HEADERS = include/e_trace.h

lib_LTLIBRARIES = libe-trace.la

libe_trace_la_SOURCES = src/e_trace.c
libe_trace_la_LIBADD  = $(DEP_LIBS)

bin_PROGRAMS =                          \
e-trace-server                          \
e-trace-dump

e_trace_server_SOURCES = src/e-trace-server.c
e_trace_dump_SOURCES   = src/e-trace-dump.c

e_trace_server_LDADD   = libe-trace.la
e_trace_dump_LDADD     = libe-trace.la
