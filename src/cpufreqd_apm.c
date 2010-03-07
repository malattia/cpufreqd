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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cpufreqd_plugin.h"

#define APM_PROC_FILE "/proc/apm"
#define PLUGGED   1
#define UNPLUGGED 0

struct battery_interval {
	int min, max;
};

static int battery_present;
static int battery_percent;
static unsigned int ac_state;

/*  static int apm_init(void)
 *
 *  test if apm file id present
 */
static int apm_init(void) {
	struct stat sb;
	int rc;

	rc = stat(APM_PROC_FILE, &sb);
	if (rc < 0) {
		clog(LOG_INFO, "%s: %s\n", APM_PROC_FILE, strerror(errno));
		return -1;
	}
	return 0;
}

static int apm_exit(void) {
	return 0;
}

/*  static int apm_ac_update(void)
 *
 *  reads temperature valuse ant compute a medium value
 */
static int apm_update(void) {
	FILE *fp;
	char buf[101];

	/***** APM SCAN *****/
	int ignore;
	unsigned int ignore2;
	char ignore3[101];
	unsigned int batt_flag;

	clog(LOG_DEBUG, "called\n");

	fp = fopen(APM_PROC_FILE , "r");
	if (!fp) {
		clog(LOG_ERR, "%s: %s\n", APM_PROC_FILE, strerror(errno));
		return -1;
	}

	if (!fgets(buf, 100, fp)) {
		fclose(fp);
		clog(LOG_ERR, "%s: %s\n", APM_PROC_FILE, strerror(errno));
		return -1;
	}

	sscanf(buf, "%s %d.%d %x %x %x %x %d%% %d %s\n",
			ignore3, &ignore, &ignore,
			&ignore2, &ac_state, &ignore2, &batt_flag,
			&battery_percent, &ignore, ignore3);

	if (battery_percent > 100) {
		battery_percent = -1;
	}

	battery_present = batt_flag < 128;

	fclose(fp);

	clog(LOG_INFO, "battery %s - %d - ac: %s\n",
			battery_present?"present":"absent",
			battery_percent,
			ac_state?"on-line":"off-line");
	return 0;
}

/*
 *  parse the 'ac' keywork
 */
static int apm_ac_parse(const char *ev, void **obj) {
	unsigned int *ret = malloc(sizeof(int));
	if (ret == NULL) {
		clog(LOG_ERR, "couldn't make enough room for ac_status (%s)\n",
				strerror(errno));
		return -1;
	}

	*ret = 0;

	clog(LOG_DEBUG, "called with %s\n", ev);

	if (strncmp(ev, "on", 2) == 0) {
		*ret = PLUGGED;
	} else if (strncmp(ev, "off", 3) == 0) {
		*ret = UNPLUGGED;
	} else {
		clog(LOG_ERR, "couldn't parse %s\n", ev);
		free(ret);
		return -1;
	}

	clog(LOG_INFO, "parsed %s\n", *ret==PLUGGED ? "on" : "off");

	*obj = ret;
	return 0;
}

/*
 *  evaluate the 'ac' keywork
 */
static int apm_ac_evaluate(const void *s) {
	const unsigned int *ac = (const unsigned int *)s;

	clog(LOG_DEBUG, "called %s [%s]\n",
			*ac==PLUGGED ? "on" : "off", ac_state==PLUGGED ? "on" : "off");

	return (*ac == ac_state) ? MATCH : DONT_MATCH;
}

/*
 *  parse the 'battery' keywork
 */
static int apm_bat_parse(const char *ev, void **obj) {
	struct battery_interval *ret = malloc(sizeof(struct battery_interval));
	if (ret == NULL) {
		clog(LOG_ERR, "couldn't make enough room for battery_interval (%s)\n",
				strerror(errno));
		return -1;
	}

	ret->min = ret->max = 0;

	clog(LOG_DEBUG, "called with %s\n", ev);

	if (sscanf(ev, "%d-%d", &(ret->min), &(ret->max)) != 2) {
		clog(LOG_ERR, "wrong parameter %s\n", ev);
		free(ret);
		return -1;
	}

	clog(LOG_INFO, "parsed %d-%d\n", ret->min, ret->max);

	*obj = ret;
	return 0;
}

/*
 *  evaluate the 'battery' keywork
 */
static int apm_bat_evaluate(const void *s) {
	const struct battery_interval *bi = (const struct battery_interval *)s;

	clog(LOG_DEBUG, "called %d-%d [%d]\n", bi->min, bi->max, battery_percent);

	return (battery_percent>=bi->min && battery_percent<=bi->max) ? MATCH : DONT_MATCH;
}

static struct cpufreqd_keyword kw[] = {
	{ .word = "ac",       .parse = &apm_ac_parse,  .evaluate = &apm_ac_evaluate  },
	{ .word = "battery_interval",  .parse = &apm_bat_parse, .evaluate = &apm_bat_evaluate },
	{ .word = NULL,       .parse = NULL,           .evaluate = NULL              }
};

static struct cpufreqd_plugin apm = {
	.plugin_name      = "apm_plugin",	/* plugin_name */
	.keywords         = kw,		/* config_keywords */
	.plugin_init      = &apm_init,	/* plugin_init */
	.plugin_exit      = &apm_exit,	/* plugin_exit */
	.plugin_update    = &apm_update	/* plugin_update */
};

struct cpufreqd_plugin *create_plugin (void) {
	return &apm;
}
