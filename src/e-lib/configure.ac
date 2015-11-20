#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.


# Needed for autoconf 2.64
#m4_pattern_allow([AC_CHECK_HEADER_STDBOOL])

AC_PREREQ([2.64])
AC_INIT([e-lib: Epiphany Device Library], [0.1],
        [https://github.com/adapteva/epiphany-sdk/issues], [e-lib],
        [https://github.com/adapteva/epiphany-libs])

AC_CONFIG_SRCDIR([include/e_lib.h])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_AUX_DIR([build-aux])

AC_CONFIG_MACRO_DIR([m4])


# Support cross compilation
AC_CANONICAL_BUILD
AC_CANONICAL_HOST


# Initialize automake
AM_INIT_AUTOMAKE([-Wall -Werror])


# Support silent build rules. Disable by either passing --disable-silent-rules
# to configure or passing V=1 to make.
AM_SILENT_RULES([yes])


# Checks for programs.
AC_PROG_RANLIB
AC_PROG_CC
AM_PROG_AR
AM_PROG_AS


# Checks for libraries.

# (none needed)


# Checks for header files.
AC_CHECK_HEADERS([stddef.h string.h])


# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_TYPE_OFF_T
AC_TYPE_SIZE_T


# Checks for library functions.
#AC_FUNC_MALLOC # Don't do it. Assume GNU libc compatible malloc.


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
  ['-I$(top_srcdir)/include -I$(top_builddir) -I$(top_srcdir)/../e-trace/include $(assert_CPPFLAGS)'])

AC_SUBST([AM_LDFLAGS])

AC_OUTPUT
