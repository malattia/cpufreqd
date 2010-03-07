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
#include "cpufreqd_acpi_temperature.h"

#define THERMAL			"thermal"
#define THERMAL_TYPE		"acpitz"
/* the below is for kernels <= 2.6.25 */
#define THERMAL_TYPE_ALT	"ACPI thermal zone"
#define THERMAL_TEMP		"temp"

struct thermal_zone {
	int temperature;
	struct sysfs_class_device *cdev;
	struct sysfs_attribute *temp;
};

struct temperature_interval {
	int min, max;
	struct thermal_zone *tz;
};

static struct thermal_zone atz_list[64];
static int atz_dir_num;
static long int temp_avg;

static struct thermal_zone *get_thermal_zone(const char *name)
{
	int i;
	struct thermal_zone *ret = NULL;

	for (i = 0; i < atz_dir_num; i++) {
		if (strncmp(atz_list[i].cdev->name, name, 32) == 0) {
			ret = &atz_list[i];
			break;
		}
	}
	return ret;
}

static int atz_callback(struct sysfs_class_device *cdev)
{
	atz_list[atz_dir_num].cdev = cdev;
	atz_list[atz_dir_num].temp = get_class_device_attribute(cdev,
			THERMAL_TEMP);
	if (!atz_list[atz_dir_num].temp)
		return 1;

	clog(LOG_DEBUG, "Found %s\n", cdev->name);
	atz_dir_num++;
	return 0;
}

/*  static int acpi_temperature_init(void)
 *
 *  test if ATZ dirs are present and read their
 *  path for usage when parsing rules
 */
short int acpi_temperature_init(void)
{
	find_class_device(THERMAL, THERMAL_TYPE, atz_callback);
	/* try with the old type name */
	if (atz_dir_num <= 0)
		find_class_device(THERMAL, THERMAL_TYPE_ALT, atz_callback);
	if (atz_dir_num <= 0) {
		clog(LOG_INFO, "No thermal zones found\n");
		return -1;
	}
	clog(LOG_NOTICE, "found %d ACPI Thermal Zone%s\n", atz_dir_num,
			atz_dir_num > 1 ? "s" : "");
	return 0;
}

short int acpi_temperature_exit(void)
{
	while (--atz_dir_num >= 0) {
		put_attribute(atz_list[atz_dir_num].temp);
		put_class_device(atz_list[atz_dir_num].cdev);
	}
	clog(LOG_INFO, "exited.\n");
	return 0;
}

int acpi_temperature_parse(const char *ev, void **obj)
{
	char atz_name[32];
	struct temperature_interval *ret = calloc(1, sizeof(struct temperature_interval));
	if (ret == NULL) {
		clog(LOG_ERR, "couldn't make enough room for temperature_interval (%s)\n",
				strerror(errno));
		return -1;
	}

	clog(LOG_DEBUG, "called with: %s\n", ev);

	/* try to parse the %[a-zA-Z0-9]:%d-%d format first */
	if (sscanf(ev, "%32[a-zA-Z0-9_]:%d-%d", atz_name, &(ret->min), &(ret->max)) == 3) {
		/* validate zone name and assign pointer to struct thermal_zone */
		if ((ret->tz = get_thermal_zone(atz_name)) == NULL) {
			clog(LOG_ERR, "non existent thermal zone %s!\n", atz_name);
			free(ret);
			return -1;
		}
		clog(LOG_INFO, "parsed %s %d-%d\n",
				ret->tz->cdev->name, ret->min, ret->max);

	} else if (sscanf(ev, "%32[a-zA-Z0-9_]:%d", atz_name, &(ret->min)) == 2) {
		/* validate zone name and assign pointer to struct thermal_zone */
		if ((ret->tz = get_thermal_zone(atz_name)) == NULL) {
			clog(LOG_ERR, "non existent thermal zone %s!\n", atz_name);
			free(ret);
			return -1;
		}
		ret->max = ret->min;
		clog(LOG_INFO, "parsed %s %d\n", ret->tz->cdev->name, ret->min);

	} else if (sscanf(ev, "%d-%d", &(ret->min), &(ret->max)) == 2) {
		clog(LOG_INFO, "parsed %d-%d\n", ret->min, ret->max);

	} else if (sscanf(ev, "%d", &(ret->min)) == 1) {
		ret->max = ret->min;
		clog(LOG_INFO, "parsed %d\n", ret->min);

	} else {
		free(ret);
		return -1;
	}

	if (ret->min > ret->max) {
		clog(LOG_ERR, "Min higher than Max?\n");
		free(ret);
		return -1;
	}

	*obj = ret;
	return 0;
}

int acpi_temperature_evaluate(const void *s)
{
	const struct temperature_interval *ti = (const struct temperature_interval *)s;
	long int temp = temp_avg;

	if (ti != NULL && ti->tz != NULL)
		temp = ti->tz->temperature;

	clog(LOG_DEBUG, "called %d-%d [%s:%.1f]\n", ti->min, ti->max,
			ti != NULL && ti->tz != NULL ? ti->tz->cdev->name : "Avg",
			(float)temp / 1000);

	return (temp <= (ti->max * 1000) && temp >= (ti->min * 1000)) ? MATCH : DONT_MATCH;
}

/*  static int acpi_temperature_update(void)
 *
 *  reads temperature valuse ant compute a medium value
 */
int acpi_temperature_update(void)
{
	int count = 0, i = 0;

	clog(LOG_DEBUG, "called\n");

	temp_avg = 0;
	for (i = 0; i < atz_dir_num; i++) {

		if (read_int(atz_list[i].temp, &atz_list[i].temperature)) {
			continue;
		}
		count++;
		temp_avg += atz_list[i].temperature;
		clog(LOG_INFO, "temperature for %s is %.1fC\n",
				atz_list[i].cdev->name,
				(float)atz_list[i].temperature / 1000);
	}

	/* compute global medium value */
	if (count > 0) {
		temp_avg = (float)temp_avg / (float)count;
	}
	clog(LOG_INFO, "temperature average is %.1fC\n", (float)temp_avg / 1000);
	return 0;
}

#if 0
static struct cpufreqd_keyword kw[] = {
	{ .word = "acpi_temperature", .parse = &acpi_temperature_parse,   .evaluate = &acpi_temperature_evaluate },
	{ .word = NULL,               .parse = NULL,                      .evaluate = NULL }
};

static struct cpufreqd_plugin acpi_temperature = {
	.plugin_name      = "acpi_temperature_plugin",	/* plugin_name */
	.keywords         = kw,				/* config_keywords */
	.plugin_init      = &acpi_temperature_init,	/* plugin_init */
	.plugin_exit      = &acpi_temperature_exit,	/* plugin_exit */
	.plugin_update    = &acpi_temperature_update	/* plugin_update */
};

struct cpufreqd_plugin *create_plugin (void)
{
	return &acpi_temperature;
}
#endif
