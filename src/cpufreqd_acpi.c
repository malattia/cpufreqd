/*
 *  Copyright (C) 2006  Mattia Dongili <malattia@linux.it>
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

#include "cpufreqd_plugin.h"
#include "cpufreqd_acpi_ac.h"
#include "cpufreqd_acpi_battery.h"
#include "cpufreqd_acpi_event.h"
#include "cpufreqd_acpi_temperature.h"

static short acpi_ac_failed;
static short acpi_batt_failed;
static short acpi_ev_failed;
static short acpi_temp_failed;

static int acpi_init (void) {
	clog(LOG_DEBUG, "Initializing AC\n");
	acpi_ac_failed = acpi_ac_init();
	clog(LOG_DEBUG, "Initializing BATTERY\n");
	acpi_batt_failed = acpi_battery_init();
	clog(LOG_DEBUG, "Initializing TEMPERATURE\n");
	acpi_temp_failed = acpi_temperature_init();
	return acpi_ac_failed || acpi_batt_failed || acpi_temp_failed;
}

static int acpi_post_conf (void) {
	clog(LOG_DEBUG, "Initializing EVENT\n");
	acpi_ev_failed = acpi_event_init();
	return acpi_ev_failed;
}

static int acpi_exit (void) {
	int ret = 0;
	clog(LOG_DEBUG, "Closing AC\n");
	ret |= acpi_ac_exit();
	clog(LOG_DEBUG, "Closing BATTERY\n");
	ret |= acpi_battery_exit();
	clog(LOG_DEBUG, "Closing TEMPERATURE\n");
	ret |= acpi_temperature_exit();
	clog(LOG_DEBUG, "Closing EVENT\n");
	ret |= acpi_event_exit();
	return ret;
}

static int acpi_update(void) {

	acpi_event_lock();
	if (!acpi_ac_failed && !acpi_ev_failed && is_event_pending())
		acpi_ac_update();

	if (!acpi_batt_failed && !acpi_ev_failed && is_event_pending())
		acpi_battery_update();

	reset_event();
	acpi_event_unlock();
	
	acpi_temperature_update();

	return 0;
}

static struct cpufreqd_keyword kw[] = {
	{ .word = "ac", .parse = &acpi_ac_parse, .evaluate = &acpi_ac_evaluate },
	{ .word = "battery_interval", .parse = &acpi_battery_parse, .evaluate = &acpi_battery_evaluate },
	{ .word = "acpi_temperature", .parse = &acpi_temperature_parse,   .evaluate = &acpi_temperature_evaluate },
	{ .word = NULL, .parse = NULL, .evaluate = NULL, .free = NULL }
};

static struct cpufreqd_plugin acpi = {
	.plugin_name	= "acpi",	/* plugin_name */
	.keywords	= kw,			/* config_keywords */
	.plugin_init	= &acpi_init,		/* plugin_init */
	.plugin_exit	= &acpi_exit,		/* plugin_exit */
	.plugin_update	= &acpi_update,		/* plugin_update */
	.plugin_conf	= &acpi_event_conf,
	.plugin_post_conf = &acpi_post_conf,
};

struct cpufreqd_plugin *create_plugin (void) {
	return &acpi;
}
