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
#include "cpufreqd_plugin.h"
#include "cpufreqd_acpi_temperature.h"

#define ACPI_TEMPERATURE_DIR "/proc/acpi/thermal_zone/"
#define ACPI_TEMPERATURE_FILE "temperature"
#define ACPI_TEMPERATURE_FORMAT "temperature:             %ld C\n"

struct thermal_zone {
	char name[32];
	char zone_path[64];
	long int temperature;
};

struct temperature_interval {
	int min, max;
	struct thermal_zone *tz;
};

static struct thermal_zone *atz_list;
static int temp_dir_num;
static long int temperature;

static int no_dots(const struct dirent *d)
{
	return d->d_name[0]!= '.';
}

static struct thermal_zone *get_thermal_zone(const char *name)
{
	int i;
	struct thermal_zone *ret = NULL;
	
	for (i=0; i<temp_dir_num; i++) {
		if (strncmp(atz_list[i].name, name, 32) == 0) {
			ret = &atz_list[i];
			break;
		}
	}
	return ret;
}

/*  static int acpi_temperature_init(void)
 *
 *  test if ATZ dirs are present and read their 
 *  path for usage when parsing rules
 */
int acpi_temperature_init(void)
{
	struct dirent **namelist = NULL;
	int n = 0;

	/* get ATZ path */
	n = scandir(ACPI_TEMPERATURE_DIR, &namelist, no_dots, NULL);
	if (n > 0) {
		temp_dir_num = n;
		atz_list = malloc(n * sizeof(struct thermal_zone));
		while (n--) {
			snprintf(atz_list[n].name, 32, "%s", namelist[n]->d_name);
			snprintf(atz_list[n].zone_path, 64, "%s%s/",
					ACPI_TEMPERATURE_DIR, namelist[n]->d_name);
			clog(LOG_INFO, "TEMP path: %s name: %s\n",
					atz_list[n].zone_path, atz_list[n].name);
			free(namelist[n]);
		} 
		free(namelist);

	} else if (n < -1) {
		clog(LOG_NOTICE, "no acpi_temperature support %s:%s\n", 
				ACPI_TEMPERATURE_DIR, strerror(errno));
		return -1;

	} else {
		clog(LOG_NOTICE, "no acpi_temperature support found %s\n", 
				ACPI_TEMPERATURE_DIR);
		return -1;
	}

	return 0;
}

int acpi_temperature_exit(void) 
{
	if (atz_list != NULL)
		free(atz_list);
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
	if (sscanf(ev, "%32[a-zA-Z0-9]:%d-%d", atz_name, &(ret->min), &(ret->max)) == 3) {
		/* validate zone name and assign pointer to struct thermal_zone */
		if ((ret->tz = get_thermal_zone(atz_name)) == NULL) {
			clog(LOG_ERR, "non existent thermal zone %s!\n", atz_name);
			free(ret);
			return -1;
		}
		clog(LOG_INFO, "parsed %s %d-%d\n",
				ret->tz->name, ret->min, ret->max);

	} else if (sscanf(ev, "%32[a-zA-Z0-9]:%d", atz_name, &(ret->min)) == 2) {
		/* validate zone name and assign pointer to struct thermal_zone */
		if ((ret->tz = get_thermal_zone(atz_name)) == NULL) {
			clog(LOG_ERR, "non existent thermal zone %s!\n", atz_name);
			free(ret);
			return -1;
		}
		ret->max = ret->min;
		clog(LOG_INFO, "parsed %s %d\n", ret->tz->name, ret->min);

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
	long int temp = temperature;

	if (ti != NULL && ti->tz != NULL)
		temp = ti->tz->temperature;
		
	clog(LOG_DEBUG, "called %d-%d [%s:%d]\n", ti->min, ti->max, 
			ti != NULL && ti->tz != NULL ? ti->tz->name : "Medium", temp);

	return (temp <= ti->max && temp >= ti->min) ? MATCH : DONT_MATCH;
}

/*  static int acpi_temperature_update(void)
 *  
 *  reads temperature valuse ant compute a medium value
 */
int acpi_temperature_update(void)
{
	char fname[256];
	int count = 0, i = 0;
	long int t = 0;
	FILE *fp = NULL;

	clog(LOG_DEBUG, "called\n");

	temperature = 0;
	for (i=0; i<temp_dir_num; i++) {
		snprintf(fname, 255, "%s%s", atz_list[i].zone_path, ACPI_TEMPERATURE_FILE);
		fp = fopen(fname, "r");
		if (fp) {
			if (fscanf(fp, ACPI_TEMPERATURE_FORMAT, &t) == 1) {
				count++;
				temperature += t;
				atz_list[i].temperature = t;
				clog(LOG_INFO, "temperature for %s is %ldC\n",
						atz_list[i].name, atz_list[i].temperature);
			}
			fclose(fp);

		} else {
			clog(LOG_ERR, "%s: %s\n", fname, strerror(errno));
			clog(LOG_ERR, "ATZ path %s disappeared? send SIGHUP to re-read Temp zones\n",
					atz_list[i].zone_path);
		}
	}

	/* compute global medium value */
	if (count > 0) {
		temperature = (float)temperature / (float)count;
	}
	clog(LOG_INFO, "medium temperature is %ldC\n", temperature);
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
