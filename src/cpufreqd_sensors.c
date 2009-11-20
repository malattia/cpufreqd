/*
 *  Copyright (C) 2005  Mattia Dongili<malattia@linux.it>
 *                      Franz Pletz <franz_pletz@web.de>
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
#include <errno.h>
#include <sensors/sensors.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpufreqd_plugin.h"

/* to hold monitored feature list and avoid reading all sensors */
struct sensors_monitor {
	const sensors_chip_name *chip;
	const sensors_feature_data *feat;
	double value;
	struct sensors_monitor *next;
};
static struct sensors_monitor *monitor_list;

/* object returned by parse_config pointer */
struct sensor_object {
	struct sensors_monitor *monitor;
	char name[MAX_STRING_LEN];
	double min;
	double max;
};

static const char *default_file_path[] =
  { "/etc", "/usr/local/etc", "/usr/lib/sensors", "/usr/local/lib/sensors",
    "/usr/lib", "/usr/local/lib", ".", 0 };

static char sensors_conffile[MAX_PATH_LEN];
static int init_success;

/*
 * Do initalization after having read our config section
 */
static int sensors_post_conf(void) {
	FILE *config = NULL;
	int i;

	/* open configured sensors config file */
	if (sensors_conffile[0] && (config = fopen(sensors_conffile, "r")) == NULL) {
		clog(LOG_NOTICE, "error opening configured sensors.conf: %s\n",
				strerror(errno));
		return -1;
	}

	/* try opening default files */
	for (i=0; config == NULL && default_file_path[i] != 0; i++) {
		snprintf(sensors_conffile, MAX_PATH_LEN, "%s/sensors.conf",
				default_file_path[i]);
		config = fopen(sensors_conffile, "r");
	}

	/* did we succeed opening a config file? */
	if(config == NULL) {
		clog(LOG_INFO, "no sensors.conf found, sensors disabled!\n");
		return -1;
	}

	clog(LOG_INFO, "using %s\n", sensors_conffile);

	if(sensors_init(config)) {
		clog(LOG_ERR, "sensors_init() failed, sensors disabled!\n");
		fclose(config);
		return -1;
	}
	fclose(config);
	init_success = 1;

	/* read all features name for later validation of directives */

	return 0;
}

/*
 * cleanup senesors if init was successful
 */
static int sensors_exit(void) {
	struct sensors_monitor *released = NULL;

	if (init_success)
		sensors_cleanup();

	/* free monitored features list */
	while (monitor_list != NULL) {
		released = monitor_list;
		monitor_list = monitor_list->next;
		free(released);
	}

	return 0;
}

/*
 * parse configuration entries
 */
static int sensors_conf(const char *key, const char *value) {

	if (strncmp(key, "sensors_conf", 11) == 0) {
		snprintf(sensors_conffile, MAX_PATH_LEN, "%s", value);
		clog(LOG_DEBUG, "configuration file is %s\n", sensors_conffile);
		return 0;
	}

	/* chip directive: use only said chips (usefull??) */
	return -1;
}

/* void get_sensors(void)
 *
 * Internal function to fill the list to-be-monitored features
 * with data.
 */
static int sensors_get(void) {

	struct sensors_monitor *list = monitor_list;

	while (list) {
		sensors_get_feature(*(list->chip), list->feat->number, &list->value);
		clog(LOG_INFO, "%s: %.3f\n", list->feat->name, list->value);
		list = list->next;
	}

	return 0;
}

/* this function can be pretty expensive (CPU time)?? */
static struct sensors_monitor * validate_feature_name(const char *name) {

	/* get all sensors from first chip */
	const sensors_chip_name *chip;
	const sensors_feature_data *feat;
	int nr = 0, nr1 = 0, nr2 = 0;
	struct sensors_monitor *list = monitor_list;
	struct sensors_monitor *ret = NULL;

	/* scan the full thing */
	while ( (chip = sensors_get_detected_chips(&nr)) != NULL) {
		nr1 = nr2 = 0;
		char *label = NULL;
		clog(LOG_DEBUG, "Examining chip %s(%d)\n", chip->prefix, nr);
		while ((feat = sensors_get_all_features(*chip, &nr1, &nr2)) != NULL) {
			/* sensor? */
			if(feat->mapping != SENSORS_NO_MAPPING)
				continue;

			if (sensors_get_label(*chip, feat->number, &label) != 0)
				clog(LOG_DEBUG, "Couldn't get label for %s (%s)\n",
						feat->name, strerror(errno));

			/* is it the one we are looking for? */
			if (strncmp(feat->name, name, MAX_STRING_LEN) != 0 &&
					(label && strncmp(label, name, MAX_STRING_LEN) != 0)) {
				free(label);
				continue;

			/* cache it */
			} else if ((ret = calloc(1, sizeof(struct sensors_monitor))) != NULL) {
				clog(LOG_DEBUG, "Creating new sensors_monitor for %s (%s)\n",
						label, feat->name);
				ret->chip = chip;
				ret->feat = feat;
				ret->next = NULL;
				/* free the label here, we are not using it anymore */
				free(label);
				/* append monitor to the cache list */
				list = monitor_list;
				if (list != NULL) {
					while (list->next != NULL) {
						list = list->next;
					}
					list->next = ret;
				} else {
					monitor_list = ret;
				}
				return ret;
			/* somethign went wrong... */
			} else {
				clog(LOG_ERR, "Couldn't create new sensor monitor for %s (%s)\n",
						name, strerror(errno));
				break;
			}
		}
	}
	return NULL;
}

static int sensor_parse(const char *ev, void **obj) {

	struct sensor_object *ret = calloc(1, sizeof(struct sensor_object));
	if (ret == NULL) {
		clog(LOG_ERR, "couldn't make enough room for a sensor_object (%s)\n",
				strerror(errno));
		return -1;
	}

	clog(LOG_DEBUG, "called with %s\n", ev);

	/* try to parse the %[a-zA-Z0-9]:%d-%d format first */
	if (sscanf(ev, "%32[^:]:%lf-%lf", ret->name, &ret->min, &ret->max) == 3) {
		/* validate feature name */
		if ((ret->monitor = validate_feature_name(ret->name)) != NULL) {
			clog(LOG_INFO, "parsed %s %.3f-%.3f\n", ret->name, ret->min, ret->max);
			*obj = ret;
		}
		else {
			clog(LOG_ERR, "feature \"%s\" does not exist, try 'sensors -u' "
					"to see a full list of available feature names.\n",
					ret->name);
			free(ret);
			return -1;
		}
	} else {
		free(ret);
		return -1;
	}

	return 0;
}

static int sensor_evaluate(const void *s) {
	const struct sensor_object *so = (const struct sensor_object *) s;

	clog(LOG_DEBUG, "called %.3f-%.3f [%s:%.3f]\n", so->min, so->max,
			so->name, so->monitor->value);

	return (so->monitor->value >= so->min && so->monitor->value <= so->max) ? MATCH : DONT_MATCH;
}


static struct cpufreqd_keyword kw[] = {
	{ .word = "sensor", .parse = &sensor_parse, .evaluate = &sensor_evaluate },
	{ .word = NULL, .parse = NULL, .evaluate = NULL, .free = NULL }
};

static struct cpufreqd_plugin sensors_plugin = {
	.plugin_name	= "sensors_plugin",	/* plugin_name */
	.keywords	= kw,			/* config_keywords */
#if 0
	.plugin_init	= &sensors_init,	/* plugin_init */
#endif
	.plugin_exit	= &sensors_exit,	/* plugin_exit */
	.plugin_update	= &sensors_get,		/* plugin_update */
	.plugin_conf	= &sensors_conf,
	.plugin_post_conf= &sensors_post_conf
};

/* MUST DEFINE THIS ONE */
struct cpufreqd_plugin *create_plugin (void) {
	return &sensors_plugin;
}
