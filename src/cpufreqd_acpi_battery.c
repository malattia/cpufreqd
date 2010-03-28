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

#define POWER_SUPPLY	"power_supply"
#define BATTERY_TYPE	"Battery"
#define ENERGY_FULL	"energy_full"
#define ENERGY_NOW	"energy_now"
#define CHARGE_FULL	"charge_full"
#define CHARGE_NOW	"charge_now"
#define PRESENT		"present"
#define STATUS		"status"
#define CURRENT_NOW	"current_now"

struct battery_info {
	int capacity;
	int remaining;
	int present_rate;
	int level; /* computed percentage */
	int is_present;

	struct sysfs_class_device *cdev;
	struct sysfs_attribute *energy_full; /* last full capacity */
	struct sysfs_attribute *energy_now; /* remaining capacity */
	struct sysfs_attribute *present;
	struct sysfs_attribute *status;
	struct sysfs_attribute *current_now; /* present rate */

	int open;
};

struct battery_interval {
	int min, max;
	struct battery_info *bat;
};

/* don't want to handle more than 8 batteries... yet */
static struct battery_info info[8];
static int bat_dir_num;
static int avg_battery_level;
static double check_timeout;
static double old_time;
extern struct acpi_configuration acpi_config;

/* validate if the requested battery exists */
static struct battery_info *get_battery_info(const char *name)
{
	int i;
	struct battery_info *ret = NULL;

	for (i = 0; i < bat_dir_num; i++) {
		if (strncmp(info[i].cdev->name, name, SYSFS_NAME_LEN) == 0) {
			ret = &info[i];
			break;
		}
	}
	return ret;
}

/* close all the attributes and reset the open status */
static void close_battery(struct battery_info *binfo) {

	if (!binfo->open) return;

	if (binfo->energy_full)
		put_attribute(binfo->energy_full);
	if (binfo->energy_now)
		put_attribute(binfo->energy_now);
	if (binfo->present)
		put_attribute(binfo->present);
	if (binfo->status)
		put_attribute(binfo->status);
	if (binfo->current_now)
		put_attribute(binfo->current_now);

	binfo->open = 0;
}
/* read battery levels as reported by hw */
static int read_battery(struct battery_info *binfo) {
	clog(LOG_DEBUG, "%s - reading battery levels\n", binfo->cdev->name);

	if (read_int(binfo->current_now, &binfo->present_rate) != 0) {
		clog(LOG_ERR, "Skipping %s\n", binfo->cdev->name);
		return -1;
	}
	if (read_int(binfo->energy_now, &binfo->remaining) != 0) {
		clog(LOG_ERR, "Skipping %s\n", binfo->cdev->name);
		return -1;
	}
	if (read_value(binfo->status) != 0) {
		clog(LOG_ERR, "Skipping %s\n", binfo->cdev->name);
		return -1;
	}
	clog(LOG_DEBUG, "%s - remaining capacity: %d\n",
			binfo->cdev->name, binfo->remaining);
	return 0;
}
/* open all the required attributes and set the open status */
static int open_battery(struct battery_info *binfo) {
	binfo->open = 1;

	binfo->energy_full = get_class_device_attribute(binfo->cdev, ENERGY_FULL);
	if (!binfo->energy_full) {
		/* try the "charge_full" name */
		binfo->energy_full = get_class_device_attribute(binfo->cdev,
				CHARGE_FULL);
		if (!binfo->energy_full)
			return -1;
	}
	binfo->energy_now = get_class_device_attribute(binfo->cdev, ENERGY_NOW);
	if (!binfo->energy_now) {
		/* try the "charge_now" name */
		binfo->energy_now = get_class_device_attribute(binfo->cdev, CHARGE_NOW);
		if (!binfo->energy_now)
			return -1;
	}
	binfo->present = get_class_device_attribute(binfo->cdev, PRESENT);
	if (!binfo->present)
		return -1;
	binfo->status = get_class_device_attribute(binfo->cdev, STATUS);
	if (!binfo->status)
		return -1;
	binfo->current_now = get_class_device_attribute(binfo->cdev, CURRENT_NOW);
	if (!binfo->current_now)
		return -1;

	/* read the last full capacity, this is not going to change
	 * very often, so no need to poke it later */
	if (read_int(binfo->energy_full, &binfo->capacity) != 0) {
		clog(LOG_WARNING, "Couldn't read %s capacity (%s)\n",
				binfo->cdev->name, strerror(errno));
		return -1;
	}
	return 0;
}

/* set the battery class device into the battery_info array */
static int clsdev_callback(struct sysfs_class_device *cdev) {
	clog(LOG_DEBUG, "Got device %s\n", cdev->name);
	info[bat_dir_num].cdev = cdev;
	bat_dir_num++;
	return 0;
}

/*  int acpi_battery_init(void)
 *
 *  this never fails since batteries are hotpluggable and
 *  we can easily rescan for availability later (see acpi_battery_update
 *  when an event is pending)
 */
short int acpi_battery_init(void) {
	int i;

	find_class_device(POWER_SUPPLY, BATTERY_TYPE, &clsdev_callback);
	if (bat_dir_num <= 0) {
		clog(LOG_INFO, "No Batteries found\n");
		return 0;
	}
	/* open the required attributes */
	for (i = 0; i < bat_dir_num; i++) {
		clog(LOG_DEBUG, "Opening %s attributes\n", info[i].cdev->name);
		if (open_battery(&info[i]) != 0) {
			clog(LOG_WARNING, "Couldn't open %s attributes\n",
					info[i].cdev->name);
			close_battery(&info[i]);
		}
	}
	clog(LOG_INFO, "found %d Batter%s\n", bat_dir_num,
			bat_dir_num > 1 ? "ies" : "y");
	return 0;
}
short int acpi_battery_exit(void) {
	/* also reset values since this is called on pending
	 * acpi events to rescan batteries
	 */
	while (--bat_dir_num >= 0) {
		close_battery(&info[bat_dir_num]);
		put_class_device(info[bat_dir_num].cdev);
		info[bat_dir_num].cdev = NULL;
	}
	bat_dir_num = 0;
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
			clog(LOG_ERR, "non existent battery %s!\n",
					battery_name);
			free(ret);
			return -1;
		}
		clog(LOG_INFO, "parsed %s %d-%d\n", ret->bat->cdev->name, ret->min, ret->max);

	} else if (sscanf(ev, "%32[a-zA-Z0-9]:%d", battery_name, &(ret->min)) == 2) {
		/* validate battery name and assign pointer to struct battery_info */
		if ((ret->bat = get_battery_info(battery_name)) == NULL) {
			clog(LOG_ERR, "non existent battery %s!\n",
					battery_name);
			free(ret);
			return -1;
		}
		ret->max = ret->min;
		clog(LOG_INFO, "parsed %s %d\n", ret->bat->cdev->name, ret->min);

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
	int level = avg_battery_level;

	if (bi != NULL && bi->bat != NULL) {
		level = bi->bat->present->value ? bi->bat->level : -1;
	}

	clog(LOG_DEBUG, "called %d-%d [%s:%d]\n", bi->min, bi->max,
			bi != NULL && bi->bat != NULL ? bi->bat->cdev->name : "Avg", level);

	return (level >= bi->min && level <= bi->max) ? MATCH : DONT_MATCH;
}

/*  static int acpi_battery_update(void)
 *
 *  reads temperature valuse ant compute a medium value
 */
int acpi_battery_update(void) {
	int i = 0, total_capacity = 0, total_remaining = 0, n_read = 0;
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

	/* if there is a pending event rescan batteries */
	if (is_event_pending()) {
		clog(LOG_NOTICE, "Re-scanning available batteries\n");
		acpi_battery_exit();
		acpi_battery_init();
		/* force timeout expiration */
		check_timeout = -1;
	}

	/* Read battery informations */
	for (i = 0; i < bat_dir_num; i++) {

		if (read_int(info[i].present, &info[i].is_present) != 0) {
			clog(LOG_INFO, "Skipping %s\n", info[i].cdev->name);
			continue;
		}

		/* if battery not open or not present skip to the next one */
		if (!info[i].open || !info[i].is_present || info[i].capacity <= 0) {
			continue;
		}
		clog(LOG_INFO, "%s - present\n", info[i].cdev->name);

		/* if check_timeout is expired */
		if (check_timeout <= 0) {
			if (read_battery(&info[i]) == 0)
				n_read++;
			else
				clog(LOG_INFO, "Unable to read battery %s\n",
						info[i].cdev->name);
		} else {
			/* estimate battery life */
			clog(LOG_DEBUG, "%s - estimating battery life (timeout: %0.2f"
					" - status: %s)\n",
					info[i].cdev->name, check_timeout,
					info[i].status->value);

			if (strncmp(info[i].status->value, "Discharging", 11) == 0)
				info[i].remaining -= ((float)info[i].present_rate * elapsed_time) / 3600.0;

			else if (strncmp(info[i].status->value, "Full", 4) != 0 &&
					(int)info[i].remaining < info[i].capacity)
				info[i].remaining += ((float)info[i].present_rate * elapsed_time) / 3600.0;

			clog(LOG_DEBUG, "%s - remaining capacity: %d\n",
					info[i].cdev->name, info[i].remaining);
		}
		n_read++;
		total_remaining += info[i].remaining;
		total_capacity += info[i].capacity;

		info[i].level = 100 * (info[i].remaining / (double)info[i].capacity);
		clog(LOG_INFO, "battery life for %s is %d%%\n", info[i].cdev->name, info[i].level);
#if 0
		if (info[i].present_rate > 0) {
			remaining_secs = 3600 * info[i].remaining / info[i].present_rate;
			remaining_hours = (int) remaining_secs / 3600;
			remaining_minutes = (remaining_secs - (remaining_hours * 3600)) / 60;
			clog(LOG_INFO, "battery time for %s is %d:%0.2d\n",
					info[i].cdev->name, remaining_hours, remaining_minutes);
		}
#endif
	} /* end info loop */

	/* check_timeout is global for all batteries, so update it after all batteries got updated */
	if (check_timeout <= 0) {
		check_timeout = acpi_config.battery_update_interval;
	}

	/* calculates medium battery life between all batteries */
	if (total_capacity > 0)
		avg_battery_level = 100 * (total_remaining / (double)total_capacity);
	else
		avg_battery_level = -1;

	clog(LOG_INFO, "average battery life %d%%\n", avg_battery_level);

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
