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

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysfs/libsysfs.h>
#include "cpufreqd_plugin.h"
#include "cpufreqd_acpi_ac.h"

#define POWER_SUPPLY "power_supply"
#define AC_TYPE "Mains"
#define AC_ONLINE "online"

#define PLUGGED   1
#define UNPLUGGED 0

static struct sysfs_attribute *mains[64];
static unsigned short ac_state;
static int ac_dir_num;

/*  static int acpi_ac_init(void)
 *
 *  test if AC dirs are present
 */
int acpi_ac_init(void) {
	struct sysfs_attribute *attr = NULL;
	struct dlist *devs = NULL;
	struct sysfs_class *cls = NULL;
	struct sysfs_class_device *power_supply = NULL;
	char type[256];
	char path[SYSFS_PATH_MAX];

	cls = sysfs_open_class(POWER_SUPPLY);
	if (!cls) {
		clog(LOG_NOTICE, "class '%s' not found\n", POWER_SUPPLY);
		return -1;
	}

	/* read POWER_SUPPLY devices (we only want Mains here) */
	devs = sysfs_get_class_devices(cls);
	if (!cls) {
		clog(LOG_NOTICE, "class '%s' not found\n", POWER_SUPPLY);
		goto exit;
	}
	dlist_for_each_data(devs, power_supply, struct sysfs_class_device) {
		clog(LOG_NOTICE, "found %s\n", power_supply->path);

		attr = sysfs_get_classdev_attr(power_supply, "type");
		if (!attr) {
			clog(LOG_NOTICE, "attribute 'type' not found for %s.\n",
					power_supply->name);
			continue;
		}
		/* check power_supply type */
		if (sysfs_read_attribute(attr)) {
			clog(LOG_NOTICE, "couldn't read %s\n", attr->path);
		}
		sscanf(attr->value, "%255s\n", type);
		clog(LOG_NOTICE, "%s is of type %s\n", power_supply->name, type);
		if (strncmp(type, AC_TYPE, 256) == 0) {
			snprintf(path, SYSFS_PATH_MAX, "%s/%s",
					power_supply->path, AC_ONLINE);
			mains[ac_dir_num] = sysfs_open_attribute(path);
			if (!mains[ac_dir_num]) {
				clog(LOG_WARNING, "couldn't open %s\n", path);
			}
			else {
				clog(LOG_INFO, "found %s AC path %s\n",
						AC_TYPE, path);
				ac_dir_num++;
			}
		}
	}
	sysfs_close_class(cls);
	return 0;
exit:
	/* cleanup */
	sysfs_close_class(cls);
	return -1;
}

int acpi_ac_exit(void) {
	while (ac_dir_num--) {
		clog(LOG_DEBUG, "closing %s.\n", mains[ac_dir_num]->path);
		sysfs_close_attribute(mains[ac_dir_num]);
	}
	clog(LOG_INFO, "exited.\n");
	return 0;
}

/*  static int acpi_ac_update(void)
 *  
 *  reads temperature valuse ant compute a medium value
 */
int acpi_ac_update(void) {
	char temp[256];
	int i = 0;

	ac_state = UNPLUGGED;
	clog(LOG_DEBUG, "called\n");
	for (i = 0; i < ac_dir_num; i++) {
		/* check power_supply type */
		if (sysfs_read_attribute(mains[i])) {
			clog(LOG_NOTICE, "couldn't read %s\n", mains[i]->path);
			continue;
		}
		sscanf(mains[i]->value, "%255s\n", temp);
		clog(LOG_DEBUG, "read %s:%s\n", mains[i]->path, temp);
		ac_state |= (strncmp(temp, "1", 7) == 0 ? PLUGGED : UNPLUGGED);
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
