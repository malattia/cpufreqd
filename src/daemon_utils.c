/*
 *  Copyright (C) 2002-2005  Mattia Dongili <malattia@gmail.com>
 *                           George Staikos <staikos@0wned.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "daemon_utils.h"
#include "cpufreqd_log.h"
#include "cpufreqd.h"
#include "config_parser.h"

extern struct cpufreqd_conf configuration;

/* int write_cpufreqd_pid(void)
 *
 * Writes the pid file.
 *
 * Returns 0 on success, -1 otherwise.
 */
int write_cpufreqd_pid(void) {
  FILE *pid;
  struct stat sb;
  int rc = 0;
  
  /* check if old pidfile is still there */
  rc = stat(configuration.pidfile, &sb);
  if (rc == 0) {
    char oldpid[10];
    pid = fopen(configuration.pidfile, "r");
    /* see if there is a pid already */
    if (fscanf(pid, "%s", oldpid) == 1) {
      FILE *fd;
      char old_pidfile[256];
      char old_cmdline[256];

      snprintf(old_pidfile, 256, "/proc/%s/cmdline", oldpid);
      fd = fopen(old_pidfile, "r");
      /* if the file exists see if there's another cpufreqd process running */
      if (fd && fscanf(fd, "%s", old_cmdline) == 1 && strstr(old_cmdline,"cpufreqd") != NULL) {
        cpufreqd_log(LOG_ERR, "write_cpufreqd_pid(): the daemon is already running.\n");
        fclose(fd);
        fclose(pid);
        return -1;
      }
      fclose(fd);
    }
    fclose(pid);
  }

  /* set permission mask 033 */
  umask( S_IXGRP | S_IXOTH | S_IWOTH | S_IWGRP );

  /* write pidfile */
  pid = fopen(configuration.pidfile, "w");
  if (!pid) {
    cpufreqd_log(LOG_ERR, "write_cpufreqd_pid(): %s: %s.\n", configuration.pidfile, strerror(errno));
    return -1;
  }

  if (!fprintf(pid, "%d", getpid())) {
    cpufreqd_log(LOG_ERR, "write_cpufreqd_pid(): cannot write pid %d.\n", getpid());
    fclose(pid);
    clear_cpufreqd_pid();
    return -1;
  }

  fclose(pid);
  return 0;
}

/* int clear_cpufreqd_pid(void)
 *
 * Removes pid file
 *
 * Returns 0 on success, -1 otherwise.
 */
int clear_cpufreqd_pid(void) {
        
  if (unlink(configuration.pidfile) < 0) {
    cpufreqd_log(LOG_ERR, "clear_cpufreqd_pid(): error while removing %s: %s.\n", configuration.pidfile, strerror(errno));
    return -1;
  }

  return 0;
}

/*  int daemonize (void)
 *  Creates a child and detach from tty
 */
int daemonize (void) {

  cpufreqd_log(LOG_INFO, "daemonize(): going background, bye.\n");

  switch (fork ()) {
    case -1:
      cpufreqd_log(LOG_ERR, "deamonize(): fork: %s\n", strerror (errno));
      return -1;
    case 0:
      /* child */
      break;
    default:
      /* parent */
      exit (0);
  }

  /* disconnect */
  setsid ();
  umask (0);

  /* set up stdout to log and stderr,stdin to /dev/null */
  if (freopen("/dev/null", "r", stdin) == NULL) {
    cpufreqd_log(LOG_ERR, "deamonize(): %s: %s.\n", "/dev/null", strerror(errno));
    return -1;
  }
  
  if (freopen("/dev/null", "w", stdout) == NULL) {
    cpufreqd_log(LOG_ERR, "deamonize(): %s: %s.\n", "/dev/null", strerror(errno));
    return -1;
  }
  
  if (freopen("/dev/null", "w", stderr) == NULL) {
    cpufreqd_log(LOG_ERR, "deamonize(): %s: %s.\n", "/dev/null", strerror(errno));
    return -1;
  }

  /* get outta the way */
  chdir ("/");
  return 0;
}

