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
#include <signal.h>
#include <sys/inotify.h>


/* Event codes */
struct transtab sysev_transtab[] = {
	{ "ACCESS",        IN_ACCESS         },
	{ "ATTRIB",        IN_ATTRIB         },       
	{ "CLOSE_WRITE",   IN_CLOSE_WRITE    },  
	{ "CLOSE_NOWRITE", IN_CLOSE_NOWRITE  },
	{ "CREATE",        IN_CREATE         },       
	{ "DELETE",        IN_DELETE         },      
	{ "MODIFY",        IN_MODIFY         },
	{ "MOVED_FROM",    IN_MOVED_FROM     },    
	{ "MOVED_TO",      IN_MOVED_TO       },      
	{ "OPEN",          IN_OPEN           },
	{ 0 }
};

#define CHANGED_MASK (IN_CREATE|IN_MOVED_TO|IN_MODIFY)

event_mask genev_xlat[] = {
	{ GENEV_CREATE, IN_CREATE|IN_MOVED_TO },
	{ GENEV_WRITE,  IN_MODIFY },
	{ GENEV_ATTRIB, IN_ATTRIB },
	{ GENEV_DELETE, IN_DELETE|IN_MOVED_FROM },
	{ 0 }
};


static int ifd;

static struct watchpoint **wptab;
static size_t wpsize;

static int
wpreg(int wd, struct watchpoint *wpt)
{
	if (wd < 0)
		abort();
	if (wd >= wpsize) {
		size_t n = wpsize;
		struct watchpoint **p;

		if (n == 0)
			n = sysconf(_SC_OPEN_MAX);
		while (wd >= n) {
			n *= 2;
			if (n < wpsize) {
				diag(LOG_CRIT,
				     _("can't allocate memory for fd %d"),
				     wd);
				return -1;
			}
		}
		p = realloc(wptab, n * sizeof(wptab[0]));
		if (!p) {
			diag(LOG_CRIT,
			     _("can't allocate memory for fd %d"),
			     wd);
			return -1;
		}

		memset(p + wpsize, 0, (n - wpsize) * sizeof(wptab[0]));
		wptab = p;
		wpsize = n;
	}
	watchpoint_ref(wpt);
	wptab[wd] = wpt;
	return 0;
}

static void
wpunreg(int wd)
{
	if (wd < 0 || wd > wpsize)
		abort();
	if (wptab[wd]) {
		watchpoint_unref(wptab[wd]);
		wptab[wd] = NULL;
	}
}

static struct watchpoint *
wpget(int wd)
{
	if (wd >= 0 && wd < wpsize)
		return wptab[wd];
	return NULL;
}

int
sysev_filemask(struct watchpoint *dp)
{
	return 0;
}

void
sysev_init()
{
	ifd = inotify_init();
	if (ifd == -1) {
		diag(LOG_CRIT, "inotify_init: %s", strerror(errno));
		exit(1);
	}
}

int
sysev_add_watch(struct watchpoint *wpt, event_mask mask)
{
	int sysmask = evtrans_gen_to_sys(&mask, genev_xlat);
	int wd;

	if (mask.gen_mask & GENEV_CHANGE) {
		sysmask |= CHANGED_MASK | IN_CLOSE_WRITE;
	}
	wd = inotify_add_watch(ifd, wpt->dirname, sysmask);
	if (wd >= 0 && wpreg(wd, wpt)) {
		inotify_rm_watch(ifd, wd);
		return -1;
	}
	return wd;
}

void
sysev_rm_watch(struct watchpoint *wpt)
{
	wpunreg(wpt->wd);
	inotify_rm_watch(ifd, wpt->wd);
}

/* Remove a watcher identified by its directory and file name */
void
remove_watcher(const char *dir, const char *name)
{
	struct watchpoint *wpt;
	char *fullname = mkfilename(dir, name);
	if (!fullname) {
		diag(LOG_EMERG, "%s",
		     _("not enough memory: "
		       "cannot look up a watcher to delete"));
		return;
	}
	wpt = watchpoint_lookup(fullname);
	free(fullname);
	if (wpt)
		watchpoint_suspend(wpt);
}

/*
 * Define or test the changed status of file FILENAME in watchpoint WPT.
 * If INSTALL is 1, file changed status is set.  Otherwise, its current
 * status is verified and reset to unchanged.
 * Returns the status (0 if file is not changed and 1 otherwise).
 */
static int
file_changed(struct watchpoint *wpt, char const *filename, int install)
{
	struct grecs_syment key;

	if (install) {
		if (!wpt->files_changed) {
			wpt->files_changed = grecs_symtab_create_default(sizeof(struct grecs_syment));
			if (!wpt->files_changed)
				nomem_abend();
		}
	} else if (!wpt->files_changed)
		return 0;

	key.name = (char*) filename;
	if (grecs_symtab_lookup_or_install(wpt->files_changed, &key,
					   &install)) {
		grecs_symtab_remove(wpt->files_changed, &key);
		return 1;
	}
	if (install)
		nomem_abend();
	return 0;
}

static void
process_event(struct inotify_event *ep)
{
	struct watchpoint *wpt;
	char *dirname, *filename;
	event_mask event;
	
	wpt = wpget(ep->wd);
	if (!wpt) {
		if (!(ep->mask & IN_IGNORED))
			diag(LOG_NOTICE, _("watcher not found: %d (%s)"),
			     ep->wd, ep->name);
		return;
	}
	
	if (ep->mask & IN_IGNORED) {
		diag(LOG_NOTICE, _("%s deleted"), wpt->dirname);
		watchpoint_suspend(wpt);
		return;
	}
	
	if (ep->mask & IN_Q_OVERFLOW) {
		diag(LOG_NOTICE, "%s", _("event queue overflow"));
		return;
	} else if (ep->mask & IN_UNMOUNT) {
		/* FIXME: not sure if there's
		   anything to do. Perhaps we should
		   deregister the watched dirs that
		   were located under the mountpoint
		*/
		return;
	} else if (!wpt) {
		if (ep->len)
			diag(LOG_NOTICE, _("unrecognized event %x for %s"),
			     ep->mask, ep->name);
		else
			diag(LOG_NOTICE, _("unrecognized event %x"), ep->mask);
		return;
	}

	if (ep->mask & IN_CREATE) {		
		debug(1, (_("%s/%s created"), wpt->dirname, ep->name));
		if (watchpoint_recent_lookup(wpt, ep->name)) {
			diag(LOG_NOTICE,
			     _("%s/%s: ignoring CREATE event: already delivered"),
			     wpt->dirname, ep->name);
			return;
		}
	}

	if (ep->len == 0) {
		if (wpt->isdir) {
			char *sys_str = NULL;
			event.sys_mask = ep->mask;
			if (ev_format(event, NULL, &sys_str))
				diag(LOG_NOTICE,
				     _("%s: ignoring event (%x) for the watchpoint directory"),
				     wpt->dirname, ep->mask);
			else {
				diag(LOG_NOTICE,
				     _("%s: ignoring event (%s) for the watchpoint directory"),
				     wpt->dirname, sys_str);
				free(sys_str);
			}
			return;
		}
		filename = split_pathname(wpt, &dirname);
	} else {
		dirname = wpt->dirname;
		filename = ep->name;
	}

	/* Translate system events to generic ones. */
	evtrans_sys_to_gen(ep->mask, genev_xlat, &event);
	if (ep->mask & CHANGED_MASK) {
		file_changed(wpt, filename, 1);
	}
	if (ep->mask & IN_CLOSE_WRITE) {
		if (file_changed(wpt, filename, 0)) {
			/* Reset the flag and raise the event. */
			event.gen_mask |= GENEV_CHANGE;
		}
	}
	if (debug_level > 0)
		ev_log(LOG_DEBUG, wpt, event, ep->name);

	watchpoint_run_handlers(wpt, event, dirname, filename);
	
	unsplit_pathname(wpt);

	if (ep->mask & (IN_DELETE|IN_MOVED_FROM)) {
		debug(1, (_("%s/%s deleted"), wpt->dirname, ep->name));
		remove_watcher(wpt->dirname, ep->name);
	}
}	

int
sysev_select()
{
	char buffer[4096];
	struct inotify_event *ep;
	size_t size;
	ssize_t rdbytes;

	rdbytes = read(ifd, buffer, sizeof(buffer));
	if (rdbytes == -1) {
		if (errno == EINTR) {
			if (!signo || signo == SIGCHLD || signo == SIGALRM)
				return 0;
			diag(LOG_NOTICE, _("got signal %d"), signo);
			return 1;
		}
		
		diag(LOG_NOTICE, _("read failed: %s"), strerror(errno));
		return 1;
	}
		
	ep = (struct inotify_event *) buffer;
	while (rdbytes) {
		if (ep->wd >= 0)
			process_event(ep);
		size = sizeof(*ep) + ep->len;
		ep = (struct inotify_event *) ((char*) ep + size);
		rdbytes -= size;
	}
	
	return 0;
}
