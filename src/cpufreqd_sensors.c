/*
 *  Copyright (C) 2005  Mattia Dongili<dongili@supereva.it>
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
	sensors_chip_name *chip;
	sensors_feature_data *feat;
	double value;
	struct sensors_monitor *next;
};
static struct sensors_monitor *monitor_list;

/* object returned by parse_config pointer */
struct sensors_object {
	struct sensors_monitor *monitor;
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
		cpufreqd_log(LOG_NOTICE, "%s: error opening configured sensors.conf: %s\n",
				__func__, strerror(errno));
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
		cpufreqd_log(LOG_ERR, "%s: no sensors.conf found, "
				"sensors disabled!\n", __func__);
		return -1;
	}

	cpufreqd_log(LOG_NOTICE, "%s: using %s\n", __func__, sensors_conffile);

	if(sensors_init(config)) {
		cpufreqd_log(LOG_ERR, "%s: sensors_init() failed, "
				"sensosrs disabled!\n", __func__);
		fclose(config);
		return -1;
	}
	fclose(config);
	init_success = 1;
	return 0;
}

/*
 * cleanup senesors if init was successful
 */
static int sensors_exit(void) {
	struct sensors_monitor *sm = NULL;

	if (init_success)
		sensors_cleanup();

	/* free monitored features list */
	while (monitor_list != NULL) {
		sm = monitor_list;
		monitor_list = monitor_list->next;
		free(sm);
	}
	
	return 0;
}

/*
 * parse configuration entries
 */
static int sensors_conf(const char *key, const char *value) {

	if (strncmp(key, "sensors_conf", 11) == 0) {
		snprintf(sensors_conffile, MAX_PATH_LEN, "%s/sensors.conf", value);
		return 0;
	}

	/* chip directive: use only said chips (usefull??) */
	return -1;
}

/* void get_sensors(general *configuration)
 *
 * Internal function to fill sensors and sensor_names
 * with data.
 *
 * Returns nothing, but sets configuration->have_sensors
 */
static int sensors_get(void) {

	/* get all sensors from first chip */
	const sensors_chip_name *chip;
	const sensors_feature_data *feat;
	int i = 0, nr=0, nr1=0, nr2=0;
	double res;

	cpufreqd_log(LOG_DEBUG, "get_sensors(): getting sensors names\n");
	while ( (chip = sensors_get_detected_chips(&nr)) != NULL) {
		nr1 = nr2 = 0;
		
		cpufreqd_log(LOG_DEBUG, "get_sensors(): chip#%d - prefix=%s, bus=%x, addr=%x, busname=%s\n",
				nr, chip->prefix, chip->bus, chip->addr, chip->busname);

		/* until our buffer is full */
		while ((feat = sensors_get_all_features(*chip, &nr1, &nr2)) != NULL) {

			/* sensor? */
			if(feat->mapping != SENSORS_NO_MAPPING) {
				/*cpufreqd_log(LOG_DEBUG, "SENSORS_NO_MAPPING %s\n", feat->name);*/
				continue;
			}

			sensors_get_feature(*chip, feat->number, &res);
			cpufreqd_log(LOG_DEBUG, "%s: %f\n", feat->name, res);
			i++;
		}
	}

	cpufreqd_log(LOG_DEBUG, "get_sensors(): read %d features\n", i);
	return 0;
}

static int sensor_parse(const char *ev, void **obj) {
	return -1;
}
static int sensor_evaluate(const void *s) {
	return DONT_MATCH;
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
#if 0
int main (int argc, char *argv[]) {
	FILE * f = fopen("/etc/sensors.conf", "r");
	sensors_init(f);
	fclose(f);
	get_sensors();
	sensors_cleanup();	
	return 0;
}
#endif
