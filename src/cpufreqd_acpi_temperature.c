/*
 *  Copyright (C) 2002-2005  Mattia Dongili <malattia@gmail.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include "cpufreqd_plugin.h"
#include "cpufreqd.h"

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

static int no_dots(const struct dirent *d);
static int acpi_temperature_init(void);
static int acpi_temperature_exit(void);
static int acpi_temperature_parse(const char *ev, void **obj);
static int acpi_temperature_evaluate(const void *s);
static int acpi_temperature_update(void);

static struct cpufreqd_keyword kw[] = {
	{ .word = "acpi_temperature", .parse = &acpi_temperature_parse,   .evaluate = &acpi_temperature_evaluate },
	{ .word = NULL,               .parse = NULL,                      .evaluate = NULL }
};

static struct cpufreqd_plugin acpi_temperature = {
	.plugin_name      = "acpi_temperature_plugin",      /* plugin_name */
	.keywords         = kw,                             /* config_keywords */
	.poll_interval    = 1000,                           /* poll_interval (1 second) */
	.plugin_init      = &acpi_temperature_init,         /* plugin_init */
	.plugin_exit      = &acpi_temperature_exit,         /* plugin_exit */
	.plugin_update    = &acpi_temperature_update,       /* plugin_update */
	.cfdprint         = NULL
};

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
static int acpi_temperature_init(void)
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
			acpi_temperature.cfdprint(LOG_INFO, 
					"acpi_temperature_init() - TEMP path: %s name: %s\n",
					atz_list[n].zone_path, atz_list[n].name);
			free(namelist[n]);
		} 
		free(namelist);

	} else if (n < -1) {
		acpi_temperature.cfdprint(LOG_NOTICE, 
				"acpi_temperature_init(): no acpi_temperature support %s:%s\n", 
				ACPI_TEMPERATURE_DIR,
				strerror(errno));
		return -1;

	} else {
		acpi_temperature.cfdprint(LOG_NOTICE, 
				"acpi_temperature_init(): no acpi_temperature support found %s\n", 
				ACPI_TEMPERATURE_DIR);
		return -1;
	}

	return 0;
}

static int acpi_temperature_exit(void) 
{
	if (atz_list != NULL)
		free(atz_list);
	acpi_temperature.cfdprint(LOG_INFO, "%s - exited.\n", acpi_temperature.plugin_name);
	return 0;
}

static int acpi_temperature_parse(const char *ev, void **obj)
{
	char atz_name[32];
	struct temperature_interval *ret = calloc(1, sizeof(struct temperature_interval));
	if (ret == NULL) {
		acpi_temperature.cfdprint(LOG_ERR, 
				"%s - acpi_temperature_parse() couldn't "
				"make enough room for temperature_interval (%s)\n",
				strerror(errno));
		return -1;
	}

	acpi_temperature.cfdprint(LOG_DEBUG, "%s - acpi_temperature_parse() called with: %s\n",
			acpi_temperature.plugin_name, ev);

	/* try to parse the %[a-zA-Z0-9]:%d-%d format first */
	if (sscanf(ev, "%32[a-zA-Z0-9]:%d-%d", atz_name, &(ret->min), &(ret->max)) == 3) {
		/* validate zone name and assign pointer to struct thermal_zone */
		if ((ret->tz = get_thermal_zone(atz_name)) == NULL) {
			acpi_temperature.cfdprint(LOG_ERR, "%s - acpi_temperature_parse(): non existent thermal zone %s!\n",
					acpi_temperature.plugin_name, atz_name);
			free(ret);
			return -1;
		}
		acpi_temperature.cfdprint(LOG_INFO, "%s - acpi_temperature_parse() parsed: %s %d-%d\n",
				acpi_temperature.plugin_name, ret->tz->name, ret->min, ret->max);

	} else if (sscanf(ev, "%32[a-zA-Z0-9]:%d", atz_name, &(ret->min)) == 2) {
		/* validate zone name and assign pointer to struct thermal_zone */
		if ((ret->tz = get_thermal_zone(atz_name)) == NULL) {
			acpi_temperature.cfdprint(LOG_ERR, "%s - acpi_temperature_parse(): non existent thermal zone %s!\n",
					acpi_temperature.plugin_name, atz_name);
			free(ret);
			return -1;
		}
		ret->max = ret->min;
		acpi_temperature.cfdprint(LOG_INFO, "%s - acpi_temperature_parse() parsed: %s %d\n",
				acpi_temperature.plugin_name, ret->tz->name, ret->min);

	} else if (sscanf(ev, "%d-%d", &(ret->min), &(ret->max)) == 2) {
		acpi_temperature.cfdprint(LOG_INFO, "%s - acpi_temperature_parse() parsed: %d-%d\n",
				acpi_temperature.plugin_name, ret->min, ret->max);

	} else if (sscanf(ev, "%d", &(ret->min)) == 1) {
		ret->max = ret->min;
		acpi_temperature.cfdprint(LOG_INFO, "%s - acpi_temperature_parse() parsed: %d\n",
				acpi_temperature.plugin_name, ret->min);

	} else {
		free(ret);
		return -1;
	}

	if (ret->min > ret->max) {
		acpi_temperature.cfdprint(LOG_ERR, "%s - acpi_temperature_parse() Min higher than Max?\n",
				acpi_temperature.plugin_name);
		free(ret);
		return -1;
	}

	*obj = ret;
	return 0;
}

static int acpi_temperature_evaluate(const void *s)
{
	const struct temperature_interval *ti = (const struct temperature_interval *)s;
	long int temp = 0;

	if (ti != NULL && ti->tz != NULL)
		temp = ti->tz->temperature;
	else 
		temp = temperature;
		
	acpi_temperature.cfdprint(LOG_DEBUG, "%s - evaluate() called: %d-%d [%s:%d]\n",
			acpi_temperature.plugin_name, ti->min, ti->max, 
			ti != NULL && ti->tz != NULL ? ti->tz->name : "Medium", temp);

	return (temp <= ti->max && temp >= ti->min) ? MATCH : DONT_MATCH;
}

/*  static int acpi_temperature_update(void)
 *  
 *  reads temperature valuse ant compute a medium value
 */
static int acpi_temperature_update(void)
{
	char fname[256];
	int count = 0, i = 0;
	long int t = 0;
	FILE *fp = NULL;

	acpi_temperature.cfdprint(LOG_DEBUG, "%s - update() called\n", acpi_temperature.plugin_name);

	temperature = 0;
	for (i=0; i<temp_dir_num; i++) {
		snprintf(fname, 255, "%s%s", atz_list[i].zone_path, ACPI_TEMPERATURE_FILE);
		fp = fopen(fname, "r");
		if (fp) {
			if (fscanf(fp, ACPI_TEMPERATURE_FORMAT, &t) == 1) {
				count++;
				temperature += t;
				atz_list[i].temperature = t;
				acpi_temperature.cfdprint(LOG_INFO,
						"acpi_temperature_update(): temperature for %s is %ldC\n",
						atz_list[i].name, temperature);
			}
			fclose(fp);

		} else {
			acpi_temperature.cfdprint(LOG_ERR, "acpi_temperature_update(): %s: %s\n",
					fname, strerror(errno));
			acpi_temperature.cfdprint(LOG_ERR,
					"acpi_temperature_update(): ATZ path %s disappeared?"
					"send SIGHUP to re-read Temp zones\n",
					atz_list[i].zone_path);
		}
	}

	/* compute global medium value */
	if (count > 0) {
		temperature = (float)temperature / (float)count;
	}
	acpi_temperature.cfdprint(LOG_INFO, "acpi_temperature_update(): medium temperature is %ldC\n",
			temperature);
	return 0;
}

struct cpufreqd_plugin *create_plugin (void)
{
	return &acpi_temperature;
}
