/* direvent - directory content watcher daemon
   Copyright (C) 2012-2021 Sergey Poznyakoff

   Direvent is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3 of the License, or (at your
   option) any later version.

   Direvent is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with direvent. If not, see <http://www.gnu.org/licenses/>. */

#include "direvent.h"

int
getevt(const char *name, event_mask *mask)
{
	if (trans_strtotok(genev_transtab, name, &mask->gen_mask) == 0)
		mask->sys_mask = 0;
	else if (trans_strtotok(sysev_transtab, name, &mask->sys_mask) == 0)
		mask->gen_mask = 0;
	else
		return -1;
	return 0;
}

void
evtempty(event_mask *mask)
{
	mask->gen_mask = 0;
	mask->sys_mask = 0;
}

void
evtfill(event_mask *mask)
{
	mask->gen_mask = trans_fullmask(genev_transtab);
	mask->sys_mask = trans_fullmask(sysev_transtab);
}

int
evtand(event_mask const *a, event_mask const *b, event_mask *res)
{
	res->gen_mask = a->gen_mask & b->gen_mask;
	res->sys_mask = a->sys_mask & b->sys_mask;
	return res->gen_mask != 0 || res->sys_mask != 0;
}

int
evtnullp(event_mask *mask)
{
	return mask->gen_mask == 0 && mask->sys_mask == 0;
}

struct transtab genev_transtab[] = {
	{ "create", GENEV_CREATE },
	{ "write",  GENEV_WRITE  },
	{ "attrib", GENEV_ATTRIB },
	{ "delete", GENEV_DELETE },
	{ "change", GENEV_CHANGE },
	{ NULL }
};

void
evtrans_sys_to_gen(int fflags, event_mask const *xlat, event_mask *ret_evt)
{
	ret_evt->sys_mask = fflags;
	ret_evt->gen_mask = 0;
	for (; xlat->gen_mask != 0; xlat++)
		if (xlat->sys_mask & ret_evt->sys_mask)
			ret_evt->gen_mask |= xlat->gen_mask;
}

int
evtrans_gen_to_sys(event_mask const *event, event_mask const *xlat)
{
	int n = 0;
	for (; xlat->gen_mask != 0; xlat++)
		if (xlat->gen_mask & event->gen_mask)
			n |= xlat->sys_mask;
	return n;
}
