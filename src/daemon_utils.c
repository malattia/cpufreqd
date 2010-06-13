/*
 *  Copyright (C) 2002-2005  Mattia Dongili <malattia@linux.it>
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

/* int write_cpufreqd_pid(const char *)
 *
 * Writes the pid file.
 *
 * Returns 0 on success, -1 otherwise.
 */
int write_cpufreqd_pid(const char *pidfile)
{
	FILE *pid;
	struct stat sb;
	mode_t oldmask;
	int rc = 0;

	/* check if old pidfile is still there */
	rc = stat(pidfile, &sb);
	if (rc == 0) {
		char oldpid[10];
		pid = fopen(pidfile, "r");
		/* see if there is a pid already */
		if (fscanf(pid, "%s", oldpid) == 1) {
			FILE *fd;
			char old_pidfile[256];
			char old_cmdline[256];

			snprintf(old_pidfile, 256, "/proc/%s/cmdline", oldpid);
			fd = fopen(old_pidfile, "r");
			/* if the file exists see if there's another cpufreqd process running */
			if (fd) {
				if (fscanf(fd, "%s", old_cmdline) == 1
						&& strstr(old_cmdline,"cpufreqd") != NULL) {
					clog(LOG_ERR, "the daemon is already running.\n");
					fclose(fd);
					fclose(pid);
					return -1;
				}
				fclose(fd);
			}
		}
		fclose(pid);
	}

	/* set permission mask 033 */
	oldmask = umask(S_IXGRP | S_IXOTH | S_IWOTH | S_IWGRP);
	/* write pidfile */
	pid = fopen(pidfile, "w");
	if (!pid) {
		clog(LOG_ERR, "%s: %s.\n", pidfile, strerror(errno));
		return -1;
	}
	if (!fprintf(pid, "%d", getpid())) {
		clog(LOG_ERR, "cannot write pid %d.\n", getpid());
		fclose(pid);
		clear_cpufreqd_pid(pidfile);
		return -1;
	}
	fclose(pid);
	umask(oldmask);
	return 0;
}

/* int clear_cpufreqd_pid(const char *)
 *
 * Removes pid file
 *
 * Returns 0 on success, -1 otherwise.
 */
int clear_cpufreqd_pid(const char *pidfile) {

	if (unlink(pidfile) < 0) {
		clog(LOG_ERR, "error while removing %s: %s.\n", pidfile, strerror(errno));
		return -1;
	}

	return 0;
}

/*  int daemonize (void)
 *  Creates a child and detach from tty
 */
int daemonize (void) {

	clog(LOG_INFO, "going background, bye.\n");

	switch (fork ()) {
		case -1:
			clog(LOG_ERR, "fork: %s\n", strerror (errno));
			return -1;
		case 0:
			/* child */
			break;
		default:
			/* parent */
			exit(0);
	}

	/* disconnect */
	setsid();
	/* set a decent umask: 177 */
	umask(S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);

	/* set up stdout to log and stderr,stdin to /dev/null */
	if (freopen("/dev/null", "r", stdin) == NULL) {
		clog(LOG_ERR, "/dev/null: %s.\n", strerror(errno));
		return -1;
	}

	if (freopen("/dev/null", "w", stdout) == NULL) {
		clog(LOG_ERR, "/dev/null: %s.\n", strerror(errno));
		return -1;
	}

	if (freopen("/dev/null", "w", stderr) == NULL) {
		clog(LOG_ERR, "/dev/null: %s.\n", strerror(errno));
		return -1;
	}

	/* get outta the way */
	chdir("/");
	return 0;
}
