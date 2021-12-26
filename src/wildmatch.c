/* Environment-specific globbing pattern matching for GNU Pies.
   Copyright (C) 2019-2021 Sergey Poznyakoff

   GNU Pies is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GNU Pies is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GNU Pies.  If not, see <http://www.gnu.org/licenses/>. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdlib.h>
#include <wordsplit.h>

enum
  {
    WILD_FALSE = 0,
    WILD_TRUE,
    WILD_ABORT
  };

static int
match_char_class (char const **pexpr, char c)
{
  int res;
  int rc;
  char const *expr = *pexpr;
  
  expr++;
  if (*expr == '^')
    {
      res = 0;
      expr++;
    }
  else
    res = 1;

  if (*expr == '-' || *expr == ']')
    rc = c == *expr++;
  else
    rc = !res;
  
  for (; *expr && *expr != ']'; expr++)
    {
      if (rc == res)
	{
	  if (*expr == '\\' && expr[1] == ']')
	    expr++;
	}
      else if (expr[1] == '-')
	{
	  if (*expr == '\\')
	    rc = *++expr == c;
	  else
	    {
	      rc = *expr <= c && c <= expr[2];
	      expr += 2;
	    }
	}
      else if (*expr == '\\' && expr[1] == ']')
	rc = *++expr == c;
      else
	rc = *expr == c;
    }
  *pexpr = *expr ? expr + 1 : expr;
  return rc == res;
}

#define END_OF_NAME(s,l) ((l) == 0 || *(s) == 0)
#define NEXT_CHAR(s,l) (s++, l--)

int
wilder_match (char const *expr, char const *name, size_t len)
{
  int c;

  while (expr && *expr)
    {
      if (END_OF_NAME (name, len) && *expr != '*')
	return WILD_ABORT;
      switch (*expr)
	{
	case '*':
	  while (*++expr == '*')
	    ;
	  if (*expr == 0)
	    return WILD_TRUE;
	  while (!END_OF_NAME (name, len))
	    {
	      int res = wilder_match (expr, name, len);
	      if (res != WILD_FALSE)
		return res;
	      NEXT_CHAR (name, len);
	    }
	  return WILD_ABORT;
                        
	case '?':
	  expr++;
	  NEXT_CHAR (name, len);
	  break;
                        
	case '[':
	  if (!match_char_class (&expr, *name))
	    return WILD_FALSE;
	  NEXT_CHAR (name, len);
	  break;
	  
	case '\\':
	  if (expr[1])
	    {
	      c = *++expr; expr++;
	      if (*name != wordsplit_c_unquote_char (c))
		return WILD_FALSE;
	      NEXT_CHAR (name, len);
	      break;
	    }
	  /* fall through */
	default:
	  if (*expr != *name)
	    return WILD_FALSE;
	  expr++;
	  NEXT_CHAR (name, len);
	}
    }
  return END_OF_NAME (name, len) ? WILD_TRUE : WILD_FALSE;
}

/* Return 0 if first LEN bytes of NAME match globbing pattern EXPR. */
int
wildmatch (char const *expr, char const *name, size_t len)
{
  return wilder_match (expr, name, len) != WILD_TRUE;
}
