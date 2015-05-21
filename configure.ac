AC_PREREQ(2.59)
AC_COPYRIGHT([Copyright (c) 2013-2014 UPLEX Nils Goroll Systemoptimierung])
AC_INIT([libvmod-re], [0.6])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR(src/vmod_re.vcc)
AM_CONFIG_HEADER(config.h)

AC_CANONICAL_SYSTEM
AC_LANG(C)

AM_INIT_AUTOMAKE([foreign])

AC_GNU_SOURCE
AC_PROG_CC
AC_PROG_CC_STDC
if test "x$ac_cv_prog_cc_c99" = xno; then
	AC_MSG_ERROR([Could not find a C99 compatible compiler])
fi
AC_PROG_CPP

AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PROG_MAKE_SET

# Check for rst utilities
AC_CHECK_PROGS(RST2MAN, [rst2man rst2man.py], "no")
if test "x$RST2MAN" = "xno"; then
	AC_MSG_WARN([rst2man not found - not building man pages])
fi
AM_CONDITIONAL(HAVE_RST2MAN, [test "x$RST2MAN" != "xno"])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h])
AC_CHECK_HEADERS([pthread.h])

# Varnish include files tree
VARNISH_VMOD_INCLUDES
VARNISH_VMOD_DIR
VARNISH_VMODTOOL

# Check paths to varnishd and varnishtest
AC_ARG_VAR([VARNISHTEST], [path to varnishtest (mandatory for make check)])
AC_ARG_VAR([VARNISHD], [path to varnishd (mandatory for make check)])
AC_PATH_PROG([VARNISHTEST], [varnishtest])
AC_PATH_PROG([VARNISHD], [varnishd], [/bin/false],
             [${PATH}${PATH_SEPARATOR}/usr/local/sbin${PATH_SEPARATOR}/usr/sbin])
if test "x$VARNISHD" = x; then
   AC_MSG_FAILURE([Can't locate varnishd])
fi
if test "x$VARNISHTEST" = x; then
   AC_MSG_FAILURE([Can't locate varnishtest])
fi

# This corresponds to FreeBSD's WARNS level 6
DEVELOPER_CFLAGS="-Wall -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wreturn-type -Wcast-qual -Wwrite-strings -Wswitch -Wshadow -Wcast-align -Wchar-subscripts -Winline -Wnested-externs -Wredundant-decls -Wformat"

# Additional flags for GCC 4
EXTRA_DEVELOPER_CFLAGS="-Wextra -Wno-missing-field-initializers -Wno-sign-compare"

# --enable-developer-warnings
AC_ARG_ENABLE(developer-warnings,
	AS_HELP_STRING([--enable-developer-warnings],[enable strict warnings (default is NO)]),
	CFLAGS="${CFLAGS} ${DEVELOPER_CFLAGS}")

# --enable-debugging-symbols
AC_ARG_ENABLE(debugging-symbols,
	AS_HELP_STRING([--enable-debugging-symbols],[enable debugging symbols (default is NO)]),
	CFLAGS="${CFLAGS} -O0 -g -fno-inline")

# --enable-diagnostics
AC_ARG_ENABLE(diagnostics,
	AS_HELP_STRING([--enable-diagnostics],[enable run-time diagnostics (default is NO)]),
	CFLAGS="${CFLAGS} -DDIAGNOSTICS")

# --enable-extra-developer-warnings
AC_ARG_ENABLE(extra-developer-warnings,
	AS_HELP_STRING([--enable-extra-developer-warnings],[enable even stricter warnings (default is NO)]),
	[],
	[enable_extra_developer_warnings=no])

if test "x$enable_stack_protector" != "xno"; then
	save_CFLAGS="$CFLAGS"
	CFLAGS="${CFLAGS} ${EXTRA_DEVELOPER_CFLAGS}"
	AC_COMPILE_IFELSE(
		[AC_LANG_PROGRAM([],[],[])],
		[],
		[AC_MSG_WARN([All of ${EXTRA_DEVELOPER_CFLAGS} not supported, disabling])
		    CFLAGS="$save_CFLAGS"])
fi

# --enable-stack-protector
AC_ARG_ENABLE(stack-protector,
	AS_HELP_STRING([--enable-stack-protector],[enable stack protector (default is NO)]),
	[],
	[enable_stack_protector=no])

if test "x$enable_stack_protector" != "xno"; then
	save_CFLAGS="$CFLAGS"
	CFLAGS="${CFLAGS} -fstack-protector-all"
	AC_COMPILE_IFELSE(
		[AC_LANG_PROGRAM([],[],[])],
		[],
		[AC_MSG_WARN([-fstack-protector not supported, disabling])
		    CFLAGS="$save_CFLAGS"])
fi

# --enable-werror
AC_ARG_ENABLE(werror,
	AS_HELP_STRING([--enable-werror],[use -Werror (default is NO)]),
	CFLAGS="${CFLAGS} -Werror")


AC_CONFIG_FILES([
	Makefile
	src/Makefile
])
AC_OUTPUT
