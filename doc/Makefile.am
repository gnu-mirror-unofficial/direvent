# This file is part of Direvent
# Copyright (C) 2012-2021 Sergey Poznyakoff
#
# Direvent is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3, or (at your option)
# any later version.
#
# Direvent is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Direvent.  If not, see <http://www.gnu.org/licenses/>.

info_TEXINFOS=direvent.texi
direvent_TEXINFOS=\
 fdl.texi

dist_man_MANS=direvent.8 direvent.conf.5

EXTRA_DIST = \
 gendocs_template\
 gendocs.sh

clean-local:
	@rm -rf manual

GENDOCS = perl gendocs.pl

.PHONY: manual
manual:
	rm -rf manual
	TEXINPUTS=$(srcdir):$(top_srcdir)/build-aux:$(TEXINPUTS) \
	 MAKEINFO="$(MAKEINFO) $(MAKEINFOFLAGS) $(AM_MAKEINFOFLAGS)" \
	 $(GENDOCS) -C manual -o otherdoc.texi $(PACKAGE) otherdoc.texi.in
	$(MAKEINFO) $(AM_MAKEINFOFLAGS) $(MAKEINFOFLAGS) -DWEBDOC \
	   --html --init-file=webdoc.init $(info_TEXINFOS) -o manual

manual.tar.bz2: manual
	tar cfj manual.tar.bz2 manual

man-tar: manual.tar.bz2
