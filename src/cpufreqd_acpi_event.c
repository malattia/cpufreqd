/*
 *  Copyright (C) 2005-2006  Mattia Dongili<malattia@linux.it>
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
 *
 *  ACPI Event Plugin
 *  -----------------
 *  This plugin allows cpufreqd to monitor acpi events and process them.
 *
 *  It supports both direct /proc/acpi/event and acpid socket reading.
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "cpufreqd_plugin.h"
#include "cpufreqd_acpi.h"
#include "cpufreqd_acpi_event.h"

struct acpi_event {
	char *device_class;
	char *bus_id;
	unsigned int type;
	unsigned int data;
	struct acpi_event *next;
};

static int acpi_event_active;

static pthread_t event_thread;
static pthread_mutex_t event_mutex = PTHREAD_MUTEX_INITIALIZER;
static int event_fd;
static short event_pending;
extern struct acpi_configuration acpi_config;

static void close_acpi_event(void) {
	if (event_fd) {
		clog(LOG_DEBUG, "closing event handle.\n");
		if (close(event_fd))
			clog(LOG_ERR, "Couldn't close the ACPI event handler (%s).\n",
					strerror(errno));
	}
	event_fd = 0;
}

static int open_acpi_event (void) {
#if 0
	/* try to open /proc/acpi/event */
	event_fd = open("/proc/acpi/event", O_RDONLY);
#endif
	/* or fallback to the acpid socket */
	if (event_fd <= 0 && acpi_config.acpid_sock_path[0]) {
		struct sockaddr_un sck;
		sck.sun_family = AF_UNIX;
		strncpy(sck.sun_path, acpi_config.acpid_sock_path, 108);
		sck.sun_path[107] = '\0';

		if ((event_fd = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
			clog(LOG_ERR, "Couldn't open acpid socket (%s).\n",
					strerror(errno));
			event_fd = 0;
			return -1;
		}

		if (connect(event_fd, (struct sockaddr *)&sck, sizeof(sck)) == -1) {
			clog(LOG_NOTICE, "Couldn't connect to acpid socket %s (%s).\n",
					acpi_config.acpid_sock_path, strerror(errno));
			close_acpi_event();
			return -1;
		}
	} else if (event_fd <= 0) {
		clog(LOG_ERR, "Couldn't open ACPI event device (%s).\n",
				strerror(errno));
		event_fd = 0;
		return -1;
	} else if (!acpi_config.acpid_sock_path[0]) {
		clog(LOG_ERR, "No acpid socket given (%s).\n", acpi_config.acpid_sock_path);
		return -1;
	} else {
		clog(LOG_ERR, "Unknown error (%d)-(%s).\n", event_fd, acpi_config.acpid_sock_path);
		return -1;

	}

	if (fcntl(event_fd, F_SETFL, O_NONBLOCK) == -1) {
		clog(LOG_ERR, "Couldn't set O_NONBLOCK on ACPI event handler (%s).\n",
				strerror(errno));
		close(event_fd);
		return -1;
	}
	return 0;
}

/*  Waits for ACPI events on the file descriptor opened previously.
 *  This function uses poll(2) to wait for readable data in order
 *  to only wake cpufreqd once in case multiple events are available.
 */
static void *event_wait (void __UNUSED__ *arg) {
	struct pollfd rfd;
	char buf[MAX_STRING_LEN];
	int read_chars = 0, ret = 0;

	clog(LOG_DEBUG, "event thread running.\n");
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	rfd.events = POLLIN | POLLPRI;

	while (1) {
		while (!event_fd && open_acpi_event() != 0) {
			struct timespec req = { .tv_sec = 5, .tv_nsec = 0 };
			nanosleep(&req, NULL);
		}

		rfd.fd = event_fd;
		rfd.revents = 0;
		ret = poll(&rfd, 1, -1);

		if (ret < 0) {
			clog(LOG_ERR, "Error polling ACPI Event handler: %s\n",
					strerror(errno));
			if (errno == EINTR)
				close_acpi_event();
			continue;
		}

		/* barf and exit on any error condition */
		/* close the FD and try too reopen it every 5 seconds */
		if (rfd.revents & ~POLLIN) {
			clog(LOG_ERR, "Error polling ACPI Event handler (0x%.4x).\n",
					rfd.revents);
			close_acpi_event();
			continue;
		}
		while ((read_chars = read(event_fd, buf, MAX_STRING_LEN-1)) > 0) {
			buf[read_chars-1] = '\0';
			clog(LOG_DEBUG, "%s (%d)\n", buf, read_chars);
		}

		if (read_chars < 0 && errno != EAGAIN && errno != EINTR) {
			clog(LOG_DEBUG, "Error reading the ACPI event handler (%d)\n",
					strerror(errno), read_chars);
			close_acpi_event();
			continue;
		} else if (read_chars == 0) {
			clog(LOG_DEBUG, "ACPI event handler disappeared.\n");
			close_acpi_event();
			continue;
		}

		/* mark pending event */
		acpi_event_lock();
		event_pending = 1;
		acpi_event_unlock();
		/* Ring the bell!! */
		wake_cpufreqd();
	}

	/* set acpi_event as inactive */
	acpi_event_active = 0;

	return NULL;
}

int is_event_pending(void) {
	return event_pending && acpi_event_active;
}

void reset_event(void) {
	event_pending = 0;
}

int acpi_event_lock (void) {
	return pthread_mutex_lock(&event_mutex);
}

int acpi_event_unlock (void) {
	return pthread_mutex_unlock(&event_mutex);
}

/* Launch the thread that will wait for acpi events
 */
short int acpi_event_init (void) {
	int ret = 0;

	event_pending = 1;

	/* launch exec thread */
	if ((ret = pthread_create(&event_thread, NULL, &event_wait, NULL)) != 0) {
		clog(LOG_ERR, "Unable to launch thread: %s\n", strerror(ret));
		return -1;
	}
	acpi_event_active = 1;
	return 0;
}

short int acpi_event_exit (void) {
	int ret = 0;

	if (event_thread) {
		clog(LOG_DEBUG, "killing event thread.\n");
		ret = pthread_cancel(event_thread);
		if (ret != 0)
			clog(LOG_ERR, "Couldn't cancel event thread (%s).\n",
					strerror(ret));

		/* cleanup */
		ret = pthread_join(event_thread, NULL);
		if (ret != 0)
			clog(LOG_ERR, "Couldn't join event thread (%s).\n",
					strerror(ret));
		event_thread = 0;
	}

	/* just in case the event thread missed it */
	close_acpi_event();

	clog(LOG_INFO, "acpi_event exited.\n");
	return 0;
}

#if 0
static int acpi_event_update(void) {
	if (!event_fd && !event_thread) {
		acpi_event_exit();
		acpi_event_init();
	}
	return 0;
}
#endif

#if 0
static struct cpufreqd_keyword kw[] =  {
	{ .word = NULL, }
};

static struct cpufreqd_plugin plugin =  {
	.plugin_name = "acpi_event",
	.keywords = kw,
	.plugin_init = NULL,
	.plugin_exit = &acpi_event_exit,
	.plugin_update = NULL, /*&acpi_event_update,*/
	.plugin_conf = &acpi_event_conf,
	.plugin_post_conf = &acpi_event_init,
};

/*
 *  A cpufreqd plugin MUST define the following function to provide the
 *  core cpufreqd with the correct struct cpufreqd_plugin structure
 */
struct cpufreqd_plugin *create_plugin(void) {
	return &plugin;
}
#endif
