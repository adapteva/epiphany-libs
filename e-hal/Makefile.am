AUTOMAKE_OPTIONS = foreign

ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libe-loader.la libe-hal.la

libe_loader_la_SOURCES =      \
src/e-loader.c                \
src/e-loader.h                \
src/e-process-SREC.c

libe_hal_la_SOURCES =         \
src/e-hal.h                   \
src/epiphany-hal-api.h        \
src/epiphany-hal-api-local.h  \
src/epiphany-hal.c            \
src/epiphany-hal-data.h       \
src/epiphany-hal-data-local.h \
src/epiphany-hal.h            \
src/epiphany-hal-legacy.c     \
src/epiphany-memman.c         \
src/epiphany-shm-manager.c    \
src/epiphany-shm-manager.h    \
src/memman.h
libe_hal_la_LIBADD = libe-loader.la

#libe_hal_la_LIBADD = -lm

#lib_e_hal_la_LDFLAGS = -export-symbols-regex '^p_|^__pal_'
