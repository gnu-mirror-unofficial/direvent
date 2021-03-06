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
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <sys/wait.h>
#include <ctype.h>
#include <grecs.h>
#include "wordsplit.h"

/* Process list */

/* Logger codes */
#define LOGGER_OUT 0
#define LOGGER_ERR 1

#define PROC_HANDLER  0
#define PROC_LOGGER    1
/* Special types for use in print_status: */
#define PROC_SELFTEST 2
#define PROC_FOREIGN  3

static char const *
process_type_string(int type)
{
	static char const *typestr[] = {
		"handler",
		"logger",
		"self-test",
		"foreign"
	};
	if (type >= 0 && type < sizeof(typestr) / sizeof(typestr[0]))
		return typestr[type];
	return "unknown!";
}

/* A running process is described by this structure */
struct process {
	struct process *next, *prev;
	int type;               /* Process type */
	unsigned timeout;       /* Timeout in seconds */
	pid_t pid;              /* PID */
	time_t start;           /* Time when the process started */
	union {
                /* Pointers to logger processes, if
		   type == PROC_HANDLER (NULL if no logger) */
		struct process *logger[2];
                /* Master process, if type == PROC_LOGGER */
		struct process *master;
	} v;
};

/* List of running processes */
struct process *proc_list;
/* List of available process slots */
struct process *proc_avail;

/* Declare functions for handling process lists */
struct process *
proc_unlink(struct process **root, struct process *p)
{
	if (p->prev)
		p->prev->next = p->next;
	else
		*root = p->next;
	if (p->next)
		p->next->prev = p->prev;
	p->next = p->prev = NULL;
	return p;
}

struct process *
proc_pop(struct process **pp)
{
	if (*pp)
		return proc_unlink(pp, *pp);
	return NULL;
}

void
proc_push(struct process **pp, struct process *p)
{
	p->prev = NULL;
	p->next = *pp;
	if (*pp)
		(*pp)->prev = p;
	*pp = p;
}


/* Process list handling (high-level) */

struct process *
register_process(int type, pid_t pid, time_t t, unsigned timeout)
{
	struct process *p;

	if (proc_avail)
		p = proc_pop(&proc_avail);
	else
		p = emalloc(sizeof(*p));
	memset(p, 0, sizeof(*p));
	p->type = type;
	p->timeout = timeout;
	p->pid = pid;
	p->start = t;
	proc_push(&proc_list, p);
	return p;
}

void
deregister_process(pid_t pid, time_t t)
{
	struct process *p;

	for (p = proc_list; p; p = p->next)
		if (p->pid == pid) {
			if (p->prev)
				p->prev->next = p->next;
			else
				proc_list = p;
			if (p->next)
				p->next->prev = p->prev;
			free(p);
			break;
		}
}

struct process *
process_lookup(pid_t pid)
{
	struct process *p;

	for (p = proc_list; p; p = p->next)
		if (p->pid == pid)
			return p;
	return NULL;
}

static void
print_status(pid_t pid, int status, int type, sigset_t *mask)
{
	if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) == 0) {
			debug(type == PROC_HANDLER ? 1 : 2,
			      (_("process %lu (%s) exited successfully"),
			       (unsigned long) pid,
			       process_type_string(type)));
		} else
			diag(LOG_ERR,
			     _("process %lu (%s) failed with status %d"),
			     (unsigned long) pid, process_type_string(type),
			     WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		int prio;
		char *core_status;
		
		if (sigismember(mask, WTERMSIG(status)))
			prio = LOG_DEBUG;
		else
			prio = LOG_ERR;

		if (WCOREDUMP(status)) {
			core_status = _(" (dumped core)");
		} else
			core_status = "";
		diag(prio, _("process %lu (%s) terminated on signal %d%s"),
		     (unsigned long) pid, process_type_string(type),
		     WTERMSIG(status), core_status);
	} else if (WIFSTOPPED(status))
		diag(LOG_ERR, _("process %lu (%s) stopped on signal %d"),
		     (unsigned long) pid, process_type_string(type),
		     WSTOPSIG(status));
	else
		diag(LOG_ERR,
		     _("process %lu (%s) terminated with unrecognized status"),
		     (unsigned long) pid, process_type_string(type));
}

void
process_cleanup(int expect_term)
{
	pid_t pid;
	int status;
	
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		sigset_t set;
		sigemptyset(&set);

		if (pid == self_test_pid) {
			sigaddset(&set, SIGHUP);
			print_status(pid, status, PROC_SELFTEST, &set);
			
			if (WIFEXITED(status))
				exit_code = WEXITSTATUS(status);
			else if (WIFSIGNALED(status)) {
				if (WTERMSIG(status) == SIGHUP)
					exit_code = 0;
				else
					exit_code = 2;
			} else
				exit_code = 2;
			stop = 1;
		} else {
			struct process *p = process_lookup(pid);

			if (expect_term)
				sigaddset(&set, SIGTERM);
			if (!p) {
				sigaddset(&set, SIGTERM);
				sigaddset(&set, SIGKILL);
			}
			print_status(pid, status, p ? p->type : PROC_FOREIGN,
				     &set);
			if (!p)
				continue;

			if (p->type == PROC_HANDLER) {
				if (p->v.logger[LOGGER_OUT])
					p->v.logger[LOGGER_OUT]->v.master = NULL;
				if (p->v.logger[LOGGER_ERR])
					p->v.logger[LOGGER_ERR]->v.master = NULL;
			}
			p->pid = 0;
			proc_unlink(&proc_list, p);
			proc_push(&proc_avail, p);
		}
	}
}

void
process_timeouts(void)
{
	struct process *p;
	time_t now = time(NULL);
	time_t alarm_time = watchpoint_recent_cleanup(), x;

	debug(3, (_("begin scanning process list")));
	for (p = proc_list; p; p = p->next) {
		x = now - p->start;
		if (x >= p->timeout) {
			diag(LOG_ERR, _("process %lu timed out"),
			     (unsigned long) p->pid);
			kill(p->pid, SIGKILL);
		} else if (alarm_time == 0 ||
			   p->timeout - x < alarm_time)
			alarm_time = p->timeout - x;
	}

	if (alarm_time) {
		debug(3, (_("scheduling alarm in %lu seconds"),
			  (unsigned long) alarm_time));
		alarm(alarm_time);
	}
	debug(3, ("%s", _("end scanning process list")));
}

int
switchpriv(struct prog_handler *hp)
{
	if (hp->uid == 0 || hp->uid == getuid())
		return 0;
	
	if (setgroups(hp->gidc, hp->gidv) < 0) {
		diag(LOG_CRIT, "setgroups: %s",
		     strerror(errno));
		return 1;
	}
	if (setregid(hp->gidv[0], hp->gidv[0]) < 0) {
		diag(LOG_CRIT, "setregid(%lu,%lu): %s",
		     (unsigned long) hp->gidv[0],
		     (unsigned long) hp->gidv[0],
		     strerror(errno));
		return 1;
	}
	if (setreuid(hp->uid, hp->uid) < 0) {
		diag(LOG_CRIT, "setreuid(%lu,%lu): %s",
		     (unsigned long) hp->uid,
		     (unsigned long) hp->uid,
		     strerror(errno));
		return 1;
	}
	return 0;
}		

/* Operations with handlers and loggers */

static void
logger_exit(int sig)
{
	_exit(0);
}

int
open_logger(const char *tag, int prio, struct process **return_proc)
{
	int p[2];
	FILE *fp;
	char buf[512];
	pid_t pid;
	int i;
	
	if (pipe(p)) {
		diag(LOG_ERR,
		     _("cannot start logger for %s, pipe failed: %s"),
		     tag, strerror(errno));
		return -1;
	}
	switch (pid = fork()) {
	case 0:
		/* Logger process */
		close_fds(p[0] + 1);
		for (i = 3; i < p[0]; i++)
			close(i);
		alarm(0);
		signal_setup(logger_exit);

		fp = fdopen(p[0], "r");
		if (fp == NULL)
			_exit(1);
		if (facility > 0) 
			openlog(tag, LOG_PID, facility);

		while (fgets(buf, sizeof(buf), fp)) {
			int len = strlen(buf);
			if (len && buf[len-1] == '\n')
				buf[len-1] = 0;
			diag(prio, "%s", buf);
		}
		_exit(0);
      
	case -1:
		diag(LOG_CRIT,
		     _("cannot run logger `%s': fork failed: %s"),
		     tag, strerror(errno));
		return -1;

	default:
		debug(3, (_("logger for %s started, pid=%lu"),
			  tag, (unsigned long) pid));
		close(p[0]);
		*return_proc = register_process(PROC_LOGGER, pid, 
						time(NULL), 0);
		return p[1];
	}
}

extern char **environ;    /* Environment */

enum {
	ENV_FILE,
	ENV_SYSEV_CODE,
	ENV_SYSEV_NAME,
	ENV_GENEV_CODE,
	ENV_GENEV_NAME,
	ENV_SELF_TEST_PID,
	DEFENV_COUNT
};

static struct defenv {
	char *macro_name;
	char *envar_name;
	char *value;
} defenv[DEFENV_COUNT] = {
	[ENV_FILE]       = { "file", "DIREVENT_FILE" },
	[ENV_SYSEV_CODE] = { "sysev_code", "DIREVENT_SYSEV_CODE" },
	[ENV_SYSEV_NAME] = { "sysev_name", "DIREVENT_SYSEV_NAME" },
	[ENV_GENEV_CODE] = { "genev_code", "DIREVENT_GENEV_CODE" },
	[ENV_GENEV_NAME] = { "genev_name", "DIREVENT_GENEV_NAME" },
	[ENV_SELF_TEST_PID] = { "self_test_pid", "DIREVENT_SELF_TEST_PID" },
};

int
parse_legacy_env(char **argv, envop_t **envop)
{
	size_t i = 0;
	char *name;

	if (strcmp(argv[0], "-") == 0 || strcmp(argv[0], "--") == 0) {
		int j;

		if (envop_entry_add(envop, envop_clear, NULL, NULL))
			return -1;
		i++;

		for (j = 0; j < DEFENV_COUNT; j++) {
			if (envop_entry_add(envop, envop_keep,
					    defenv[j].macro_name, NULL))
				return -1;
		}
		
		if (argv[0][1] == 0) {
			for (j = 0; j < DEFENV_COUNT; j++) {
				if (envop_entry_add(envop, envop_keep,
						    defenv[j].envar_name, NULL))
					return -1;
			}
		}
	}
	for (; (name = argv[i]) != NULL; i++) {
		char *name = argv[i];
		size_t len = strcspn(name, "=");
		char *value;
		char *mem = NULL;
		size_t msize = 0;
		enum envop_code code;
		int rc;
		
		if (name[0] == '-') {
			/* Unset directive */
			name++;
			len--;
			
			if (name[len]) {
				name[len] = 0;
				value = name + len + 1;
			} else
				value = NULL;

			code = envop_unset;
		} else if (name[len]) {
			size_t vlen;

			if (len == 0)
                                /* Skip erroneous entry */
				continue;
			value = name + len + 1;
			vlen = strlen(value);
			name[len] = 0;
			if (name[len-1] == '+') {
				name[--len] = 0;
				if (ispunct(value[0])) {
					msize = 2*len + 9 + vlen + 1;
					mem = grecs_malloc(msize);
					snprintf(mem, msize,
						 "${%s:-}${%s:+%c}%s",
						 name, name, value[0],
						 value + 1);
				} else {
					msize = len + vlen + 6;
					mem = grecs_malloc(msize);
					snprintf(mem, msize,
						 "${%s:-}%s",
						 name, value);
				}
				value = mem;
			} else if (value[0] == '+') {
				value++;
				vlen--;

				if (vlen > 0 && ispunct(value[vlen-1])) {
					int c = value[vlen-1];
					value[--vlen] = 0;

					msize = 2*len + 10 + vlen + 1;
					mem = grecs_malloc(msize);
					snprintf(mem, msize,
						 "%s${%s:+%c}${%s:-}",
						 value, name, c, name);
				} else {
					msize = len + vlen + 6;
					mem = grecs_malloc(msize);
					snprintf(mem, msize,
						 "%s${%s:-}",
						 value, name);
				}
				value = mem;
			}
			code = envop_set;
		} else {
			value = NULL;
			code = envop_keep;
		}
		rc = envop_entry_add(envop, code, name, value);
		free(mem);
		if (rc)
			return -1;
	}
	return 0;
}

static inline void
debug_environ(int lev, environ_t *env, char *text)
{
	if (debug_level >= lev) {
		int i;
		char **envp = environ_ptr(env);
		diag(LOG_DEBUG, "%s: ", text);
		for (i = 0; envp[i]; i++)
			diag(LOG_DEBUG, "%d: %s", i, envp[i]);
	}
}

static int
runcmd_getmacro(char **ret, const char *var, size_t len, void *clos)
{
	int i;
	struct defenv *de = clos;

	for (i = 0; i < DEFENV_COUNT; i++, de++) {
		if (len == strlen(de->macro_name) &&
		    memcmp(var, de->macro_name, len) == 0) {
			if (de->value) {
				if ((*ret = strdup(de->value)) == NULL)
					return WRDSE_NOSPACE;
			} else {
				*ret = NULL;
			}
			return WRDSE_OK;
		}
	}
	return WRDSE_UNDEF;
}

static void
runcmd(struct prog_handler *hp, event_mask *event, const char *file)
{
	char buf[1024];
	char **argv;
	environ_t *env;
	char *xargv[4];
	struct wordsplit ws;
	int wsflags;
	int i;

	/*
	 * Fill in the default environment and macro variable values.
	 */
	
	defenv[ENV_FILE].value = (char*) file;
	
	snprintf(buf, sizeof buf, "%d", event->sys_mask);
	defenv[ENV_SYSEV_CODE].value = estrdup(buf);

	snprintf(buf, sizeof buf, "%d", event->gen_mask);
	defenv[ENV_GENEV_CODE].value = estrdup(buf);

	if (ev_format(*event,
		      &defenv[ENV_GENEV_NAME].value,
		      &defenv[ENV_SYSEV_NAME].value))
		nomem_abend();

	if (self_test_pid) {
		snprintf(buf, sizeof buf, "%lu", (unsigned long)self_test_pid);
		defenv[ENV_SELF_TEST_PID].value = estrdup(buf);
	}

	/*
	 * Initialize the environment.
	 */
	env = environ_create(environ);
	for (i = 0; i < DEFENV_COUNT; i++) {
		environ_set(env, defenv[i].envar_name, defenv[i].value);
		/*
		 * NOTE: Version 5.2 macro names are temporarily defined as
		 * environment variables so that they be properly expanded
		 * by the envop_exec calls below.
		 */
		environ_set(env, defenv[i].macro_name, defenv[i].value);
	}

	/*
	 * Modify it according to the "environ" configuration statements.
	 */
	if (envop_exec(direvent_envop, env) || envop_exec(hp->envop, env)) {
		diag(LOG_CRIT, "envop_exec failed: %s", strerror(errno));
		_exit(127);
	}
	
	/* Unset macro names.  See the NOTE above. */
 	for (i = 0; i < DEFENV_COUNT; i++)
		environ_unset(env, defenv[i].macro_name, NULL);

	debug_environ(4, env, "modified environment");

	ws.ws_getvar = runcmd_getmacro;
	ws.ws_closure = defenv;
	wsflags = WRDSF_NOCMD | WRDSF_QUOTE
		| WRDSF_SQUEEZE_DELIMS | WRDSF_CESCAPES
		| WRDSF_GETVAR | WRDSF_CLOSURE | WRDSF_KEEPUNDEF;
	if (hp->flags & HF_SHELL) {
		wsflags |= WRDSF_NOSPLIT;
	} else {
		ws.ws_env = (const char **) environ_ptr(env);
		wsflags |= WRDSF_ENV;
	}
	if (wordsplit(hp->command, &ws, wsflags)) {
		diag(LOG_CRIT, "wordsplit: %s",
		     wordsplit_strerror(&ws));
		_exit(127);
	}
	
	if (hp->flags & HF_SHELL) {
		xargv[0] = (char*) environ_get(env, "SHELL");
		if (!xargv[0])
			xargv[0] = "/bin/sh";
		xargv[1] = "-c";
		xargv[2] = ws.ws_wordv[0];
		xargv[3] = NULL;
		argv = xargv;
	} else {
		argv = ws.ws_wordv;
	}

	execve(argv[0], argv, environ_ptr(env));

	diag(LOG_ERR, "execve: %s \"%s\": %s", argv[0], hp->command,
	     strerror(errno));
	_exit(127);
}

static int
prog_handler_run(struct watchpoint *wp, event_mask *event,
		 const char *dirname, const char *file, void *data, int notify)
{
	pid_t pid;
	int logger_fd[2] = { -1, -1 };
	struct process *logger_proc[2] = { NULL, NULL };
	struct process *p;
	struct prog_handler *hp = data;

	if (!hp->command || !notify)
		return 0;
	
	debug(1, (_("starting %s, dir=%s, file=%s"),
		  hp->command, dirname, file));
	if (hp->flags & HF_STDERR)
		logger_fd[LOGGER_ERR] = open_logger(hp->command, LOG_ERR,
						    &logger_proc[LOGGER_ERR]);
	if (hp->flags & HF_STDOUT)
		logger_fd[LOGGER_OUT] = open_logger(hp->command, LOG_INFO,
						    &logger_proc[LOGGER_OUT]);
	
	pid = fork();
	if (pid == -1) {
		diag(LOG_ERR, "fork: %s", strerror(errno));
		close(logger_fd[LOGGER_OUT]);
		close(logger_fd[LOGGER_ERR]);
		if (logger_proc[LOGGER_OUT])
			kill(logger_proc[LOGGER_OUT]->pid, SIGKILL);
		if (logger_proc[LOGGER_ERR])
			kill(logger_proc[LOGGER_ERR]->pid, SIGKILL);
		return -1;
	}
	
	if (pid == 0) {		
		/* child */
		int keepfd[3] = { 0, 0, 0 };
		
		if (switchpriv(hp))
			_exit(127);
		
		if (chdir(dirname)) {
			diag(LOG_CRIT, _("cannot change to %s: %s"),
			     dirname, strerror(errno));
			_exit(127);
		}

		if (logger_fd[LOGGER_OUT] != -1) {
			if (logger_fd[LOGGER_OUT] != 1 &&
			    dup2(logger_fd[LOGGER_OUT], 1) == -1) {
				diag(LOG_ERR, "dup2: %s", strerror(errno));
				_exit(127);
			}
			keepfd[1] = 1;
		}
		if (logger_fd[LOGGER_ERR] != -1) {
			if (logger_fd[LOGGER_ERR] != 2 &&
			    dup2(logger_fd[LOGGER_ERR], 2) == -1) {
				diag(LOG_ERR, "dup2: %s", strerror(errno));
				_exit(127);
			}
			keepfd[2] = 1;
		}
		close_fds(3);
		close(0);
		if (!keepfd[1])
			close(1);
		if (!keepfd[2])
			close(2);
		alarm(0);
		signal_setup(SIG_DFL);
		runcmd(hp, event, file);
	}

	/* master */
	debug(1, (_("%s running; dir=%s, file=%s, pid=%lu"),
		  hp->command, dirname, file, (unsigned long)pid));

	p = register_process(PROC_HANDLER, pid, time(NULL), hp->timeout);

	if (logger_proc[LOGGER_OUT]) {
		logger_proc[LOGGER_OUT]->v.master = p;
		logger_proc[LOGGER_OUT]->timeout = hp->timeout;
	}
	if (logger_proc[LOGGER_ERR]) {
		logger_proc[LOGGER_ERR]->v.master = p;
		logger_proc[LOGGER_ERR]->timeout = hp->timeout;
	}
	memcpy(p->v.logger, logger_proc, sizeof(p->v.logger));
	
	close(logger_fd[LOGGER_OUT]);
	close(logger_fd[LOGGER_ERR]);

	if (hp->flags & HF_NOWAIT) {
		return 0;
	}

	debug(2, (_("waiting for %s (%lu) to terminate"),
		  hp->command, (unsigned long)pid));
	while (time(NULL) - p->start < 2 * p->timeout) {
		sleep(1);
		process_cleanup(1);
		if (p->pid == 0)
			break;
	}
	return 0;
}

void
prog_handler_free(struct prog_handler *hp)
{
	free(hp->command);
	free(hp->gidv);
	envop_free(hp->envop);
}

static void
prog_handler_free_data(void *ptr)
{
	prog_handler_free((struct prog_handler *)ptr);
}

struct handler *
prog_handler_alloc(event_mask ev_mask, filpatlist_t fpat,
		   struct prog_handler *p)
{
	struct handler *hp = handler_alloc(ev_mask);
	struct prog_handler *mem;

	hp->fnames = fpat;
	hp->run = prog_handler_run;
	hp->free = prog_handler_free_data;
	hp->notify_always = 0;
	mem = emalloc(sizeof(*mem));
	*mem = *p;
	hp->data = mem;
	memset(p, 0, sizeof(*p));
	return hp;
}
		
