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
#include "cpufreqd_acpi.h"
#include "cpufreqd_acpi_event.h"
#include "cpufreqd_acpi_battery.h"

#define ACPI_BATTERY_DIR        "/proc/acpi/battery/"
#define ACPI_BATTERY_STATE_FILE "/state"
#define ACPI_BATTERY_INFO_FILE  "/info"
#define ACPI_BATTERY_FULL_CAPACITY_FMT  "last full capacity:      %d %sh\n"
#define ACPI_BATTERY_REM_CAPACITY_FMT   "remaining capacity:      %d %sh\n"
#define ACPI_BATTERY_PRESENT_RATE	"present rate:            %d %s\n"
#define ACPI_BATTERY_DISCHARGING	"charging state:          discharging\n"
#define ACPI_BATTERY_CHARGING		"charging state:          charging\n"

struct battery_info {
	int capacity;
	float remaining;
	int present_rate;
	int present;
	int discharging;
	int level;
	char name[32];
	char path[100];
};

struct battery_interval {
	int min, max;
	struct battery_info *bat;
};

static struct battery_info *infos;
static int bat_num;
static int battery_level;
static double check_timeout;
static double old_time;
extern struct acpi_configuration acpi_config;

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
 *  check if the battery is present and read infos.
 */
static void check_battery(struct battery_info *info) {
	FILE *fp;
	char file_name[256];
	char ignore[100];
	char line[100];
	int tmp;

	snprintf(file_name, 256, "%s%s", info->path, ACPI_BATTERY_INFO_FILE);
	/** /proc/acpi/battery/.../info **/
	fp = fopen(file_name, "r");
	if (!fp) {
		clog(LOG_ERR , "%s: %s\n", file_name, strerror(errno));
		return;
	}

	/* reset some info */
	info->present = 0;

	while (fgets(line, 100, fp)) {
		if(sscanf(line, ACPI_BATTERY_FULL_CAPACITY_FMT, &tmp, ignore) == 2) {
			info->capacity = tmp;
			clog(LOG_DEBUG, "%s - capacity: %d\n", info->name,
					info->capacity);
			info->present = 1;
		}
	}
	fclose(fp);
}

/*  static int acpi_battery_init(void)
 *
 *  test if BATTERY dirs are present
 */
int acpi_battery_init(void) {
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

			clog(LOG_INFO, "%s battery path: %s, %s, capacity:%d\n",
					infos[n].name, infos[n].path, 
					infos[n].present?"present":"absent", infos[n].capacity);
			
			free(namelist[n]);
		}
		free(namelist);
		clog(LOG_INFO, "found %d battery slots\n", bat_num);

	} else if (n < 0) {
		clog(LOG_ERR, "error, acpi_battery module not compiled or inserted (%s: %s)?\n",
				ACPI_BATTERY_DIR, strerror(errno));
		clog(LOG_ERR, "exiting.\n");
		return -1;

	} else {
		clog(LOG_ERR, "no batteries found, not a laptop?\n");
		clog(LOG_ERR, "exiting.\n");
		return -1;
	}

	return 0;
}

int acpi_battery_exit(void) {
	if (infos != NULL) {
		free(infos);
	}
	clog(LOG_INFO, "exited.\n");
	return 0;
}

/*
 *  Parses entries of the form %d-%d (min-max)
 */
int acpi_battery_parse(const char *ev, void **obj) {
	char battery_name[32];
	struct battery_interval *ret = calloc(1, sizeof(struct battery_interval));
	if (ret == NULL) {
		clog(LOG_ERR, "couldn't make enough room for battery_interval (%s)\n",
				strerror(errno));
		return -1;
	}

	clog(LOG_DEBUG, "called with: %s\n", ev);

	/* try to parse the %[a-zA-Z0-9]:%d-%d format first */
	if (sscanf(ev, "%32[a-zA-Z0-9]:%d-%d", battery_name, &(ret->min), &(ret->max)) == 3) {
		/* validate battery name and assign pointer to struct battery_info */
		if ((ret->bat = get_battery_info(battery_name)) == NULL) {
			clog(LOG_ERR, "non existent thermal zone %s!\n",
					battery_name);
			free(ret);
			return -1;
		}
		clog(LOG_INFO, "parsed %s %d-%d\n", ret->bat->name, ret->min, ret->max);

	} else if (sscanf(ev, "%32[a-zA-Z0-9]:%d", battery_name, &(ret->min)) == 2) {
		/* validate battery name and assign pointer to struct battery_info */
		if ((ret->bat = get_battery_info(battery_name)) == NULL) {
			clog(LOG_ERR, "non existent thermal zone %s!\n",
					battery_name);
			free(ret);
			return -1;
		}
		ret->max = ret->min;
		clog(LOG_INFO, "parsed %s %d\n", ret->bat->name, ret->min);

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


int acpi_battery_evaluate(const void *s) {
	const struct battery_interval *bi = (const struct battery_interval *)s;
	int level = battery_level;

	if (bi != NULL && bi->bat != NULL) {
		level = bi->bat->present ? bi->bat->level : -1;
	}

	clog(LOG_DEBUG, "called %d-%d [%s:%d]\n", bi->min, bi->max, 
			bi != NULL && bi->bat != NULL ? bi->bat->name : "Medium", level);

	return (level >= bi->min && level <= bi->max) ? MATCH : DONT_MATCH;
}

/*  static int acpi_battery_update(void)
 *  
 *  reads temperature valuse ant compute a medium value
 */
int acpi_battery_update(void) {
	FILE *fp;
	char ignore[100];
	char file_name[256];
	char line[100];
	int i=0, total_capacity=0, total_remaining=0, tmp=0, n_read=0;
	double elapsed_time = 0.0;
	double current_time = 0.0;
#if 0
	int remaining_hours=0, remaining_minutes=0;
	double remaining_secs = 0.0;
#endif
	struct cpufreqd_info * cinfo = get_cpufreqd_info();
	
	current_time = (double)cinfo->timestamp.tv_sec + (cinfo->timestamp.tv_usec/1000000.0);
	elapsed_time = current_time - old_time;
	old_time = current_time;
	/* decrement timeout */
	check_timeout -= elapsed_time;

	/* Read battery informations */
	for (i=0; i<bat_num; i++) {

		/* if battery not present skip to the next one */
		if (!infos[i].present || infos[i].capacity <= 0) {
			continue;
		}

		/* if check_timeout is expired or an event is pending read battery */
		if (check_timeout <= 0 || is_event_pending()) {
			clog(LOG_DEBUG, "%s - reading battery\n", infos[i].name);
			check_timeout = acpi_config.battery_update_interval;
			/**
			 ** /proc/acpi/battery/.../state
			 **/
			snprintf(file_name, 256, "%s%s", infos[i].path, ACPI_BATTERY_STATE_FILE);
			fp = fopen(file_name, "r");
			if (!fp) {
				clog(LOG_ERR, "%s: %s\n", file_name, strerror(errno));
				clog(LOG_INFO, "battery path %s disappeared? "
						"send SIGHUP to re-read batteries\n",
						infos[i].path);
				continue;
			}

			infos[i].discharging = 0;
			while (fgets(line, 100, fp)) {
				if (sscanf(line, ACPI_BATTERY_REM_CAPACITY_FMT, &tmp, ignore) == 2) {
					infos[i].remaining = tmp;
					total_remaining += infos[i].remaining;
					total_capacity += infos[i].capacity;
					n_read++;
					clog(LOG_DEBUG, "%s - remaining capacity: %.2f\n",
							infos[i].name, infos[i].remaining);
				}
				if(sscanf(line, ACPI_BATTERY_PRESENT_RATE, &tmp, ignore) == 2) {
					infos[i].present_rate = tmp;
					clog(LOG_DEBUG, "%s - present rate: %d\n",
							infos[i].name, infos[i].present_rate);
				}
				if(strstr(line, ACPI_BATTERY_DISCHARGING) != NULL) {
					infos[i].discharging = 1;
				}
			}
			fclose(fp);
		} else {
			/* estimate battery life */
			clog(LOG_DEBUG, "%s - estimating battery life (timeout: %0.2f)\n",
					infos[i].name, check_timeout);
			if (infos[i].discharging)
				infos[i].remaining -= ((float)infos[i].present_rate * elapsed_time) / 3600.0;
			else if ((int)infos[i].remaining < infos[i].capacity)
				infos[i].remaining += ((float)infos[i].present_rate * elapsed_time) / 3600.0;
			total_remaining += infos[i].remaining;
			total_capacity += infos[i].capacity;
			n_read++;
			clog(LOG_DEBUG, "%s - remaining capacity: %.2f\n",
					infos[i].name, infos[i].remaining);
		}

		infos[i].level = 100 * (infos[i].remaining / (double)infos[i].capacity);
		clog(LOG_INFO, "battery life for %s is %d%%\n", infos[i].name, infos[i].level);
#if 0
		if (infos[i].present_rate > 0) {
			remaining_secs = 3600 * infos[i].remaining / infos[i].present_rate;
			remaining_hours = (int) remaining_secs / 3600;
			remaining_minutes = (remaining_secs - (remaining_hours * 3600)) / 60;
			clog(LOG_INFO, "battery time for %s is %d:%0.2d\n",
					infos[i].name, remaining_hours, remaining_minutes);
		}
#endif
	} /* end infos loop */

	/* calculates medium battery life between all batteries */
	if (total_capacity > 0)
		battery_level = 100 * (total_remaining / (double)total_capacity);
	else
		battery_level = -1;

	clog(LOG_INFO, "medium battery life %d%%\n", battery_level);

	return 0;
}


#if 0
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

struct cpufreqd_plugin *create_plugin (void) {
	return &acpi_battery;
}
#endif
