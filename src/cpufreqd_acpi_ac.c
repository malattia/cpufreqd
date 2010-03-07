/*
 *  Copyright (C) 2002-2008  Mattia Dongili <malattia@linux.it>
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
#include "cpufreqd_acpi.h"
#include "cpufreqd_acpi_ac.h"

#define POWER_SUPPLY "power_supply"
#define AC_TYPE "Mains"
#define AC_ONLINE "online"

#define PLUGGED   1
#define UNPLUGGED 0

static struct sysfs_attribute *mains[64];
static int ac_state;
static int ac_dir_num;

static int mains_callback(struct sysfs_class_device *cdev) {
	struct sysfs_attribute *attr = get_class_device_attribute(cdev,
			AC_ONLINE);
	if (attr) {
		/* success */
		mains[ac_dir_num] = attr;
		ac_dir_num++;
	}
	/* we don't care about the class_device
	 * returning 1 will force find_class_device
	 * to close it
	 */
	return 1;
}

/*  static int acpi_ac_init(void)
 *
 *  test if AC dirs are present
 */
short int acpi_ac_init(void) {

	find_class_device(POWER_SUPPLY, AC_TYPE, mains_callback);
	if (ac_dir_num <= 0) {
		clog(LOG_INFO, "No AC adapters found\n");
		return -1;
	}
	return 0;
}

short int acpi_ac_exit(void) {
	while (--ac_dir_num >= 0)
		put_attribute(mains[ac_dir_num]);
	clog(LOG_INFO, "exited.\n");
	return 0;
}

/*  static int acpi_ac_update(void)
 *
 *  reads temperature valuse ant compute a medium value
 */
int acpi_ac_update(void) {
	int value;
	int i = 0;

	ac_state = UNPLUGGED;
	clog(LOG_DEBUG, "called\n");
	for (i = 0; i < ac_dir_num; i++) {
		/* check power_supply type */
		if (read_int(mains[i], &value))
				continue;

		clog(LOG_DEBUG, "read %s:%d\n", mains[i]->path, value);
		ac_state |= value ? PLUGGED : UNPLUGGED;
	}

	clog(LOG_INFO, "ac_adapter is %s\n",
			ac_state==PLUGGED ? "on-line" : "off-line");

	return 0;
}

/*
 *  parse the 'ac' keywork
 */
int acpi_ac_parse(const char *ev, void **obj) {
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
int acpi_ac_evaluate(const void *s) {
	const int *ac = (const int *)s;

	clog(LOG_DEBUG, "called: %s [%s]\n",
			*ac==PLUGGED ? "on" : "off", ac_state==PLUGGED ? "on" : "off");

	return (*ac == ac_state) ? MATCH : DONT_MATCH;
}

#if 0
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
#endif
