/*
 *  Copyright (C) 2002-2006  Mattia Dongili <malattia@linux.it>
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

#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

#include "config_parser.h"
#include "cpufreqd_log.h"

extern struct cpufreqd_conf *configuration; /* defined in cpufreqd.h and declared in main.c */

static unsigned int log_opened; /* syslog already opened */

/*
 * Logger infrastructure. It reuses the same priorities as
 * sys/syslog.h because it's easier to manage.
 *
 *	LOG_EMERG	0
 *	LOG_ALERT	1
 *	LOG_CRIT	2
 *	LOG_ERR		3
 *	LOG_WARNING	4
 *	LOG_NOTICE	5
 *	LOG_INFO	6
 *	LOG_DEBUG	7
 *
 */
void cpufreqd_log(int prio, const char *fmt, ...) {
	va_list argp;
	va_list argp2;

	/* do we need to write? */
	if (configuration->log_level < prio)
		return;

	va_start(argp, fmt);
	va_copy(argp2, argp);

	if (configuration->no_daemon) {
		if (configuration->log_level <= LOG_ERR) {
			vfprintf(stderr, fmt, argp);
			/* fflush(stderr); */
		} else {
			vfprintf(stdout, fmt, argp);
			/* fflush(stdout); */
		}
	} else {
		if (!log_opened) {
			/* open syslog */
			openlog("cpufreqd", LOG_CONS, LOG_DAEMON);
			log_opened = 1;
		}
		vsyslog(prio, fmt, argp);
		if (configuration->log_level <= LOG_ERR) {
			vfprintf(stderr, fmt, argp2);
			/* fflush(stderr); */
		}
	}
	va_end(argp);
	va_end(argp2);
}
