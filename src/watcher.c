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
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>

void
watchpoint_ref(struct watchpoint *wpt)
{
	++wpt->refcnt;
}

void
watchpoint_unref(struct watchpoint *wpt)
{
	if (--wpt->refcnt)
		return;
	watchpoint_recent_deinit(wpt);
	free(wpt->dirname);
	handler_list_unref(wpt->handler_list);
	free(wpt);
}


struct wpref {
	int used;
	struct watchpoint *wpt;
};

static unsigned
wpref_hash(void *data, unsigned long hashsize)
{
	struct wpref *sym = data;
	return grecs_hash_string(sym->wpt->dirname, hashsize);
}

static int
wpref_cmp(const void *a, const void *b)
{
	struct wpref const *syma = a;
	struct wpref const *symb = b;

	return strcmp(syma->wpt->dirname, symb->wpt->dirname);
}

static int
wpref_copy(void *a, void *b)
{
	struct wpref *syma = a;
	struct wpref *symb = b;

	syma->used = 1;
	syma->wpt = symb->wpt;
	return 0;
}

static void
wpref_free(void *p)
{
	struct wpref *wpref = p;
	watchpoint_unref(wpref->wpt);
	free(wpref);
}

struct watchpoint *recent_head, *recent_tail;

static void
watchpoint_recent_link(struct watchpoint *wp)
{
	wp->rhead.next = NULL;
	wp->rhead.prev = recent_tail;
	if (recent_tail)
		recent_tail->rhead.next = wp;
	else
		recent_head = wp;
	recent_head = wp;
}

static void
watchpoint_recent_unlink(struct watchpoint *wp)
{
	struct watchpoint *p;
	
	if ((p = wp->rhead.prev) != NULL)
		p->rhead.next = wp->rhead.next;
	else
		recent_head = wp->rhead.next;
	if ((p = wp->rhead.next) != NULL)
		p->rhead.prev = wp->rhead.prev;
	else
		recent_tail = wp->rhead.prev;
}

void
watchpoint_recent_deinit(struct watchpoint *wp)
{
	if (wp->rhead.names) {
		debug(1, ("%s: recent status expired", wp->dirname));
		watchpoint_recent_unlink(wp);
		grecs_symtab_free(wp->rhead.names);
		wp->rhead.names = NULL;
	}
}

void
watchpoint_recent_init(struct watchpoint *wp)
{
	gettimeofday(&wp->rhead.tv, NULL);
	wp->rhead.names = grecs_symtab_create_default(sizeof(struct grecs_syment));
	if (!wp->rhead.names) {
		diag(LOG_CRIT, _("not enough memory"));
		exit(1);
	}
	watchpoint_recent_link(wp);
	alarm(1);
}

int
watchpoint_recent_lookup(struct watchpoint *wp, char const *name)
{
	int install = 1;
	if (wp->rhead.names) {
		struct grecs_syment key;
		struct grecs_syment *ent;

		key.name = (char*) name;
		ent = grecs_symtab_lookup_or_install(wp->rhead.names, &key,
						     &install);
		if (!ent) {
			diag(LOG_CRIT, _("not enough memory"));
			exit(1);
		}
		debug(1, ("watchpoint_recent_lookup: %s %s: %d",
			  wp->dirname, name, !install));
	}
	return !install;
}

int
watchpoint_recent_cleanup(void)
{
	struct timeval now;
	struct watchpoint *wp;
	int d = 0;
	
	gettimeofday(&now, NULL);
	for (wp = recent_head; wp; ) {
		struct watchpoint *next = wp->rhead.next;
		d = now.tv_sec - wp->rhead.tv.tv_sec;
		if (d > WATCHPOINT_RECENT_TTL)
			watchpoint_recent_deinit(wp);
		else
			break;
		wp = next;
	}
	return d;
}

struct grecs_symtab *nametab;

struct watchpoint *
watchpoint_install(const char *path, int *pnew)
{
	struct watchpoint wpkey;
	struct wpref key;
	struct wpref *ent;
	int install = 1;

	if (!nametab) {
		nametab = grecs_symtab_create(sizeof(struct wpref),
					      wpref_hash, wpref_cmp, wpref_copy,
					      NULL, wpref_free);
		if (!nametab) {
			diag(LOG_CRIT, _("not enough memory"));
			exit(1);
		}
	}

	wpkey.dirname = (char*) path;
	key.wpt = &wpkey;
	ent = grecs_symtab_lookup_or_install(nametab, &key, &install);
	if (install) {
	        struct watchpoint *wpt = ecalloc(1, sizeof(*wpt));
		wpt->dirname = estrdup(path);
		wpt->wd = -1;
		wpt->handler_list = handler_list_create();
		wpt->refcnt = 0;
		ent->wpt = wpt;
	}
	if (!ent)
		abort(); /* FIXME */
	watchpoint_ref(ent->wpt);
	if (pnew)
		*pnew = install;
	return ent->wpt;
}

struct watchpoint *
watchpoint_install_ptr(struct watchpoint *wpt)
{
	struct wpref key;
	int install = 1;
	key.wpt = wpt;
	
	if (!grecs_symtab_lookup_or_install(nametab, &key, &install)) {
		diag(LOG_CRIT, _("not enough memory"));
		exit(1);
	}
	watchpoint_ref(wpt);
	return wpt;
}	
	
static void
wpref_destroy(void *data)
{
	struct watchpoint *wpt = data;
	watchpoint_destroy(wpt);
}

static grecs_list_ptr_t watchpoint_gc_list;

void
watchpoint_gc(void)
{
	if (watchpoint_gc_list) {
		grecs_list_free(watchpoint_gc_list);
		watchpoint_gc_list = NULL;
	}
}

struct watchpoint *
watchpoint_lookup(const char *dirname)
{
	struct watchpoint wpkey;
	struct wpref key;
	struct wpref *ent;

	if (!nametab)
		return NULL;
	
	wpkey.dirname = (char*) dirname;
	key.wpt = &wpkey;
	ent = grecs_symtab_lookup_or_install(nametab, &key, NULL);
	return ent ? ent->wpt : NULL;
}

static void
watchpoint_remove(const char *dirname)
{
	struct watchpoint wpkey;
	struct wpref key;

	if (!nametab)
		return;

	wpkey.dirname = (char*) dirname;
	key.wpt = &wpkey;
	grecs_symtab_remove(nametab, &key);
}

void
watchpoint_destroy(struct watchpoint *wpt)
{
	debug(1, (_("removing watcher %s"), wpt->dirname));
	watchpoint_recent_deinit(wpt);//FIXME: This should also reset the timer
	sysev_rm_watch(wpt);
	watchpoint_remove(wpt->dirname);
}

void
watchpoint_suspend(struct watchpoint *wpt)
{
	if (!wpt->parent) /* A top-level watchpoint */
		watchpoint_install_sentinel(wpt);//FIXME: error checking
	watchpoint_destroy(wpt);
	if (grecs_symtab_count(nametab) == 0) {
		diag(LOG_CRIT, _("no watchers left; exiting now"));
		stop = 1;
	}
}

struct sentinel {
	struct handler *hp;
	struct watchpoint *watchpoint;
};

static int
sentinel_handler_run(struct watchpoint *wp, event_mask *event,
		     const char *dirname, const char *file, void *data,
		     int notify)
{
	struct sentinel *sentinel = data;
	struct watchpoint *wpt = sentinel->watchpoint;

	debug(1, ("watchpoint_init: from sentinel (%d)", __LINE__));
	watchpoint_init(wpt);
	watchpoint_install_ptr(wpt);
	deliver_ev_create(wpt, dirname, file, notify);
	
	if (handler_list_remove(wp->handler_list, sentinel->hp) == 0) {
		if (!watchpoint_gc_list) {
			watchpoint_gc_list = grecs_list_create();
			watchpoint_gc_list->free_entry = wpref_destroy;
		}
		grecs_list_append(watchpoint_gc_list, wp);
	}

	return 0;
}

static void
sentinel_handler_free(void *ptr)
{
	struct sentinel *sentinel = ptr;
	watchpoint_unref(sentinel->watchpoint);
	free(sentinel);
}

int
watchpoint_install_sentinel(struct watchpoint *wpt)
{
	struct watchpoint *sent;
	char *dirname;
	char *filename;
	struct handler *hp;
	event_mask ev_mask;
	struct sentinel *sentinel;
	
	filename = split_pathname(wpt, &dirname);
	sent = watchpoint_install(dirname, NULL);

	getevt("create", &ev_mask);
	hp = handler_alloc(ev_mask);
	hp->run = sentinel_handler_run;
	hp->free = sentinel_handler_free;

	sentinel = emalloc(sizeof(*sentinel));
	sentinel->watchpoint = wpt;
	sentinel->hp = hp;
	watchpoint_ref(wpt);
	
	hp->data = sentinel;
	hp->notify_always = 1;
	
	filpatlist_add_exact(&hp->fnames, filename);
	handler_list_append(sent->handler_list, hp);
	unsplit_pathname(wpt);
	diag(LOG_NOTICE, _("installing CREATE sentinel for %s"), wpt->dirname);
	debug(1, ("watchpoint_init: from install_sentinel (%d)", __LINE__));
	return watchpoint_init(sent);
}

static int watch_subdirs(struct watchpoint *parent, int notify);

static int
directory_sentinel_handler_run(struct watchpoint *wp, event_mask *event,
			       const char *dirname, const char *file,
			       void *data, int notify)
{
	struct sentinel *sentinel = data;
	struct watchpoint *parent = sentinel->watchpoint;
	char *filename;
	struct stat st;
	int filemask = sysev_filemask(parent);
	struct watchpoint *wpt;
	int rc = 0;
	
        //FIXME: Do that in sysev_filemask?  See also watch_subdirs
	if (parent->depth)
		filemask |= S_IFDIR;
	else
		filemask &= ~S_IFDIR;

	filename = mkfilename(dirname, file);
	if (!filename) {
		diag(LOG_ERR,
		     _("cannot create watcher %s/%s: not enough memory"),
		     dirname, file);
		return -1;
	}

	if (stat(filename, &st)) {
		diag(LOG_ERR,
		     _("cannot create watcher %s, stat failed: %s"),
		     filename, strerror(errno));
		rc = -1;
	} else if (st.st_mode & filemask) {
		int inst;

		wpt = watchpoint_install(filename, &inst);
		if (!inst)
			rc = -1;
		else {
			if ((wpt->depth = parent->depth) > 0)
				wpt->depth--;
			
			wpt->handler_list = handler_list_copy(parent->handler_list);
			if (USE_IFACE == IFACE_KQUEUE || wpt->depth)
				watchpoint_attach_directory_sentinel(wpt);
			if (handler_list_remove_cow(&wpt->handler_list, sentinel->hp) == 0) {
				if (!watchpoint_gc_list) {
					watchpoint_gc_list = grecs_list_create();
					watchpoint_gc_list->free_entry = wpref_destroy;
				}
				grecs_list_append(watchpoint_gc_list, wpt);
			} else {
				wpt->parent = parent;
		
				debug(1, ("watchpoint_init: from directory_sentinel (%d)", __LINE__));
				if (watchpoint_init(wpt)) {
					//FIXME watchpoint_free(wpt);
					rc = -1;
//					watch_subdirs(wpt, 1);//FIXME: for BSD
				} else {
					watchpoint_recent_init(wpt);
					watch_subdirs(wpt, notify);
				}
			}
		}
	}
	free(filename);
	debug(1, ("directory_sentinel finished at %d: %d", __LINE__, rc));
	return rc;
}

int
watchpoint_attach_directory_sentinel(struct watchpoint *wpt)
{
	struct handler *hp;
	event_mask ev_mask;
	struct sentinel *sentinel;
	
	getevt("create", &ev_mask);
	hp = handler_alloc(ev_mask);
	hp->run = directory_sentinel_handler_run;
	hp->free = sentinel_handler_free;

	sentinel = emalloc(sizeof(*sentinel));
	sentinel->watchpoint = wpt;
	sentinel->hp = hp;
	watchpoint_ref(wpt);
	
	hp->data = sentinel;
	hp->notify_always = 1;
	
	handler_list_append_cow(&wpt->handler_list, hp);
	diag(LOG_NOTICE, _("installing CREATE sentinel for %s/*"), wpt->dirname);
	return 0;
}

int 
watchpoint_init(struct watchpoint *wpt)
{
	struct stat st;
	event_mask mask = { 0, 0 };
	struct handler *hp;
	handler_iterator_t itr;	
	int wd;

	debug(1, (_("creating watcher %s"), wpt->dirname));

	if (stat(wpt->dirname, &st)) {
		if (errno == ENOENT) {
			return watchpoint_install_sentinel(wpt);
		} else {
			diag(LOG_ERR, _("cannot set watcher on %s: %s"),
			     wpt->dirname, strerror(errno));
			return 1;
		}
	}

	wpt->isdir = S_ISDIR(st.st_mode);
	
	for_each_handler(wpt, itr, hp) {
		mask.sys_mask |= hp->ev_mask.sys_mask;
		mask.gen_mask |= hp->ev_mask.gen_mask;
	}
	debug(1, ("%s: gen=%x,sys=%x", wpt->dirname, mask.sys_mask, mask.gen_mask));

	wd = sysev_add_watch(wpt, mask);
	if (wd == -1) {
		diag(LOG_ERR, _("cannot set watcher on %s: %s"),
		     wpt->dirname, strerror(errno));
		return 1;
	}

	wpt->wd = wd;

	return 0;
}

/* Deliver GENEV_CREATE event */
void
deliver_ev_create(struct watchpoint *wp, const char *dirname, const char *name,
		  int notify)
{
	event_mask m = { GENEV_CREATE, 0 };
	struct handler *hp;
	handler_iterator_t itr;

	if (watchpoint_recent_lookup(wp, name))
		return;
	debug(1, ("delivering CREATE for %s %s", dirname, name));
	for_each_handler(wp, itr, hp) {
		if (handler_matches_event(hp, gen, GENEV_CREATE, name))
			if (notify || hp->notify_always)
				hp->run(wp, &m, dirname, name, hp->data, notify);
	}
}

int
watchpoint_pattern_match(struct watchpoint *wpt, const char *file_name)
{
	struct handler *hp;
	handler_iterator_t itr;

	for_each_handler(wpt, itr, hp) {
		if (filpatlist_match(hp->fnames, file_name) == 0)
			return 0;
	}
	return 1;
}

/* Recursively scan subdirectories of parent and add them to the
   watcher list, as requested by the parent's recursion depth value. */
static int
watch_subdirs(struct watchpoint *parent, int notify)
{
	DIR *dir;
	struct dirent *ent;
	int filemask;
	int total = 0;

	if (!parent->isdir)
		return 0;

	debug(1, ("watch_subdirs: %s", parent->dirname));
	filemask = sysev_filemask(parent);
	if (parent->depth)
		filemask |= S_IFDIR;
	else
		filemask &= ~S_IFDIR;
	if (filemask == 0 && !notify) {
		debug(1, ("watch_subdirs %s finished at %d", parent->dirname,
			      __LINE__));
		return 0;
	}
	
	dir = opendir(parent->dirname);
	if (!dir) {
		diag(LOG_ERR, _("cannot open directory %s: %s"),
		     parent->dirname, strerror(errno));
		debug(1, ("watch_subdirs %s finished at %d", parent->dirname,
			      __LINE__));
		return 0;
	}

	while (ent = readdir(dir)) {
		struct stat st;
		char *dirname;

		if (ent->d_name[0] == '.' &&
		    (ent->d_name[1] == 0 ||
		     (ent->d_name[1] == '.' && ent->d_name[2] == 0)))
			continue;
		
		dirname = mkfilename(parent->dirname, ent->d_name);
		if (!dirname) {
			diag(LOG_ERR,
			     _("cannot stat %s/%s: not enough memory"),
			     parent->dirname, ent->d_name);
			continue;
		}
		if (watchpoint_lookup(dirname))
			/* Skip existing watchpoint */;
		else if (stat(dirname, &st)) {
			diag(LOG_ERR, _("cannot stat %s: %s"),
			     dirname, strerror(errno));
		} else if (watchpoint_pattern_match(parent, ent->d_name)
			   == 0) {
			deliver_ev_create(parent, parent->dirname,
					  ent->d_name, notify);
		}
		free(dirname);
	}
	closedir(dir);
	debug(1, ("watch_subdirs %s finished at %d", parent->dirname,
		  __LINE__));
	return total;
}


static int
setwatcher(void *ent, void *data)
{
	struct wpref *wpref = (struct wpref *) ent;
	struct watchpoint *wpt = wpref->wpt;
	
	debug(1, ("watchpoint_init: from setwatcher (%d)", __LINE__));
	if (wpt->wd == -1 && watchpoint_init(wpt) == 0)
		watch_subdirs(wpt, 0);
	return 0;
}

static int
checkwatcher(void *ent, void *data)
{
	struct wpref *wpref = (struct wpref *) ent;
	struct watchpoint *wpt = wpref->wpt;
	return wpt->wd >= 0;
}
	
void
setup_watchers(void)
{
	sysev_init();
	if (grecs_symtab_count(nametab) == 0) {
		diag(LOG_CRIT, _("no event handlers configured"));
		exit(1);
	}
	grecs_symtab_foreach(nametab, setwatcher, NULL);
	if (!grecs_symtab_foreach(nametab, checkwatcher, NULL)) {
		diag(LOG_CRIT, _("no event handlers installed"));
		exit(2);
	}
}

static int
stopwatcher(void *ent, void *data)
{
	struct wpref *wpref = (struct wpref *) ent;
	struct watchpoint *wpt = wpref->wpt;
	if (wpt->wd != -1) {
		debug(1, (_("removing watcher %s"), wpt->dirname));
		sysev_rm_watch(wpt);
	}
	return 0;
}

void
shutdown_watchers(void)
{
	grecs_symtab_foreach(nametab, stopwatcher, NULL);
	grecs_symtab_clear(nametab);
}


char *
split_pathname(struct watchpoint *dp, char **dirname)
{
	char *p = strrchr(dp->dirname, '/');
	if (p) {
		dp->split_p = p;
		*p++ = 0;
		*dirname = dp->dirname;
	} else {
		p = dp->dirname;
		*dirname = ".";
	}
	return p;
}

void
unsplit_pathname(struct watchpoint *dp)
{
	if (dp->split_p) {
		*dp->split_p = '/';
		dp->split_p = NULL;
	}
}
