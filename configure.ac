# This file is part of GNU direvent -*- autoconf -*-
# Copyright (C) 2012-2021 Sergey Poznyakoff
#
# GNU direvent is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3, or (at your option)
# any later version.
#
# GNU direvent is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GNU direvent.  If not, see <http://www.gnu.org/licenses/>.

AC_PREREQ([2.69])
AC_INIT([GNU direvent], [5.3], [bug-direvent@gnu.org.ua], [direvent],
        [http://www.gnu.org.ua/software/direvent])
AC_CONFIG_SRCDIR([src/direvent.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.15 gnits tar-ustar silent-rules std-options])

# Enable silent rules by default:
AM_SILENT_RULES([yes])

# Checks for programs.
AC_PROG_AWK
AC_PROG_CC
DEVT_CC_PAREN_QUIRK
AC_PROG_RANLIB
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([sys/inotify.h sys/event.h])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_CHECK_FUNCS([inotify_init kqueue rfork])

if test "$ac_cv_header_sys_inotify_h/$ac_cv_func_inotify_init" = yes/yes; then
  iface=inotify
elif test "$ac_cv_header_sys_event_h/$ac_cv_func_kqueue" = yes/yes; then
  iface=kqueue
else
  AC_MSG_ERROR([no suitable interface found])
fi

AM_CONDITIONAL([DIREVENT_INOTIFY],[test $iface = inotify])
AM_CONDITIONAL([DIREVENT_KQUEUE],[test $iface = kqueue])
AC_SUBST(IFACE, $iface)
AC_DEFINE_UNQUOTED(USE_IFACE,IFACE_`echo $iface|tr a-z A-Z`,
                   [event notification interface to use])
AM_CONDITIONAL([DIREVENT_RFORK], [test "$ac_cv_func_rfork" = yes])
AH_TOP([
/* Supported event notification types */
#define IFACE_INOTIFY 0  /* inotify - Linux */
#define IFACE_KQUEUE  1  /* kqueue  - BSD */
])

# ################################# 
# Select interface used to close file descriptors greater than or
# equal to the given one.
#
# Variants:
#  1. closefrom call    (FreeBSD)
#  2. F_CLOSEM fcntl (NetBSD, AIX, IRIX)
#  3. proc_pidinfo call (Darwin)
#  4. /proc/self/fd filesystem   (Linux)
#  5. Brute force
#
# The defines created here direct conditionalal compilation in
# src/closefds.c

AC_CHECK_FUNCS([closefrom])
AC_CHECK_DECL([F_CLOSEM],
              AC_DEFINE([HAVE_FCNTL_CLOSEM], [1],
	                [Use F_CLOSEM fcntl for mu_close_fds]),
	      [],
	      [#include <limits.h>
	       #include <fcntl.h>
])

AC_CHECK_HEADERS([libproc.h])
AC_CHECK_FUNCS([proc_pidinfo])

if test -d "/proc/self/fd" ; then
  AC_DEFINE([HAVE_PROC_SELF_FD], [1], [Define if you have /proc/self/fd])
fi  


# Grecs subsystem

GRECS_SETUP([grecs],[tree-api git2chg no-preproc getopt tests])
GRECS_HOST_PROJECT_INCLUDES='-I$(top_srcdir)/src'

# Gettext.
AM_ICONV
AM_GNU_GETTEXT([external], [need-formatstring-macros])
AM_GNU_GETTEXT_VERSION([0.18])

AC_SUBST(INCLUDE_PATH_ARGS,['$(pkgdatadir)/include:$(pkgdatadir)/$(VERSION)/include'])
AC_ARG_WITH([include-path],
            [AC_HELP_STRING([--with-include-path=PATH],
	         [set direvent runtime include path])],
            [INCLUDE_PATH_ARGS=$withval])
INCLUDE_PATH_ARGS=$(echo "$INCLUDE_PATH_ARGS" | sed 's/:/\",\"/g;s/^/\"/;s/$/\"/')

# Initialize the test suite.
AC_CONFIG_TESTDIR(tests)
AC_CONFIG_FILES([tests/Makefile tests/atlocal po/Makefile.in])
AM_MISSING_PROG([AUTOM4TE], [autom4te])

AC_CONFIG_COMMANDS([status],[
cat <<EOT

Selected interface: $iface

EOT
],[
iface=$iface
])

AC_CONFIG_FILES([Makefile
                 src/Makefile
		 doc/Makefile])
AC_OUTPUT
