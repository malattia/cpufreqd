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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "cpufreqd_log.h"
#include "cpufreqd.h"
#include "sock_utils.h"

/* create a temporary directory for cpufreqd socket.
 * It expects a buffer at least TMP_FILE_TEMPL_LEN long.
 * Also, if the gid parameter is > 0 it changes
 * permissions on the directory in order to let the provided
 * group read/execute.
 */
char *create_temp_dir(char *buf, gid_t gid) {
	mode_t oldmode = 0;
	strncpy(buf, TMP_DIR_TEMPL, TMP_DIR_TEMPL_LEN);

	oldmode = umask(S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
	if (mkdtemp(buf) == NULL) {
		clog(LOG_ERR, "Couldn't create temporary dir: %s.\n", strerror(errno));
		umask(oldmode);
		return NULL;

	} else if (gid > 0 && chmod(buf, S_IRUSR | S_IWUSR | S_IXUSR |
				S_IRGRP | S_IXGRP) < 0) {
		clog(LOG_ERR, "Couldn't chmod %s (%s).\n", buf, strerror(errno));

	} else if (gid > 0 && chown(buf, 0, gid)) {
		clog(LOG_ERR, "Couldn't chown %s (%s).\n", buf, strerror(errno));
	}
	umask(oldmode);
	clog(LOG_INFO, "Created temporary dir: '%s'.\n", buf);
	return buf;
}

/* removes the temporary directory named name
 * returns 1 on success, 0 otherwise
 */
void delete_temp_dir(const char *dirname) {
	char filename[MAX_PATH_LEN];
	struct stat buf;

	if (!dirname[0])
		return;

	snprintf(filename, MAX_PATH_LEN, "%s%s", dirname, CPUFREQD_SOCKET);

	/* check file and delete it */
	if (stat(filename, &buf) != 0) {
		clog(LOG_ERR, "Couldn't stat %s: %s\n", filename, strerror(errno));
	} else if (!S_ISSOCK(buf.st_mode)) {
		clog(LOG_ERR, "%s not a socket!\n", filename);
	} else if (unlink(filename) != 0) {
		clog(LOG_ERR, "Couldn't delete %s: %s\n", filename, strerror(errno));
	} else {
		clog(LOG_INFO, "%s deleted.\n", filename);
	}

	/* it's safe to try this anyway */
	if (rmdir(dirname) != 0) {
		clog(LOG_ERR, "Couldn't delete %s: %s\n", dirname, strerror(errno));
	} else {
		clog(LOG_INFO, "%s deleted.\n", dirname);
	}
}

/* opens a PF_UNIX socket and returns the file
 * descriptor (>0) on success, -1 otherwise.
 * Also, if the gid parameter is > 0 it changes
 * permissions on the file in order to let the privided
 * group read/write to the socket
 */
int open_unix_sock(const char *dirname, gid_t gid) {
	mode_t oldmode = 0;
	int fd = -1;
	struct sockaddr_un sa;

	sa.sun_family = AF_UNIX;
	snprintf(sa.sun_path, 108 , "%s%s", dirname, CPUFREQD_SOCKET);

	if (gid > 0) {
		oldmode = umask(S_IXUSR | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
	}
	if ((fd = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
		clog(LOG_ERR, "socket(): %s.\n", strerror(errno));

	} else if (bind(fd, &sa, sizeof(sa)) == -1) {
		clog(LOG_ERR, "bind(): %s.\n", strerror(errno));
		close(fd);
		fd = -1;

	} else if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
		clog(LOG_ERR, "fcntl(): %s.\n", strerror(errno));
		close(fd);
		fd = -1;

	} else if (listen(fd, 5) == -1) {
		clog(LOG_ERR, "listen(): %s.\n", strerror(errno));
		close(fd);
		fd = -1;

	} else if (gid > 0 && chown(sa.sun_path, 0, gid) < 0) {
		clog(LOG_ERR, "Couldn't chown %s (%s).\n",
				sa.sun_path, strerror(errno));
	}
	if (gid > 0) {
		umask(oldmode);
	}
	return fd;
}

/* removes a socket file after having closed the
 * file descrptor
 */
void close_unix_sock(int fd) {
	if (fd > 0) {
		close(fd);
	}
}

