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
#include <sys/event.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>

struct transtab sysev_transtab[] = {
	{ "DELETE", NOTE_DELETE },
	{ "WRITE",  NOTE_WRITE  },
	{ "EXTEND", NOTE_EXTEND },
	{ "ATTRIB", NOTE_ATTRIB },
	{ "LINK",   NOTE_LINK   },
	{ "RENAME", NOTE_RENAME },
	{ "REVOKE", NOTE_REVOKE },
#ifdef NOTE_CLOSE
	{ "CLOSE",  NOTE_CLOSE },
#endif
#ifdef NOTE_CLOSE_WRITE
	{ "CLOSE_WRITE", NOTE_CLOSE_WRITE },
#endif
#ifdef NOTE_OPEN
	{ "OPEN", NOTE_OPEN },
#endif
#ifdef NOTE_READ
	{ "READ", NOTE_READ },
#endif
	{ NULL }
};


static int kq;
static struct kevent *evtab;
static struct kevent *chtab;
static int chcnt;
static int chclosed = -1;

#define GENEV_WRITE_TRANSLATION (NOTE_WRITE|NOTE_EXTEND)

event_mask genev_xlat[] = {
	{ GENEV_WRITE,  GENEV_WRITE_TRANSLATION },
	{ GENEV_ATTRIB, NOTE_ATTRIB|NOTE_LINK },
	{ GENEV_DELETE, NOTE_DELETE|NOTE_RENAME|NOTE_REVOKE },
	{ 0 }
};

void
sysev_init()
{
	kq = kqueue();
	if (kq == -1) {
		diag(LOG_CRIT, "kqueue: %s", strerror(errno));
		exit(1);
	}
	evtab = calloc(sysconf(_SC_OPEN_MAX), sizeof(evtab[0]));
	chtab = calloc(sysconf(_SC_OPEN_MAX), sizeof(chtab[0]));
}

int
sysev_filemask(struct watchpoint *dp)
{
	return S_IFMT;
}

int
sysev_add_watch(struct watchpoint *wpt, event_mask mask)
{
	int wd = open(wpt->dirname, O_RDONLY);
	if (wd >= 0) {
		struct stat st;
		int sysmask;
		
		if (fstat(wd, &st)) {
			close(wd);
			return -1;
		}
		wpt->file_ctime = st.st_ctime;
		sysmask = evtrans_gen_to_sys(&mask, genev_xlat) | NOTE_DELETE;
#if defined(NOTE_CLOSE_WRITE)
		if (mask.gen_mask & GENEV_CHANGE) {
			sysmask |= GENEV_WRITE_TRANSLATION | NOTE_CLOSE_WRITE;
			wpt->file_changed = 0;
		}
#endif
		if (S_ISDIR(st.st_mode) && (mask.gen_mask & GENEV_CREATE))
			sysmask |= NOTE_WRITE;
		EV_SET(chtab + chcnt, wd, EVFILT_VNODE,
		       EV_ADD | EV_ENABLE | EV_CLEAR, sysmask,
		       0, wpt);
		wd = chcnt++;
		if (kevent(kq, chtab, chcnt, NULL, 0, NULL) == -1)
			diag(LOG_ERR, "%s: can't register kevent: %s",
			     wpt->dirname, strerror(errno));
	}
	return wd;
}

void
sysev_rm_watch(struct watchpoint *wpt)
{
	close(chtab[wpt->wd].ident);
	chtab[wpt->wd].ident = -1;
	if (chclosed == -1 || chclosed > wpt->wd)
		chclosed = wpt->wd;
}

static void
chclosed_elim()
{
	int i, j;
	
	if (chclosed == -1)
		return;

	for (i = chclosed, j = chclosed + 1; j < chcnt; j++)
		if (chtab[j].ident != -1) {
			struct watchpoint *wpt;
			
			chtab[i] = chtab[j];
			wpt = chtab[i].udata;
			wpt->wd = i;
			i++;
		}
	chcnt = i;
	chclosed = -1;
}

static void
check_created(struct watchpoint *dp)
{
	DIR *dir;
	struct dirent *ent;

	dir = opendir(dp->dirname);
	if (!dir) {
		diag(LOG_ERR, "cannot open directory %s: %s",
		     dp->dirname, strerror(errno));
		return;
	}

	while (1) {
		struct stat st;
		char *pathname;

		errno = 0;
		ent = readdir(dir);
		if (!ent) {
			if (errno)
				diag(LOG_ERR, "readdir(%s): %s",
				     dp->dirname, strerror(errno));
			break;
		}

		if (ent->d_name[0] == '.' &&
		    (ent->d_name[1] == 0 ||
		     (ent->d_name[1] == '.' && ent->d_name[2] == 0)))
			continue;

		if (watchpoint_pattern_match(dp, ent->d_name))
			continue;
		
		pathname = mkfilename(dp->dirname, ent->d_name);
		if (!pathname) {
			diag(LOG_ERR, "cannot stat %s/%s: not enough memory",
			     dp->dirname, ent->d_name);
			continue;
		}

		if (stat(pathname, &st)) {
			diag(LOG_ERR, "cannot stat %s: %s",
			     pathname, strerror(errno));
		/* If ok, first see if the file is newer than the last
		   directory scan.  If not, there is still a chance
		   the file is new (the timestamp precision leaves a
		   time window long enough for a file to be created)
		   so try the more expensive hash lookup to see if we
		   know about that file.  If the file is new, register
		   a watcher for it. */
		} else if (st.st_ctime > dp->file_ctime ||
			   !watchpoint_lookup(pathname)) {
			deliver_ev_create(dp, dp->dirname, ent->d_name, 1);
			dp->file_ctime = st.st_ctime;
		}
		free(pathname);
	}
	closedir(dir);
}

static void
process_event(struct kevent *ep)
{
	struct watchpoint *dp = ep->udata;
	char *filename, *dirname;
	event_mask event;
	
	if (!dp) {
		diag(LOG_NOTICE, "unrecognized event %x", ep->fflags);
		return;
	}

	ev_log(ep->fflags, dp);

	/* Translate system events to generic ones. */
	evtrans_sys_to_gen(ep->fflags, genev_xlat, &event);
#ifdef NOTE_CLOSE_WRITE
	if (ep->fflags & GENEV_WRITE_TRANSLATION) {
		dp->file_changed = 1;
	}
	if (ep->fflags & NOTE_CLOSE_WRITE) {
		if (dp->file_changed) {
			/* Reset the flag and raise the event. */
			dp->file_changed = 0;
			event.gen_mask |= GENEV_CHANGE;
		}
	}
#endif
	//FIXME: log generic events

	/* 
	 * If the event was reported for a non-directory, run user-installed
	 * handlers.  Notice, that so far special handlers (such as sentinels)
	 * are installed only for directories.  Should that change in the
	 * future, the logic below will need to be changed accordingly.
	 */
	if (!dp->isdir) {
		filename = split_pathname(dp, &dirname);
		watchpoint_run_handlers(dp, event, dirname, filename);
		unsplit_pathname(dp);
	}
	
	if (dp->isdir && !(ep->fflags & (NOTE_DELETE|NOTE_RENAME))) {
		/* Check if new files have appeared. */
		if (ep->fflags & NOTE_WRITE) 
			check_created(dp);
	}

	if (ep->fflags & (NOTE_DELETE|NOTE_RENAME)) {
		debug(1, (_("%s deleted"), dp->dirname));
		watchpoint_suspend(dp);
	}
}	

int
sysev_select()
{
	int i, n;
	
	chclosed_elim();
	n = kevent(kq, chtab, chcnt, evtab, chcnt, NULL);
	if (n == -1) {
		if (errno == EINTR) {
			if (signo == 0 || signo == SIGCHLD || signo == SIGALRM)
				return 0;
			diag(LOG_NOTICE, "got signal %d", signo);
		}
		diag(LOG_ERR, "kevent: %s", strerror(errno));
		return 1;
	} 

	for (i = 0; i < n; i++) 
		process_event(&evtab[i]);
		
	return 0;
}
		
