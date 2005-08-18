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

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpufreqd_plugin.h"

#define ACPI_BATTERY_DIR        "/proc/acpi/battery/"
#define ACPI_BATTERY_STATE_FILE "/state"
#define ACPI_BATTERY_INFO_FILE  "/info"
#define ACPI_BATTERY_FULL_CAPACITY_FMT  "last full capacity:      %d %sh\n"
#define ACPI_BATTERY_REM_CAPACITY_FMT   "remaining capacity:      %d %sh\n"

struct battery_info {
	int capacity;
	int present;
	int level;
	char name[32];
	char path[100];
};

struct battery_interval {
	int min, max;
	struct battery_info *bat;
};

static struct battery_info *infos   = 0L;
static int bat_num                  = 0;
static int battery_level            = 0;

static int acpi_battery_init(void);
static int acpi_battery_exit(void);
static int acpi_battery_parse(const char *ev, void **obj);
static int acpi_battery_evaluate(const void *s);
static int acpi_battery_update(void);

static struct cpufreqd_keyword kw[] = {
	{ .word = "battery_interval", .parse = &acpi_battery_parse, .evaluate = &acpi_battery_evaluate },
	{ .word = NULL, .parse = NULL, .evaluate = NULL, .free = NULL }
};

static struct cpufreqd_plugin acpi_battery = {
	.plugin_name      = "acpi_battery_plugin",      /* plugin_name */
	.keywords         = kw,                    /* config_keywords */
	.plugin_init      = &acpi_battery_init,         /* plugin_init */
	.plugin_exit      = &acpi_battery_exit,         /* plugin_exit */
	.plugin_update    = &acpi_battery_update,       /* plugin_update */
};

/* int no_dots(const struct dirent *d)
 * 
 * Filter function for scandir, returns
 * 0 if the first char of d_name is not
 * a dot.
 */
static int no_dots(const struct dirent *d) {
	return d->d_name[0]!= '.';
}

static struct battery_info *get_battery_info(const char *name)
{
	int i;
	struct battery_info *ret = NULL;
	
	for (i=0; i<bat_num; i++) {
		if (strncmp(infos[i].name, name, 32) == 0) {
			ret = &infos[i];
			break;
		}
	}
	return ret;
}

/*  static int check_battery(char *dirname)
 *
 *  check if the battery is present and read its capacity.
 *  Returns 1 if the battery is there and has been able
 *  to read its capacity, 0 otherwise.
 */
static int check_battery(struct battery_info *info) {
	FILE *fp;
	char file_name[256];
	char ignore[100];
	char line[100];
	int tmp_capacity;

	snprintf(file_name, 256, "%s%s", info->path, ACPI_BATTERY_INFO_FILE);
	/** /proc/acpi/battery/.../info **/
	fp = fopen(file_name, "r");
	if (!fp) {
		cpufreqd_log(LOG_ERR , "%s - check_battery(): %s: %s\n", 
				acpi_battery.plugin_name, file_name, strerror(errno));
		return 0;
	}

	while (fgets(line, 100, fp)) {
		if(sscanf(line, ACPI_BATTERY_FULL_CAPACITY_FMT, &tmp_capacity, ignore) == 2) {
			info->capacity = tmp_capacity;
			info->present = 1;
		}
	}
	fclose(fp);

	return (tmp_capacity != 0) ? 1 : 0;
}


/*  static int acpi_battery_init(void)
 *
 *  test if BATTERY dirs are present
 */
static int acpi_battery_init(void) {
	struct dirent **namelist = NULL;
	int n = 0;

	/* get batteries paths */
	bat_num = n = scandir(ACPI_BATTERY_DIR, &namelist, no_dots, NULL);
	if (n > 0) {
		infos = malloc( bat_num*sizeof(struct battery_info) );

		while (n--) {
			/* put the path into the array */
			snprintf(infos[n].name, 32, "%s", namelist[n]->d_name);
			snprintf(infos[n].path, 100, "%s%s", ACPI_BATTERY_DIR, namelist[n]->d_name);
			infos[n].present = 0;
			infos[n].capacity = 0;
			/* check this battery */
			check_battery(&(infos[n]));

			cpufreqd_log(LOG_INFO, "acpi_battery_init() - %s battery path: %s, %s, capacity:%d\n",
					infos[n].name, infos[n].path, 
					infos[n].present?"present":"absent", infos[n].capacity);
			
			free(namelist[n]);
		}
		free(namelist);
		cpufreqd_log(LOG_INFO, "acpi_battery_init() - found %d battery slots\n", bat_num);

	} else if (n < 0) {
		cpufreqd_log(LOG_ERR, "acpi_battery_init() - error, acpi_battery "
				"module not compiled or inserted (%s: %s)?\n",
				ACPI_BATTERY_DIR, strerror(errno));
		cpufreqd_log(LOG_ERR, "acpi_battery_init() - exiting.\n");
		return -1;   

	} else {
		cpufreqd_log(LOG_ERR, "acpi_battery_init() - no batteries found, not a laptop?\n");
		cpufreqd_log(LOG_ERR, "acpi_battery_init() - exiting.\n");
		return -1;
	}

	return 0;
}

static int acpi_battery_exit(void) {
	if (infos != NULL) {
		free(infos);
	}
	cpufreqd_log(LOG_INFO, "%s - exited.\n", acpi_battery.plugin_name);
	return 0;
}

/*
 *  Parses entries of the form %d-%d (min-max)
 */
static int acpi_battery_parse(const char *ev, void **obj) {
	char battery_name[32];
	struct battery_interval *ret = calloc(1, sizeof(struct battery_interval));
	if (ret == NULL) {
		cpufreqd_log(LOG_ERR, 
				"%s - acpi_battery_parse() couldn't make enough room for battery_interval (%s)\n",
				strerror(errno));
		return -1;
	}

	cpufreqd_log(LOG_DEBUG, "%s - acpi_battery_parse() called with: %s\n",
			acpi_battery.plugin_name, ev);

	/* try to parse the %[a-zA-Z0-9]:%d-%d format first */
	if (sscanf(ev, "%32[a-zA-Z0-9]:%d-%d", battery_name, &(ret->min), &(ret->max)) == 3) {
		/* validate battery name and assign pointer to struct battery_info */
		if ((ret->bat = get_battery_info(battery_name)) == NULL) {
			cpufreqd_log(LOG_ERR, "%s - acpi_battery_parse(): non existent thermal zone %s!\n",
					acpi_battery.plugin_name, battery_name);
			free(ret);
			return -1;
		}
		cpufreqd_log(LOG_INFO, "%s - acpi_battery_parse() parsed: %s %d-%d\n",
				acpi_battery.plugin_name, ret->bat->name, ret->min, ret->max);

	} else if (sscanf(ev, "%32[a-zA-Z0-9]:%d", battery_name, &(ret->min)) == 2) {
		/* validate battery name and assign pointer to struct battery_info */
		if ((ret->bat = get_battery_info(battery_name)) == NULL) {
			cpufreqd_log(LOG_ERR, "%s - acpi_battery_parse(): non existent thermal zone %s!\n",
					acpi_battery.plugin_name, battery_name);
			free(ret);
			return -1;
		}
		ret->max = ret->min;
		cpufreqd_log(LOG_INFO, "%s - acpi_battery_parse() parsed: %s %d\n",
				acpi_battery.plugin_name, ret->bat->name, ret->min);

	} else if (sscanf(ev, "%d-%d", &(ret->min), &(ret->max)) == 2) {
		cpufreqd_log(LOG_INFO, "%s - acpi_battery_parse() parsed: %d-%d\n",
				acpi_battery.plugin_name, ret->min, ret->max);

	} else if (sscanf(ev, "%d", &(ret->min)) == 1) {
		ret->max = ret->min;
		cpufreqd_log(LOG_INFO, "%s - acpi_battery_parse() parsed: %d\n",
				acpi_battery.plugin_name, ret->min);

	} else {
		free(ret);
		return -1;
	}

	if (ret->min > ret->max) {
		cpufreqd_log(LOG_ERR, "%s - acpi_battery_parse() Min higher than Max?\n",
				acpi_battery.plugin_name);
		free(ret);
		return -1;
	}

	*obj = ret;
	return 0;
}


static int acpi_battery_evaluate(const void *s) {
	const struct battery_interval *bi = (const struct battery_interval *)s;
	int level = battery_level;

	if (bi != NULL && bi->bat != NULL) {
		level = bi->bat->present ? bi->bat->level : -1;
	}

	cpufreqd_log(LOG_DEBUG, "%s - acpi_battery_evaluate() called: %d-%d [%s:%d]\n",
			acpi_battery.plugin_name, bi->min, bi->max, 
			bi != NULL && bi->bat != NULL ? bi->bat->name : "Medium", level);

	return (level >= bi->min && level <= bi->max) ? MATCH : DONT_MATCH;
}

/*  static int acpi_battery_update(void)
 *  
 *  reads temperature valuse ant compute a medium value
 */
static int acpi_battery_update(void) {
	FILE *fp;
	char ignore[100];
	char file_name[256];
	char line[100];
	int i=0, capacity=0, remaining=0, tmp_remaining=0, n_read=0;

	/* Read battery informations */
	for (i=0; i<bat_num; i++) {
#if 0
		/* avoid reading the info file if configured */
		if (!configuration->acpi_workaround) {
			check_battery(&infos[i]);
		}
#endif

		/* if battery not present skip to the next one */
		if (!infos[i].present || infos[i].capacity <= 0) {
			continue;
		}
		/**
		 ** /proc/acpi/battery/.../state
		 **/
		snprintf(file_name, 256, "%s%s", infos[i].path, ACPI_BATTERY_STATE_FILE);
		fp = fopen(file_name, "r");
		if (!fp) {
			cpufreqd_log(LOG_ERR, "acpi_battery_update(): %s: %s\n",
					file_name, strerror(errno));
			cpufreqd_log(LOG_INFO,
					"acpi_battery_update(): battery path %s disappeared? "
					"send SIGHUP to re-read batteries\n",
					infos[i].path);
			continue;
		}

		while (fgets(line, 100, fp)) {
			if (sscanf(line, ACPI_BATTERY_REM_CAPACITY_FMT, &tmp_remaining, ignore) == 2) {
				remaining += tmp_remaining;
				capacity += infos[i].capacity;
				infos[i].level = 100 * (tmp_remaining / (double)infos[i].capacity);
				n_read++;
				cpufreqd_log(LOG_INFO,
						"acpi_battery_update(): battery life for %s is %d%%\n",
						infos[i].name, infos[i].level);
			}
		}
		fclose(fp);
	} /* end infos loop */

	/* calculates medium battery life between all batteries */
	if (capacity > 0)
		battery_level = 100 * (remaining / (double)capacity);
	else
		battery_level = -1;

	cpufreqd_log(LOG_INFO, "acpi_battery_update(): battery life %d%%\n",
			battery_level);

	return 0;
}

struct cpufreqd_plugin *create_plugin (void) {
	return &acpi_battery;
}
