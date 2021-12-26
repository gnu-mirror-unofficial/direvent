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
#include <ctype.h>

extern char **environ;    /* Environment */

static struct defenv {
	char *name;
	char *value;
} defenv[] = {
	{ "DIREVENT_SYSEV_CODE", "${sysev_code}" },
	{ "DIREVENT_SYSEV_NAME", "${sysev_name}" },
	{ "DIREVENT_GENEV_CODE", "${genev_code}" },
	{ "DIREVENT_GENEV_NAME", "${genev_name}" },
	{ "DIREVENT_FILE", "${file}" },
	{ NULL }
};

environ_t *
environ_setup(envop_t *envop, char **kve, int shell)
{
	environ_t *env = environ_create(environ);
	int i;

	for (i = 0; kve[i]; i += 2)
		environ_set(env, kve[i], kve[i+1]);
		
	for (i = 0; defenv[i].name; i++) {
		environ_set(env, defenv[i].name, defenv[i].value);
	}

	if (!shell) {
		for (i = 0; kve[i]; i += 2) {
			environ_unset(env, kve[i], kve[i+1]);
		}
	}
	return env;
}
