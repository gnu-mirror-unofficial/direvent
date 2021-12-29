/* Environment and environment operation definitions for GNU direvent.
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

/* Environment structure */
struct environ
{
  char **env_base;
  size_t env_count;
  size_t env_max;
};
typedef struct environ environ_t;

environ_t *environ_create (char **);
void environ_free (environ_t *env);
char const *environ_get (environ_t *env, char const *name);
int environ_add (environ_t *env, char const *def);
int environ_set (environ_t *env, char const *name, char const *val);
int environ_unset (environ_t *env, char const *name, char const *val);
int environ_unset_glob (environ_t *env, const char *pattern);
static inline char **
environ_ptr (environ_t *env)
{
  return env->env_base;
}

/* Environment operation codes.
   Elements in a oplist are sorted in that order. */
enum envop_code
  {
    envop_clear,   /* Clear environment */
    envop_keep,    /* Keep variable when clearing */
    envop_set,     /* Set variable */
    envop_unset    /* Unset variable */
  };

struct envop_entry    /* Environment operation entry */
{
  struct envop_entry *next; /* Next entry in the list */
  enum envop_code code;     /* Operation code */
  char *name;               /* Variable name (or globbing pattern) */
  char *value;              /* Value of the variable */
};

typedef struct envop_entry envop_t;

int wildmatch (char const *expr, char const *name, size_t len);

int envop_entry_add (envop_t **head,
		     enum envop_code code,
		     char const *name, char const *value);

int envop_exec (envop_t *op, environ_t *env);
void envop_free (envop_t *op);
int envop_cmp (struct envop_entry *a, struct envop_entry *b);



