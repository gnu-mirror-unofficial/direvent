/* Environment modification functions for GNU direvent.
   Copyright (C) 2019-2021 Sergey Poznyakoff

   GNU direvent is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GNU direvent is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GNU direvent.  If not, see <http://www.gnu.org/licenses/>. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include "envop.h"
#include "wordsplit.h"

environ_t *
environ_create (char **def)
{
  size_t i;
  environ_t *env = malloc (sizeof (*env));

  if (!env)
    return NULL;

  if (!def)
    {
      static char *nullenv[] = { NULL };
      def = nullenv;
    }
  
  for (i = 0; def[i]; i++)
    ;
  env->env_count = 0;
  env->env_max = i + 1;
  env->env_base = calloc (env->env_max, sizeof (env->env_base[0]));
  if (!env->env_base)
    {
      free (env);
      return NULL;
    }
  
  for (i = 0; def[i]; i++)
    {
      if (!(env->env_base[i] = strdup (def[i])))
	{
	  environ_free (env);
	  return NULL;
	}
      env->env_count++;
    }
  env->env_base[i] = NULL;
  return env;
}

void
environ_free (environ_t *env)
{
  size_t i;
  for (i = 0; i < env->env_count; i++)
    free (env->env_base[i]);
  free (env->env_base);
  free (env);
}

static ssize_t
getenvind (environ_t *env, char const *name, char **pval)
{
  size_t i;
  
  for (i = 0; i < env->env_count; i++)
    {
      char const *p;
      char *q;
      
      for (p = name, q = env->env_base[i]; *p == *q; p++, q++)
	if (*p == '=')
	  break;
      if ((*p == 0 || *p == '=') && *q == '=')
	{
	  if (pval)
	    *pval = q + 1;
	  return i;
	}
    }
  return -1;
}

static ssize_t
environ_alloc (environ_t *env)
{
  size_t n;
  if (env->env_count + 1 >= env->env_max)
    {
      char **p;
      if (env->env_base == NULL)
	{
	  n = 64;
	  p = calloc (n, sizeof (p[0]));
	  if (!p)
	    return -1;
	}
      else
	{
	  n = env->env_max;
	  if ((size_t) -1 / 3 * 2 / sizeof (p[0]) <= n)
	    {
	      errno = ENOMEM;
	      return -1;
	    }
	  n += (n + 1) / 2;
	  p = realloc (env->env_base, n * sizeof (p[0]));
	  if (!p)
	    return -1;
	}
      env->env_base = p;
      env->env_max = n;
    }
  n = env->env_count++;
  env->env_base[env->env_count] = NULL;
  return n;
}

static int
environ_add_alloced (environ_t *env, char *def)
{
  ssize_t n;
  n = getenvind (env, def, NULL);
  if (n == -1)
    {
      n = environ_alloc (env);
      if (n == -1)
	return -1;
    }
  free (env->env_base[n]);
  env->env_base[n] = def;
  return 0;
}

char const *
environ_get (environ_t *env, char const *name)
{
  char *val;
  if (getenvind (env, name, &val) == -1)
    return NULL;
  return val;
}
  
int
environ_add (environ_t *env, char const *def)
{
  char *defcp = strdup (def);
  if (!defcp)
    return -1;
  if (environ_add_alloced (env, defcp))
    {
      free (defcp);
      return -1;
    }
  return 0;
}
  
int
environ_set (environ_t *env, char const *name, char const *value)
{
  size_t len;
  char *def;
  struct wordsplit ws;

  if (!value)
    {
      if (!name)
	{
	  errno = EINVAL;
	  return -1;
	}
      return environ_unset (env, name, value);
    }

  ws.ws_env = (char const **) env->env_base;
  if (wordsplit (value, &ws,
		 WRDSF_NOSPLIT
		 | WRDSF_QUOTE
		 | WRDSF_NOCMD /* FIXME */
		 | WRDSF_SQUEEZE_DELIMS
		 | WRDSF_CESCAPES
		 | WRDSF_ENV
		 | WRDSF_PATHEXPAND))
    {
      int ec = errno;
      wordsplit_free (&ws);
      errno = ec;
      return -1;
    }

  if (ws.ws_envbuf)
    {
      free (env->env_base);
      env->env_base = ws.ws_envbuf;
      env->env_count = ws.ws_envidx;
      env->env_max = ws.ws_envsiz;
      ws.ws_envbuf = NULL;
      ws.ws_envidx = 0;
      ws.ws_envsiz = 0;
    }

  if (name == NULL || strcmp (name, ":") == 0)
    {
      wordsplit_free (&ws);
      return 0;
    }
  
  len = strlen (name) + strlen (ws.ws_wordv[0]) + 2;
  def = malloc (len);
  if (!def)
    {
      int ec = errno;
      wordsplit_free (&ws);
      errno = ec;
      return -1;
    }
  strcat (strcat (strcpy (def, name), "="), ws.ws_wordv[0]);
  wordsplit_free (&ws);
  if (environ_add_alloced (env, def))
    {
      free (def);
      return -1;
    }
  return 0;
}
  
int
environ_unset (environ_t *env, char const *name, char const *refval)
{
  ssize_t n;
  char *val;

  if (!env || !name)
    {
      errno = EINVAL;
      return -1;
    }
  n = getenvind (env, name, &val);
  if (n == -1)
    return ENOENT;
  if (refval && strcmp (val, refval))
    return ENOENT;

  free (env->env_base[n]);
  memmove (env->env_base + n, env->env_base + n + 1,
	   (env->env_count - n) * sizeof (env->env_base[0]));
  env->env_count--;
  return 0;
}

int
environ_unset_glob (environ_t *env, const char *pattern)
{
  size_t i;

  if (!env || !pattern)
    {
      errno = EINVAL;
      return -1;
    }
  for (i = 0; i < env->env_count; )
    {
      size_t len = strcspn (env->env_base[i], "=");
      if (wildmatch (pattern, env->env_base[i], len) == 0)
	{
	  free (env->env_base[i]);
	  memmove (env->env_base + i, env->env_base + i + 1,
		   (env->env_count - i) * sizeof (env->env_base[0]));
	  env->env_count--;
	}
      else
	i++;
    }
  return 0;
}

static void
envop_entry_insert (struct envop_entry **phead, struct envop_entry *op)
{
  struct envop_entry *head = *phead;
  
  if (!head)
    {
      *phead = op;
      return;
    }

  switch (op->code)
    {
    case envop_clear:
      if (head->code == envop_clear)
	free (op);
      else
	{
	  op->next = head;
	  *phead = op;
	}
      break;
  
    case envop_keep:
    {
      struct envop_entry *prev = NULL;
      while (head && head->code <= op->code)
	{
	  prev = head;
	  head = prev->next;
	}
      op->next = head;
      if (prev)
	prev->next = op;
      else
	*phead = op;
    }
    break;

    default:
      while (head && head->next)
	head = head->next;
      
      head->next = op;
    }
}

static int
valid_envar_name (char const *name)
{
  if (!name)
    return 0;
  if (!(isalpha (*name) || *name == '_'))
    return 0;
  while (*++name)
    {
      if (!(isalnum (*name) || *name == '_'))
	return 0;
    }
  return 1;
}

int
envop_entry_add (struct envop_entry **head,
		 enum envop_code code, char const *name, char const *value)
{
  struct envop_entry *op;
  size_t s;
  char *p;

  switch (code)
    {
    case envop_clear:
      break;
      
    case envop_set:
      if (name && !(*name == ':' || valid_envar_name (name)))
	{
	  errno = EINVAL;
	  return -1;
	}
      break;

    case envop_keep:
    case envop_unset:
      break;
      
    default:
      errno = EINVAL;
      return -1;
    }

  s = sizeof (op[0]);
  if (name)
    s += strlen (name) + 1;
  if (value)
    s += strlen (value) + 1;
  op = malloc (s);
  if (!op)
    return -1;
  op->next = NULL;
  op->code = code;
  op->name = NULL;
  op->value = NULL;

  p = (char*)(op + 1);
  if (name)
    {
      op->name = p;
      strcpy (op->name, name);
      p += strlen (name) + 1;
    }
  if (value)
    {
      op->value = p;
      strcpy (op->value, value);
    }
  envop_entry_insert (head, op);
  return 0;
}

static int
envopmatch (struct envop_entry *op, char const *var, int len)
{
  if (op->value)
    {
      if (strncmp (op->name, var, len) == 0)
	return strcmp (var + len + 1, op->value);
    }
  return wildmatch (op->name, var, len);
}

static int
keep_env (char const *var, struct envop_entry *keep)
{
  int len = strcspn (var, "=");
  for (; keep && keep->code == envop_keep; keep = keep->next)
    {
      if (envopmatch (keep, var, len) == 0)
	return 1;
    }
  return 0;
}

int
envop_exec (struct envop_entry *op, environ_t *env)
{
  size_t i;
  
  if (op && op->code == envop_clear)
    {
      op = op->next;
      if (op && op->code == envop_keep)
	{
	  size_t keep_count = 0;
	  for (i = 0; i < env->env_count; i++)
	    {
	      if (keep_env (env->env_base[i], op))
		{
		  if (i > keep_count)
		    {
		      env->env_base[keep_count] = env->env_base[i];
		      env->env_base[i] = NULL;
		    }
		  keep_count++;
		}
	      else
		{
		  free (env->env_base[i]);
		  env->env_base[i] = NULL;
		}
	    }
	  env->env_count = keep_count;
	}
      else
	{
	  size_t i;
	  for (i = 0; i < env->env_count; i++)
	    free (env->env_base[i]);
	  env->env_base[0] = 0;
	  env->env_count = 0;
	}
    }

  /* Process eventual set and unset statements */
  for (; op; op = op->next)
    {
      switch (op->code)
	{
	case envop_set:
	  if (environ_set (env, op->name, op->value))
	    return -1;
	  break;
		   
	case envop_unset:
	  if (op->value)
	    environ_unset (env, op->name, op->value);
	  else
	    environ_unset_glob (env, op->name);
	  break;

	case envop_keep:
	  break;
	  
	default:
	  abort ();
	}
    }

  return 0;
}

void
envop_free (struct envop_entry *op)
{
  while (op)
    {
      struct envop_entry *next = op->next;
      free (op);
      op = next;
    }
}

int
envop_cmp (struct envop_entry *a, struct envop_entry *b)
{
  while (a)
    {
      if (!b)
	return 1;
      if (a->code != b->code)
	return 1;
      switch (a->code)
	{
	case envop_clear:
	  break;
	  
	case envop_keep:
	case envop_set:
	case envop_unset:
	  if (strcmp (a->name, b->name))
	    return 1;

	  if (!a->value)
	    {
	      if (b->value)
		return 1;
	    }
	  else if (!b->value || strcmp (a->value, b->value))
	    return 1;
	  break;
	}
      a = a->next;
      b = b->next;
    }
  if (b)
    return 1;
  return 0;
}
