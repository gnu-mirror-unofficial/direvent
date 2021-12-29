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

bin_PROGRAMS=direvent
direvent_SOURCES=\
 direvent.c\
 direvent.h\
 cmdline.h\
 closefds.c\
 config.c\
 envop.c\
 envop.h\
 event.c\
 fnpat.c\
 handler.c\
 watcher.c\
 progman.c\
 sigv.c\
 wildmatch.c

if DIREVENT_INOTIFY
  direvent_SOURCES += ev_inotify.c detach-std.c
endif

if DIREVENT_KQUEUE
  direvent_SOURCES += ev_kqueue.c
if DIREVENT_RFORK
  direvent_SOURCES += detach-bsd.c
else
  direvent_SOURCES += detach-darwin.c
endif
endif

LDADD=@GRECS_LDADD@ @LIBINTL@
AM_CPPFLAGS=\
 @GRECS_INCLUDES@\
 -DSYSCONFDIR=\"$(sysconfdir)\"\
 -DLOCALEDIR=\"$(localedir)\"\
 -DINCLUDE_PATH_ARGS='$(INCLUDE_PATH_ARGS)'

BUILT_SOURCES=cmdline.h
EXTRA_DIST=cmdline.opt
noinst_HEADERS=gettext.h

SUFFIXES=.opt .c .h
.opt.h:
	$(AM_V_GEN)m4 -s $(top_srcdir)/@GRECS_SUBDIR@/build-aux/getopt.m4 $< > $@

