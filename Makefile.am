AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS  = -I m4

SUBDIRS = e-lib

bspsdir = @bspsdir@

EXTRA_DIST       =
lib_LTLIBRARIES  =
bin_PROGRAMS     =
include_HEADERS  =
noinst_HEADERS   =

include e-hal/Makemodule.am
include e-server/Makemodule.am
include e-trace/Makemodule.am
include e-utils/Makemodule.am
include e-xml/Makemodule.am
include bsps/Makemodule.am

#libe_hal_la_LIBADD = -lm

#lib_e_hal_la_LDFLAGS = -export-symbols-regex '^p_|^__pal_'
