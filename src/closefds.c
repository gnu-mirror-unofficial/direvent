/* This file is part of GNU Pies
   Copyright (C) 2015-2021 Sergey Poznyakoff
  
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

#include <config.h>
#include <stdlib.h>
#include <unistd.h>
#include "direvent.h"

#if defined (HAVE_FUNC_CLOSEFROM)
# include <unistd.h>

static int
close_fds_sys (int minfd)
{
  closefrom (minfd);
  return 0;
}

#elif defined (HAVE_FCNTL_CLOSEM)
# include <fcntl.h>

static int
close_fds_sys (int minfd)
{
  fcntl (minfd, F_CLOSEM, 0);
  return 0;
}

#elif defined (HAVE_LIBPROC_H) && defined (HAVE_FUNC_PROC_PIDINFO)
#include <libproc.h>

static int
close_fds_sys (int minfd)
{
  pid_t pid = getpid ();
  struct proc_fdinfo *fdinfo;
  int i, n, size;

  size = proc_pidinfo (pid, PROC_PIDLISTFDS, 0, NULL, 0);
  if (size == 0)
    return 0;
  else if (size < 0)
    return -1;

  fdinfo = calloc (size, sizeof (fdinfo[0]));
  if (!fdinfo)
    return -1;

  n = proc_pidinfo (pid, PROC_PIDLISTFDS, 0, fdinfo, size);
  if (n <= 0)
    {
      free (fdinfo);
      return -1;
    }

  n /= PROC_PIDLISTFD_SIZE;
  
  for (i = 0; i < n; i++)
    {
      if (fdinfo_buf[i].proc_fd >= minfd)
	close (fdinfo_buf[i].proc_fd);
    }

  free (fdinfo);
  return 0;
}

#elif defined (HAVE_PROC_SELF_FD)
# include <sys/types.h>
# include <dirent.h>
# include <limits.h>

static int
close_fds_sys (int minfd)
{
  DIR *dir;
  struct dirent *ent;
  
  dir = opendir ("/proc/self/fd");
  if (!dir)
    return -1;
  while ((ent = readdir (dir)) != NULL)
    {
      long n;
      char *p;
      
      if (ent->d_name[0] == '.')
	continue;

      n = strtol (ent->d_name, &p, 10);
      if (n >= minfd && n < INT_MAX && *p == 0)
	close ((int) n);
    }
  closedir (dir);
  return 0;
}

#else
# define close_fds_sys(fd) (-1)
#endif

#if defined HAVE_SYSCONF && defined _SC_OPEN_MAX
# define getmaxfd() sysconf(_SC_OPEN_MAX)
#elif defined (HAVE_GETDTABLESIZE)
# define getmaxfd() getdtablesize()
#else
# define getmaxfd() 256
#endif

static int
close_fds_bruteforce (int minfd)
{
  int i, n = getmaxfd ();

  for (i = minfd; i < n; i++)
    close (i);

  return 0;
}

void
close_fds (int minfd)
{
  if (close_fds_sys (minfd))
    close_fds_bruteforce (minfd);
}
