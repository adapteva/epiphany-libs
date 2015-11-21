#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.


# Needed for autoconf 2.64
#m4_pattern_allow([AC_CHECK_HEADER_STDBOOL])

AC_PREREQ([2.64])
AC_INIT([e-server: Epiphany Remote GDB Server], [0.1],
        [https://github.com/adapteva/epiphany-sdk/issues], [e-server],
        [https://github.com/adapteva/epiphany-libs])

AC_CONFIG_SRCDIR([src/GdbServer.h])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_AUX_DIR([build-aux])

AC_CONFIG_MACRO_DIR([m4])


# Support cross compilation
AC_CANONICAL_BUILD
AC_CANONICAL_HOST


# Initialize automake
AM_INIT_AUTOMAKE([subdir-objects -Wall -Werror])


# Support silent build rules. Disable by either passing --disable-silent-rules
# to configure or passing V=1 to make.
AM_SILENT_RULES([yes])


# Checks for programs.
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_CC_C99
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
# AC_PROG_RANLIB # No: is rendered obsolete by `LT_INIT'
AM_PROG_CC_C_O
AM_PROG_AR


# Initialize libtool
LT_INIT


# Checks for libraries.

# (none needed)


# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h netdb.h netinet/in.h stdint.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])


# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_TYPE_INT32_T
AC_TYPE_SIZE_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T


# Checks for library functions.
#AC_FUNC_MALLOC # Don't do it. Assume GNU libc compatible malloc.
AC_CHECK_FUNCS([gettimeofday inet_ntoa memchr realpath socket strcasecmp strerror strtol strtoul])


AC_CONFIG_FILES([Makefile])


# Define the --enable-assert command-line option
AC_ARG_ENABLE(assert,
  [AS_HELP_STRING([--enable-assert], [Build with assertions enabled])],
  [enable_assert=yes], [enable_assert=no])
# Set any CPPFLAGS specific to debugging
AS_IF([test "x$enable_assert" = "xyes"],
  [AC_SUBST(assert_CPPFLAGS, [''])],
  [AC_SUBST(assert_CPPFLAGS, ['-DNDEBUG'])])

# Easiest way to include header files globally
AC_SUBST([AM_CPPFLAGS],
  ['-I$(top_srcdir)/src -I$(top_srcdir)/../e-hal/src -I$(top_srcdir)/../e-xml/src -I$(top_builddir) -I$(top_srcdir)/.. $(assert_CPPFLAGS)'])

AC_SUBST([AM_LDFLAGS])

AC_OUTPUT
