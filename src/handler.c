/* direvent - directory content watcher daemon
   Copyright (C) 2012-2021 Sergey Poznyakoff

   GNU direvent is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3 of the License, or (at your
   option) any later version.

   GNU direvent is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with direvent. If not, see <http://www.gnu.org/licenses/>. */

#include "direvent.h"
#include <grecs.h>

struct handler *
handler_alloc(event_mask ev_mask)
{
	struct handler *hp = ecalloc(1, sizeof(*hp));
	hp->refcnt = 0;
	hp->ev_mask = ev_mask;
	return hp;
}

void
watchpoint_run_handlers(struct watchpoint *wp, event_mask event,
			const char *dirname, const char *filename)
{
	handler_iterator_t itr;
	struct handler *hp;
	event_mask m;

	for_each_handler(wp, itr, hp) {
		if (evtand(&event, &hp->ev_mask, &m) &&
		    filpatlist_match(hp->fnames, filename) == 0) {
			hp->run(wp, &m, dirname, filename, hp->data, 1);
		}
	}
}

static void
handler_ref(struct handler *hp)
{
	++hp->refcnt;
}

void
handler_free(struct handler *hp)
{
	filpatlist_destroy(&hp->fnames);
	if (hp->free)
		hp->free(hp->data);
}

static void
handler_unref(struct handler *hp)
{
	if (hp && --hp->refcnt == 0) {
		handler_free(hp);
		free(hp);
	}
}

/* Handler lists */

static void
handler_listent_free(void *p)
{
	struct handler *hp = p;
	handler_unref(hp);
}

struct handler_list {
	size_t refcnt;
	grecs_list_ptr_t list;
	struct handler_iterator *itr_chain;
};

struct handler_iterator {
	struct handler_iterator *prev, *next;
	handler_list_t hlist;
	struct grecs_list_entry *ent;
	int advanced;
};

static struct handler_iterator *itr_avail;

struct handler *
handler_itr_first(struct watchpoint *wpt, handler_iterator_t *ret_itr)
{
	struct handler_iterator *itr;
		
	if (!wpt->handler_list)
		return NULL;

	if (itr_avail) {
		itr = itr_avail;
		itr_avail = itr->next;
		if (itr_avail)
			itr_avail->prev = NULL;
	} else 
		itr = emalloc(sizeof *itr);

	itr->prev = NULL;
	itr->next = wpt->handler_list->itr_chain;
	itr->hlist = wpt->handler_list;
	if (wpt->handler_list->itr_chain)
	    wpt->handler_list->itr_chain->prev = itr;
	wpt->handler_list->itr_chain = itr;

	itr->ent = wpt->handler_list->list->head;
	itr->advanced = 0;
	*ret_itr = itr;
	return handler_itr_current(itr);
}

struct handler *
handler_itr_next(handler_iterator_t *pitr)
{
	struct handler_iterator *itr;
	
	if (!pitr || (itr = *pitr) == NULL)
		return NULL;
	if (itr->advanced)
		itr->advanced = 0;
	else 
		itr->ent = itr->ent->next;

	if (!itr->ent) {
		/* Remove from iterator chain */
		struct handler_iterator *p;
		if ((p = itr->prev) != NULL)
			p->next = itr->next;
		else
			itr->hlist->itr_chain = itr->next;
		if ((p = itr->next) != NULL)
			p->prev = itr->prev;
			
		/* Add to the available chain */
		if (itr_avail)
			itr_avail->prev = itr;
		itr->prev = NULL;
		itr->next = itr_avail;
		itr->hlist = NULL;
		itr_avail = itr;
		*pitr = NULL;
		return NULL;
	}
	return handler_itr_current(itr);
}
		
struct handler *
handler_itr_current(handler_iterator_t itr)
{
	if (!itr)
		return NULL;
	return itr->ent ? itr->ent->data : NULL;
}

handler_list_t
handler_list_create(void)
{
	handler_list_t hlist = emalloc(sizeof(*hlist));
	hlist->list = grecs_list_create();
	hlist->list->free_entry = handler_listent_free;
	hlist->refcnt = 1;
	hlist->itr_chain = NULL;
	return hlist;
}

size_t
handler_list_size(handler_list_t hlist)
{
	return grecs_list_size(hlist->list);
}

handler_list_t
handler_list_copy(handler_list_t orig)
{
	if (!orig)
		return handler_list_create();
	++orig->refcnt;
	return orig;
}

void
handler_list_unref(handler_list_t hlist)
{
	if (hlist) {
		if (--hlist->refcnt == 0) {
			grecs_list_free(hlist->list);
			free(hlist);
		}
	}
}

static void
handler_list_dup(handler_list_t *phlist)
{
	handler_list_t hlist = handler_list_create();
	struct grecs_list_entry *ent;
	//FIXME: Use iterator?
	//FIXME: Optimization: check for ent->data==hp and skip it
	for (ent = (*phlist)->list->head; ent; ent = ent->next) {
		handler_list_append(hlist, ent->data);
	}
	*phlist = hlist;
}

void
handler_list_append(handler_list_t hlist, struct handler *hp)
{
	handler_ref(hp);
	grecs_list_append(hlist->list, hp);
}

void
handler_list_append_cow(handler_list_t *phlist, struct handler *hp)
{
	handler_list_t hlist = *phlist;
	if (!hlist) {
		hlist = *phlist = handler_list_create();
	} else if (hlist->refcnt > 1) {
		handler_list_dup(phlist);
		hlist = *phlist;
	}
	handler_list_append(hlist, hp);
}

size_t
handler_list_remove(handler_list_t hlist, struct handler *hp)
{
	struct grecs_list_entry *ep;
	for (ep = hlist->list->head; ep; ep = ep->next)
		if (ep->data == hp)
			break;
	if (!ep)
		abort();

	if (hlist->itr_chain) {
		struct handler_iterator *itr;

		for (itr = hlist->itr_chain; itr; itr = itr->next)
			if (itr->ent == ep) {
				itr->ent = ep->next;
				itr->advanced = 1;
			}
	}
	
	grecs_list_remove_entry(hlist->list, ep);
	return grecs_list_size(hlist->list);
}

size_t
handler_list_remove_cow(handler_list_t *phlist, struct handler *hp)
{
	handler_list_t hlist = *phlist;
	struct grecs_list_entry *ep;

	if (!hlist)
		return 0;
	else if (hlist->refcnt > 1) {
		handler_list_dup(phlist);
		hlist = *phlist;
	}
	
	for (ep = hlist->list->head; ep; ep = ep->next)
		if (ep->data == hp)
			break;
	if (!ep)
		abort();

		
	if (hlist->itr_chain) {
		struct handler_iterator *itr;

		for (itr = hlist->itr_chain; itr; itr = itr->next)
			if (itr->ent == ep) {
				itr->ent = ep->next;
				itr->advanced = 1;
			}
	}
	
	grecs_list_remove_entry(hlist->list, ep);
	return grecs_list_size(hlist->list);
}

