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

#include "config.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <regex.h>
#include <grecs/list.h>
#include <grecs/symtab.h>
#include <envop.h>
#include "gettext.h"

#define _(s) gettext(s)
#define N_(s) s

/* Generic (system-independent) event codes */
#define GENEV_CREATE   0x01
#define GENEV_WRITE    0x02
#define GENEV_ATTRIB   0x04
#define GENEV_DELETE   0x08
#define GENEV_CHANGE   0x10

/* Handler flags. */
#define HF_NOWAIT  0x01   /* Don't wait for termination */
#define HF_STDOUT  0x02   /* Capture stdout */
#define HF_STDERR  0x04   /* Capture stderr */
#define HF_SHELL   0x08   /* Call program via /bin/sh -c */ 

#ifndef DEFAULT_TIMEOUT
# define DEFAULT_TIMEOUT 5
#endif

typedef struct {
	int gen_mask;        /* Generic event mask */
	int sys_mask;        /* System event mask */
} event_mask;

/* Event description */
struct transtab {
	char *name;
	int tok;
};

enum pattern_type {
	PAT_EXACT,
	PAT_GLOB,
	PAT_REGEX
};

struct filename_pattern {
	enum pattern_type type;
	int neg;
	union {
		regex_t re;
		char *glob;
	} v;
};

typedef struct filpatlist *filpatlist_t;

struct watchpoint;

typedef int (*event_handler_fn) (struct watchpoint *wp,
				 event_mask *event,
				 const char *dir,
				 const char *file,
				 void *data,
				 int notify);
typedef void (*handler_free_fn) (void *data);

/* Handler structure */
struct handler {
	size_t refcnt;        /* Reference counter */
	event_mask ev_mask;   /* Event mask */
	filpatlist_t fnames;  /* File name patterns */
	event_handler_fn run;
	handler_free_fn free;
	void *data;
	int notify_always;
};

typedef struct handler_list *handler_list_t;
typedef struct handler_iterator *handler_iterator_t;

struct recent_head {
	struct watchpoint *prev;
	struct watchpoint *next;
	struct grecs_symtab *names;
	struct timeval tv;
};

/* Watchpoint links the directory being monitored and a list of
   handlers for various events: */
struct watchpoint {
	size_t refcnt;
	int wd;                              /* Watch descriptor */
	struct watchpoint *parent;           /* Points to the parent watcher.
					        NULL for top-level watchers */
	char *dirname;                       /* Pathname being watched */
	int isdir;                           /* Is it directory */
	handler_list_t handler_list;         /* List of handlers */
	int depth;                           /* Recursion depth */
	char *split_p;                       /* Points to the deleted directory
						separator in dirname (see
						split_pathname,
						unsplit_pathname */
	struct recent_head rhead;
#if USE_IFACE == IFACE_KQUEUE
	int file_changed;
	time_t file_ctime;
#else
	struct grecs_symtab *files_changed;	
#endif
};

struct handler *handler_alloc(event_mask ev_mask);
void handler_free(struct handler *hp);

struct prog_handler {
	int flags;     /* Handler flags */
	char *command; /* Handler command (with eventual arguments) */
	uid_t uid;     /* Run as this user (unless 0) */
	gid_t *gidv;   /* Run with these groups' privileges */
	size_t gidc;   /* Number of elements in gidv */
	unsigned timeout; /* Handler timeout */
	envop_t *envop;   /* Environment setup program */
};

struct handler *prog_handler_alloc(event_mask ev_mask, filpatlist_t fpat,
				   struct prog_handler *p);
void prog_handler_free(struct prog_handler *);


extern int foreground;
extern int debug_level;
extern int facility;
extern char *tag;
extern int syslog_include_prio;
extern char *pidfile;
extern char *user;
extern unsigned opt_timeout;
extern unsigned opt_flags;
extern int signo;
extern int stop;

extern pid_t self_test_pid;
extern int exit_code;
extern envop_t *direvent_envop;


void nomem_abend(void);
void *emalloc(size_t size);
void *ecalloc(size_t nmemb, size_t size);
void *erealloc(void *ptr, size_t size);
char *estrdup(const char *str);

char *mkfilename(const char *dir, const char *file);

void diag(int prio, const char *fmt, ...);
void debugprt(const char *fmt, ...);

#define debug(l, c) do { if (debug_level>=(l)) debugprt c; } while(0)

void signal_setup(void (*sf) (int));
int detach(void (*)(void));

int sysev_filemask(struct watchpoint *dp);
void sysev_init(void);
int sysev_add_watch(struct watchpoint *dwp, event_mask mask);
void sysev_rm_watch(struct watchpoint *dwp);
int sysev_select(void);
int sysev_name_to_code(const char *name);
const char *sysev_code_to_name(int code);

int getevt(const char *name, event_mask *mask);

void evtempty(event_mask *mask);
void evtfill(event_mask *mask);
int evtnullp(event_mask *mask);
int evtand(event_mask const *a, event_mask const *b, event_mask *res);
void evtrans_sys_to_gen(int fflags, event_mask const *xlat, event_mask *ret_evt);
int evtrans_gen_to_sys(event_mask const *event, event_mask const *xlat);

/* Translate generic event codes to symbolic names and vice-versa */
extern struct transtab genev_transtab[];
/* Translate system event codes to symbolic names and vice-versa */
extern struct transtab sysev_transtab[];

int trans_strtotok(struct transtab *tab, const char *str, int *ret);
char *trans_toktostr(struct transtab *tab, int tok);
char *trans_tokfirst(struct transtab *tab, int tok, int *next);
char *trans_toknext(struct transtab *tab, int tok, int *next);
int trans_fullmask(struct transtab *tab);

struct pathent {
	long depth;
	size_t len;
	char path[1];
};

void config_help(void);
void config_init(void);
void config_parse(const char *file);

int get_facility(const char *arg);
int get_priority(const char *arg);

int  watchpoint_init(struct watchpoint *dwp);
void watchpoint_ref(struct watchpoint *dw);
void watchpoint_unref(struct watchpoint *dw);
void watchpoint_gc(void);

int watchpoint_pattern_match(struct watchpoint *dwp, const char *file_name);

void watchpoint_run_handlers(struct watchpoint *wp, event_mask event,
			      const char *dirname, const char *filename);


void setup_watchers(void);
void shutdown_watchers(void);

struct watchpoint *watchpoint_lookup(const char *dirname);
struct watchpoint *watchpoint_install(const char *path, int *pnew);
struct watchpoint *watchpoint_install_ptr(struct watchpoint *dw);
void watchpoint_suspend(struct watchpoint *dwp);
void watchpoint_destroy(struct watchpoint *dwp);
int watchpoint_install_sentinel(struct watchpoint *dwp);
int watchpoint_attach_directory_sentinel(struct watchpoint *wpt);

int watch_pathname(struct watchpoint *parent, const char *dirname, int isdir,
		   int notify);

char *split_pathname(struct watchpoint *dp, char **dirname);
void unsplit_pathname(struct watchpoint *dp);

int ev_format(event_mask ev, char **gen, char **sys);
void ev_log(int prio, struct watchpoint *dp, event_mask ev, char *prefix);
void deliver_ev_create(struct watchpoint *dp,
		       const char *dirname, const char *filename,
		       int notify);
int subwatcher_create(struct watchpoint *parent, const char *dirname,
		      int notify);

void watchpoint_recent_init(struct watchpoint *wp);
void watchpoint_recent_deinit(struct watchpoint *wp);
int watchpoint_recent_lookup(struct watchpoint *wp, char const *name);
int watchpoint_recent_cleanup(void);

#define WATCHPOINT_RECENT_TTL 1


struct handler *handler_itr_first(struct watchpoint *dp,
				       handler_iterator_t *itr);
struct handler *handler_itr_next(handler_iterator_t *itr);
struct handler *handler_itr_current(handler_iterator_t itr);

#define for_each_handler(d,i,h)				\
	for (h = handler_itr_first(d, &(i));	\
	     h;						\
	     h = handler_itr_next(&(i)))
	

handler_list_t handler_list_create(void);
handler_list_t handler_list_copy(handler_list_t);
void handler_list_unref(handler_list_t hlist);
void handler_list_append(handler_list_t hlist, struct handler *hp);
void handler_list_append_cow(handler_list_t *phlist, struct handler *hp);
size_t handler_list_remove(handler_list_t hlist, struct handler *hp);
size_t handler_list_remove_cow(handler_list_t *phlist, struct handler *hp);
size_t handler_list_size(handler_list_t hlist);

struct process *process_lookup(pid_t pid);
void process_cleanup(int expect_term);
void process_timeouts(void);

#define NITEMS(a) ((sizeof(a)/sizeof((a)[0])))
struct sigtab {
	int signo;
	void (*sigfun)(int);
};

int sigv_set_action(int sigc, int *sigv, struct sigaction *sa);
int sigv_set_all(void (*handler)(int), int sigc, int *sigv,
		 struct sigaction *retsa);
int sigv_set_tab(int sigc, struct sigtab *sigtab, struct sigaction *retsa);
int sigv_set_action_tab(int sigc, struct sigtab *sigtab, struct sigaction *sa);

struct grecs_locus;
int filpatlist_add(filpatlist_t *fptr, char const *arg,
		   struct grecs_locus *loc);
void filpatlist_add_exact(filpatlist_t *fptr, char const *arg);
void filpatlist_destroy(filpatlist_t *fptr);
int filpatlist_match(filpatlist_t fp, const char *name);
int filpatlist_is_empty(filpatlist_t fp);

void close_fds(int minfd);

int parse_legacy_env(char **argv, envop_t **envop);
