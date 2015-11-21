AUTOMAKE_OPTIONS = foreign

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST =                            \
archive/e-eclipse                       \
archive/e-objcopy

LIBS =                                  \
$(top_builddir)/../e-hal/libe-hal.la    \
$(top_builddir)/../e-hal/libe-loader.la

bin_PROGRAMS =                          \
e-clear-shmtable                        \
e-hw-rev                                \
e-loader                                \
e-read                                  \
e-reset                                 \
e-write

e_clear_shmtable_SOURCES = src/e-clear-shmtable.c
e_hw_rev_SOURCES         = src/e-hw-rev.c
e_loader_SOURCES         = src/e-loader.c
e_read_SOURCES           = src/e-read.c
e_reset_SOURCES          = src/e-reset.c
e_write_SOURCES          = src/e-write.c

e_clear_shmtable_LDADD   = $(LIBS)
e_hw_rev_LDADD           = $(LIBS)
e_loader_LDADD           = $(LIBS)
e_read_LDADD             = $(LIBS)
e_reset_LDADD            = $(LIBS)
e_write_LDADD            = $(LIBS)
