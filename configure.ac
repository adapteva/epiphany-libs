#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.


# Needed for autoconf 2.64
#m4_pattern_allow([AC_CHECK_HEADER_STDBOOL])

AC_PREREQ([2.64])
AC_INIT([epiphany-libs: Epiphany Libraries], [0.1],
        [https://github.com/adapteva/epiphany-sdk/issues], [epiphany-libs],
        [https://github.com/adapteva/epiphany-libs])

AC_CONFIG_SRCDIR([README])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_AUX_DIR([build-aux])

AC_CONFIG_MACRO_DIR([m4])


# Support cross compilation
AC_CANONICAL_BUILD
AC_CANONICAL_HOST
# Don't use autoconf machinery for configuring e-lib target.
# See --with-target
# AC_CANONICAL_TARGET


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
AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h unistd.h])


# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT8_T
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T


# Checks for library functions.
AC_FUNC_ALLOCA
#AC_FUNC_MALLOC # Don't do it. Assume GNU libc compatible malloc.
AC_FUNC_MMAP
AC_CHECK_FUNCS([gettimeofday inet_ntoa memchr memmove memset munmap pow realpath socket strcasecmp strchr strerror strstr strtol strtoul])


AC_CONFIG_FILES([Makefile])

# Define the --enable-assert command-line option
AC_ARG_ENABLE(assert,
  [AS_HELP_STRING([--enable-assert], [Build with assertions enabled])],
  [enable_assert=yes], [enable_assert=no])
# Set any CPPFLAGS specific to debugging
AS_IF([test "x$enable_assert" = "xyes"],
  [AC_SUBST(assert_CPPFLAGS, [''])],
  [AC_SUBST(assert_CPPFLAGS, ['-DNDEBUG'])])


AC_ARG_WITH(target,
  [AS_HELP_STRING([--with-target=ARCH],
                  [target architecture (used for e-lib)])],
  [], [with_target="epiphany-elf"])

# Verify target
AS_CASE([$with_target],
        [epiphany-elf], [],
        [AC_MSG_ERROR(["Invalid target ${with_target}, must be epiphany-elf."])])


AC_ARG_WITH(target-prefix,
  [AS_HELP_STRING([--with-target-prefix=PREFIX],
                  [install target files in PREFIX (e-lib)])],
  [target_prefix="$withval"])


AC_ARG_WITH(bsps-dir,
  [AS_HELP_STRING([--with-bsps-dir=DIR],
                  [install BSPs in DIR])],
  [bspsdir="$withval"], [bspsdir='${datadir}/epiphany/bsps'])
AC_SUBST(BSPSDIR, [$bspsdir])
AC_SUBST(bspsdir, [$bspsdir])


# Easiest way to include header files globally
AC_SUBST([AM_CPPFLAGS],
  ['-I$(top_srcdir) -I$(top_srcdir)/e-hal/src -I$(top_srcdir)/e-xml/src -I$(top_srcdir)/e-trace/include -I$(top_builddir) $(assert_CPPFLAGS)'])

AC_SUBST([AM_LDFLAGS])



# This is the messy part. We need to set the host to epiphany-elf and adjust
# the prefix for e-lib.

save_prefix=$prefix
save_exec_prefix=$exec_prefix
save_ac_configure_args=$ac_configure_args

host_ac_configure_args=$ac_configure_args
ac_configure_args=

# Whitelist arguments
for host_arg in $host_ac_configure_args
do
AS_CASE([$host_arg],
        [*--enable-assert*], [ac_configure_args="$ac_configure_args $host_arg"],
        [*--disable-assert*],[ac_configure_args="$ac_configure_args $host_arg"],
        [])
done


# Insert target as host
ac_configure_args="--host=$with_target $ac_configure_args"

# Expand default prefix NOW.
test "x$prefix" = xNONE && host_prefix=$ac_default_prefix || host_prefix=$prefix
test "x$target_prefix" = x && target_prefix="${host_prefix}/${with_target}"
prefix=$target_prefix
exec_prefix=$prefix

# Insert target prefix
ac_configure_args="$ac_configure_args --prefix=${target_prefix}"

# Custom command that configures subdir NOW, so we can restore variables after.
AX_CONFIG_DIR([e-lib])

# Restore variables
prefix=$save_prefix
exec_prefix=$save_exec_prefix
ac_configure_args=$save_ac_configure_args


AC_SUBST(TARGET_PREFIX, [$target_prefix])
AC_SUBST(target_prefix, [$target_prefix])

AC_OUTPUT
