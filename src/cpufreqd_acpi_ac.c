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

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpufreqd_plugin.h"

#define ACPI_AC_DIR "/proc/acpi/ac_adapter/"
#define ACPI_AC_FILE "/state"
#define ACPI_AC_FORMAT "state:                   %s\n"

#define PLUGGED   1
#define UNPLUGGED 0

static char *ac_filelist[64];
static unsigned short ac_state;
static int ac_dir_num;

static int no_dots(const struct dirent *d) {
	return d->d_name[0]!= '.';
}

/*  static int acpi_ac_init(void)
 *
 *  test if AC dirs are present
 */
static int acpi_ac_init(void) {
	struct dirent **namelist = NULL;
	int n = 0;

	/* get AC path */
	n = scandir(ACPI_AC_DIR, &namelist, no_dots, NULL);
	if (n > 0) {
		ac_dir_num = n;
		*ac_filelist = malloc(n * 64 * sizeof(char));
		while (n--) {
			snprintf(ac_filelist[n], 64, "%s%s%s", ACPI_AC_DIR, namelist[n]->d_name, ACPI_AC_FILE);
			clog(LOG_INFO, "AC path %s\n", ac_filelist[n]);
			free(namelist[n]);
		} 
		free(namelist);

	} else if (n < 0) {
		clog(LOG_DEBUG, "no acpi_ac module compiled or inserted? (%s: %s)\n",
				ACPI_AC_DIR, strerror(errno));
		return -1;

	} else {
		clog(LOG_NOTICE, "no ac adapters found, not a laptop?\n");
		return -1;
	}
	return 0;
}

static int acpi_ac_exit(void) {
	if (ac_filelist != NULL)
		free(*ac_filelist);
	clog(LOG_INFO, "exited.\n");
	return 0;
}

/*  static int acpi_ac_update(void)
 *  
 *  reads temperature valuse ant compute a medium value
 */
static int acpi_ac_update(void) {
	char temp[50];
	int i=0;
	FILE *fp = NULL;

	ac_state = UNPLUGGED;
	clog(LOG_DEBUG, "called\n");
	for (i=0; i<ac_dir_num; i++) {
		fp = fopen(ac_filelist[i], "r");
		if (!fp) {
			clog(LOG_ERR, "%s: %s\n", ac_filelist[i], strerror(errno));
			return -1;
		}
		fscanf(fp, ACPI_AC_FORMAT, temp);
		fclose(fp);

		clog(LOG_DEBUG, "read %s\n", temp);
		ac_state |= (strncmp(temp, "on-line", 7)==0 ? PLUGGED : UNPLUGGED);
	}

	clog(LOG_INFO, "ac_adapter is %s\n",
			ac_state==PLUGGED ? "on-line" : "off-line");
	return 0;
}

/*
 *  parse the 'ac' keywork
 */
static int acpi_ac_parse(const char *ev, void **obj) {
	int *ret = malloc(sizeof(int));
	if (ret == NULL) {
		clog(LOG_ERR, "couldn't make enough room for ac_status (%s)\n",
				strerror(errno));
		return -1;
	}

	*ret = 0;

	clog(LOG_DEBUG, "called with: %s\n", ev);

	if (strncmp(ev, "on", 2) == 0) {
		*ret = PLUGGED;
	} else if (strncmp(ev, "off", 3) == 0) {
		*ret = UNPLUGGED;
	} else {
		clog(LOG_ERR, "couldn't parse %s\n", ev);
		free(ret);
		return -1;
	}

	clog(LOG_INFO, "parsed: %s\n", *ret==PLUGGED ? "on" : "off");

	*obj = ret;
	return 0;
}

/*
 *  evaluate the 'ac' keywork
 */
static int acpi_ac_evaluate(const void *s) {
	const int *ac = (const int *)s;

	clog(LOG_DEBUG, "called: %s [%s]\n",
			*ac==PLUGGED ? "on" : "off", ac_state==PLUGGED ? "on" : "off");

	return (*ac == ac_state) ? MATCH : DONT_MATCH;
}

static struct cpufreqd_keyword kw[] = {
	{ .word = "ac", .parse = &acpi_ac_parse, .evaluate = &acpi_ac_evaluate },
	{ .word = NULL, .parse = NULL, .evaluate = NULL, .free = NULL }
};

static struct cpufreqd_plugin acpi_ac = {
	.plugin_name	= "acpi_ac_plugin",	/* plugin_name */
	.keywords	= kw,			/* config_keywords */
	.plugin_init	= &acpi_ac_init,	/* plugin_init */
	.plugin_exit	= &acpi_ac_exit,	/* plugin_exit */
	.plugin_update	= &acpi_ac_update	/* plugin_update */
};

struct cpufreqd_plugin *create_plugin (void) {
	return &acpi_ac;
}
