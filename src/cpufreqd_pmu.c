/*
 *  Copyright (C) 2003,2004  Rene Rebe <rene@rocklinux.org>
 *                2005       Mattia Dongili <malattia@linux.it>
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
 *  Based upon the acpi version:
 *    Copyright (C) 2002-2005  Mattia Dongili<malattia@linux.it>
 *                             George Staikos <staikos@0wned.org>
 *
 *  2005-09-11
 *  Ported to cpufreqd-2 new plugin interface by Mattia Dongili
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpufreqd_plugin.h"

#ifndef DEBUG_NO_PMU
#  define PMU_INFO_FILE		"/proc/pmu/info"
#  define PMU_BATTERY_FILE	"/proc/pmu/battery_0"
#else
#  define PMU_INFO_FILE		"/home/mattia/devel/cpufreqd/pmu/info"
#  define PMU_BATTERY_FILE	"/home/mattia/devel/cpufreqd/pmu/battery_0"
#endif
#define PLUGGED   1
#define UNPLUGGED 0

struct battery_interval {
	int min, max;
};

static char tag[255];
static char val[255];
static char version[100];
static unsigned int battery_present;
static int battery_percent;
static unsigned int ac;

static int tokenize (FILE *fp, char *t, char *v) {
	char str[255];
	char *s1, *s2;

	t[0] = v[0] = '\0';

	if (fgets(str, 255, fp) == NULL)
		return EOF;

	if ((s1 = strtok(str, ":")) == NULL)
		return 0;

	s2 = s1 + strlen (s1) - 1;
	/* remove trailing spaces */
	for ( ; s1 != s2 ; --s2) {
		if (!isspace(*s2)) {
			break;
		} else {
			*s2 = '\0';
		}
	}

	strncpy (t, s1, 255);
	t[254] = '\0';

	if ((s1 = strtok(NULL, ":")) == NULL)
		return 0;

	for ( ; *s1 != 0 ; ++s1) {
		if (!isspace(*s1)) {
			break;
		}
	}
	s2 = s1 + strlen (s1) - 1;
	/* remove trailing spaces */
	for ( ; s1 != s2 ; --s2) {
		if (!isspace(*s2)) {
			break;
		} else {
			*s2 = '\0';
		}
	}

	strncpy (v, s1, 255);
	v[254] = '\0';

	return 1;
}

/* check if PMU is available
 */
static int pmu_init(void) {

	FILE *fp;

	fp = fopen(PMU_INFO_FILE, "r");
	if (!fp) {
		clog(LOG_INFO, "%s: %s\n", PMU_INFO_FILE, strerror(errno));
		return -1;
	}

	while (tokenize(fp, tag, val) != EOF) {
		if (strcmp(tag, "PMU driver version") == 0) {
			sprintf(version, "%s - ", val);
		}
		else if (strcmp(tag, "PMU firmware version") == 0) {
			strncat(version, val, 100-strlen(version));
		}
	}
	fclose(fp);

	clog(LOG_NOTICE, "PMU driver/firmware version %s\n", version);

	return 0;
}

static int pmu_update(void) {

	FILE *fp;

	float bat_charge = .0;
	float bat_max_charge = .0;

	/** /proc/pmu/info **/
	fp = fopen(PMU_INFO_FILE, "r");
	if (!fp) {
		clog(LOG_ERR, "%s: %s\n", PMU_INFO_FILE, strerror(errno));
		return -1;
	}

	while (tokenize(fp, tag, val) != EOF) {
		if (strcmp(tag, "AC Power") == 0) {
			ac = atoi(val);
		}
		else if (strcmp(tag, "Battery count") == 0) {
			battery_present = atoi(val);
		}
	}
	fclose(fp);

	/** /proc/pmu/battery_0 **/
	fp = fopen(PMU_BATTERY_FILE, "r");
	if (!fp) {
		clog(LOG_ERR, "%s: %s\n", PMU_BATTERY_FILE, strerror(errno));
		return -1;
	}

	while (tokenize(fp, tag, val) != EOF) {
		if (strcmp(tag, "charge") == 0) {
			bat_charge = atof(val);
		}
		else if (strcmp(tag, "max_charge") == 0) {
			bat_max_charge = atof(val);
		}
	}
	fclose(fp);

	battery_percent = 100 * (bat_charge / bat_max_charge);

	clog(LOG_INFO, "battery %s - %d - %s\n",
			battery_present ? "present" : "absent",
			battery_percent,
			ac ? "on-line" : "off-line");
	return 0;
}

/*
 *  parse the 'ac' keywork
 */
static int pmu_ac_parse(const char *ev, void **obj) {
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
static int pmu_ac_evaluate(const void *s) {
	const unsigned int *ac_parm = (const unsigned int *)s;

	clog(LOG_DEBUG, "called %s [%s]\n",
			*ac_parm==PLUGGED ? "on" : "off", ac==PLUGGED ? "on" : "off");

	return (*ac_parm == ac) ? MATCH : DONT_MATCH;
}

/*
 *  parse the 'battery' keywork
 */
static int pmu_bat_parse(const char *ev, void **obj) {
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
static int pmu_bat_evaluate(const void *s) {
	const struct battery_interval *bi = (const struct battery_interval *)s;

	clog(LOG_DEBUG, "called %d-%d [%d]\n", bi->min, bi->max, battery_percent);

	return (battery_percent>=bi->min && battery_percent<=bi->max) ? MATCH : DONT_MATCH;
}

static struct cpufreqd_keyword kw[] = {
	{ .word = "ac",       .parse = &pmu_ac_parse,  .evaluate = &pmu_ac_evaluate  },
	{ .word = "battery_interval",  .parse = &pmu_bat_parse, .evaluate = &pmu_bat_evaluate },
	{ .word = NULL,       .parse = NULL,           .evaluate = NULL              }
};

static struct cpufreqd_plugin pmu = {
	.plugin_name      = "pmu_plugin",	/* plugin_name */
	.keywords         = kw,			/* config_keywords */
	.plugin_init      = &pmu_init,		/* plugin_init */
	.plugin_update    = &pmu_update		/* plugin_update */
};

struct cpufreqd_plugin *create_plugin (void) {
	return &pmu;
}
