/*
 *  Copyright (C) 2006  Heiko Noordhof <heiko.noordhof@xs4all.nl>
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

/*
 * Governor parameter plugin for cpufreqd
 * Version 0.3
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <libsysfs.h>

#include "cpufreqd_plugin.h"

#define BUFLEN_PARAMETER_VALUE 24
#define SYS_CPU_DIR "devices/system/cpu"

static unsigned int number_of_cpus = 1;
static char syspath_cpu[SYSFS_PATH_MAX];

static const char *supported_governors[] = {
	"ondemand",
	"conservative",
	NULL
};


/* Governor parameter object struct.
 */
struct gov_parameter {
	char parameter_name[SYSFS_NAME_LEN];
	long int parameter_value;
	short int is_percentage;
};


/* Opens a sysfs device for the cpufreq governor of a given CPU number.
 * Returns a pointer to the device if successfull, NULL on error.
 */
static struct sysfs_device *open_governor_device(unsigned int cpu, const char *governor)
{
	char govdev_path[SYSFS_PATH_MAX];
	struct sysfs_device *govdev = NULL;

	/* Construct sysfs path string for governor device of this cpu */
	snprintf(govdev_path, SYSFS_PATH_MAX, "%s/cpu%u/cpufreq/%s", syspath_cpu, cpu, governor);
	clog(LOG_DEBUG, "sysfs path governor device = %s\n", govdev_path);

	/* Open sysfs governor device */
	govdev = sysfs_open_device_path(govdev_path);
	if (govdev == NULL) {
		clog(LOG_ERR, "ERROR: device for governor %s not found in sysfs for cpu%u.\n", governor, cpu);
		return NULL;
	}
	return govdev;
}


/* Reads the value of 'parameter' of the active governor
 * Returns the paramter value, or -1 on error.
 */
static long int get_parameter(struct sysfs_device *govdev, const char *parameter)
{
	struct sysfs_attribute *govattr = NULL;

	govattr = sysfs_get_device_attr(govdev, parameter);
	if (govattr == NULL) {
		clog(LOG_WARNING, "warning: attribute %s not found in sysfs.\n", parameter);
		return -1;
	}
	return atol(govattr->value);
}


/* Sets the value of 'parameter' of 'governor' to 'value'.
 */
static void set_parameter(const unsigned int cpu, const char *governor,
		const char *parameter, long int value, int is_percentage)
{
	char value_str[BUFLEN_PARAMETER_VALUE];
	struct sysfs_device *govdev = NULL;
	struct sysfs_attribute *govattr = NULL;

	/* For each CPU */
	govdev = open_governor_device(cpu, governor);
	if (govdev == NULL) {
		return;
	}
	if (is_percentage) {
		long int min, max;
		long long int abs_value;
		char min_name[SYSFS_NAME_LEN];
		char max_name[SYSFS_NAME_LEN];

		/* Read minimum allowed value */
		snprintf(min_name, SYSFS_NAME_LEN, "%s_min", parameter);
		min = get_parameter(govdev, min_name);
		if (min < 0) {
			sysfs_close_device(govdev);
			clog(LOG_WARNING, "warning: minimum value for %s could not be read: ignored.\n", parameter);
			return;
		}
		clog(LOG_DEBUG, "minimum value for %s: %ld\n", parameter, min);

		/* Read maximum allowed value */
		snprintf(max_name, SYSFS_NAME_LEN, "%s_max", parameter);
		max = get_parameter(govdev, max_name);
		if (max < 0) {
			sysfs_close_device(govdev);
			clog(LOG_WARNING, "warning: maximum value for %s could not be read: ignored.\n", parameter);
			return;
		}
		clog(LOG_DEBUG, "maximum value for %s: %ld\n", parameter, max);

		/* Convert percentage to absolute value in a string */
		abs_value = (long long int)value * (max - min) / 100 + min;
		snprintf(value_str, BUFLEN_PARAMETER_VALUE, "%lu", (long int)abs_value);
		clog(LOG_DEBUG, "converted percentage %ld to absolute value: %s\n", value, value_str);
	} else {
		/* Convert value to string */
		snprintf(value_str, BUFLEN_PARAMETER_VALUE, "%lu", value);
	}

	/* Get sysfs attribute for the parameter */
	govattr = sysfs_get_device_attr(govdev, parameter);

	/* Kernel compatibility issue:
	 * The parameter name "ignore_nice" changed to "ignore_nice_load" in kernel >= 2.6.16.
	 * We are accepting both names silently, regardless the kernel version by trying to get
	 * a sysfs device for the other name when the specified name doesn't exist in sysfs.
	 */
	if (govattr == NULL) {
		if (strcmp(parameter, "ignore_nice") == 0) {
			govattr = sysfs_get_device_attr(govdev, "ignore_nice_load");
		} else if (strcmp(parameter, "ignore_nice_load") == 0) {
			govattr = sysfs_get_device_attr(govdev, "ignore_nice");
		}
	}
	if (govattr == NULL) {
		sysfs_close_device(govdev);
		clog(LOG_WARNING, "warning: attribute %s not found in sysfs.\n", parameter);
		return;
	}

	/* Write new value to sysfs' parameter attribute */
	if (sysfs_write_attribute(govattr, value_str, strlen(value_str)) < 0) {
		clog(LOG_ERR, "ERROR: could not set parameter %s to %s for %s governor on cpu%u: %s\n",
				parameter, value_str, governor, cpu, strerror(errno));
	}
	clog(LOG_DEBUG, "parameter %s set to %s for %s governor on cpu%u\n",
			parameter, value_str, governor, cpu);

	/* Close governor device. This will free all structures allocated by libsysfs */
	sysfs_close_device(govdev);
}


/* Common keyword parse function.
 */
static int parameter_parse(const char *keyword, const char *value, void **obj,
		int accept_percentage, int accept_suffix)
{
	int i, j;
	long int val;
	int suffix_parsed;
	int multiplier;
	char suffix;
	char strvalue[BUFLEN_PARAMETER_VALUE];
	char *convcheck;
	struct gov_parameter *ret;

	clog(LOG_DEBUG, "called: %s = %s\n", keyword, value);
	ret = malloc(sizeof(struct gov_parameter));
	if (ret == NULL) {
		clog(LOG_ERR, "ERROR: not enough memory for a governor parameter\n");
		return -1;
	}

	/* Check for empty value */
	if (value[0] == '\0') {
		clog(LOG_WARNING, "governor parameter %s has no value\n", keyword);
		free(ret);
		return -1;
	}

	/* Copy keyword to the object. */
	snprintf(ret->parameter_name, SYSFS_NAME_LEN, "%s", keyword);

	/* Make writable copy of the value string */
	snprintf(strvalue, BUFLEN_PARAMETER_VALUE, "%s", value);

	/* Parse suffix */
	multiplier = 1;
	ret->is_percentage = FALSE;
	suffix = '\0';

	/* Find where the number ends */
	i = strspn(strvalue, "0123456789");
	if (i == 0) {
		clog(LOG_WARNING, "invalid number: %s=%s\n", keyword, value);
		free(ret);
		return -1;
	}
	j = i;

	/* Skip whitespace */
	while (strvalue[i] != '\0' && (strvalue[i] == ' ' || strvalue[i] == '\t')) {
		++i;
	}

	if (strvalue[i] != '\0') { /* some suffix was specified */
		if (!accept_percentage && !accept_suffix) {
			clog(LOG_WARNING, "warning: no suffix allowed for %s\n", keyword);
			free(ret);
			return -1;
		}
		suffix = strvalue[i];
		strvalue[j] = '\0';    /* cut suffix from the string to get clean number */
		if (strvalue[++i] != '\0') {
			free(ret);
			clog(LOG_WARNING, "warning: only one-char suffix allowed: %s=%s\n", keyword, value);
			return -1;
		}
		suffix_parsed = FALSE;
		if (accept_percentage) {
			if (suffix == '%') {
				ret->is_percentage = TRUE;
				suffix_parsed = TRUE;
			}
		}
		if (accept_suffix) {
			switch (suffix) {
				case 'u':
					multiplier = 1;
					suffix_parsed = TRUE;
					break;
				case 'm':
					multiplier = 1000;
					suffix_parsed = TRUE;
					break;
				case 's':
					multiplier = 1000000;
					suffix_parsed = TRUE;
			}
		}
		if (!suffix_parsed) {
			clog(LOG_WARNING, "warning: invalid suffix: %s=%s\n", keyword, value);
			free(ret);
			return -1;
		}
	}

	/* Convert string to long int and check if it's a valid number */
	val = strtol(strvalue, &convcheck, 10);
	if (val == LONG_MAX) {
		clog(LOG_WARNING, "governor parameter out of range: %s = %s\n", keyword, strvalue);
		free(ret);
		return -1;
	}
	if (*convcheck != '\0') {
		clog(LOG_WARNING, "governor parameter value invalid: %s = %s\n", keyword, strvalue);
		free(ret);
		return -1;
	}
	if (ret->is_percentage && val > 100) {
		clog(LOG_WARNING, "percentage greater than 100%% not allowed: %s=%s\n", keyword, strvalue);
		free(ret);
		return -1;
	}

   	/* Finish the object */
	val *= multiplier;
	ret->parameter_value = val;
	*obj = ret;
	return 0;
}


/* Keyword parse functions.
 * They just call a common parse function with the parameter name as an extra argument.
 */
static int ignore_nice_parse(const char *value, void **obj)
{
	return parameter_parse("ignore_nice", value, obj, FALSE, FALSE);
}

static int ignore_nice_load_parse(const char *value, void **obj)
{
	return parameter_parse("ignore_nice_load", value, obj, FALSE, FALSE);
}

static int sampling_rate_parse(const char *value, void **obj)
{
	return parameter_parse("sampling_rate", value, obj, TRUE, TRUE);
}

static int sampling_down_factor_parse(const char *value, void **obj)
{
	return parameter_parse("sampling_down_factor", value, obj, FALSE, FALSE);
}

static int up_threshold_parse(const char *value, void **obj)
{
	return parameter_parse("up_threshold", value, obj, FALSE, FALSE);
}

static int down_threshold_parse(const char *value, void **obj)
{
	return parameter_parse("down_threshold", value, obj, FALSE, FALSE);
}

static int freq_step_parse(const char *value, void **obj)
{
	return parameter_parse("freq_step", value, obj, FALSE, FALSE);
}


/* Event handler.
 * Called when cpufreqd just changed the profile.
 */
static void gov_parameter_post_change(void *obj, const struct cpufreq_policy *not_needed,
		const struct cpufreq_policy *new_policy, const unsigned int cpu)
{
	int i;
	const char *parameter;
	long int value;
	int is_percentage;

	/* Just preventing compiler warning here */
	not_needed = not_needed;

	/* Convenience variables */
	parameter = ((struct gov_parameter *) obj)->parameter_name;
	value = ((struct gov_parameter *) obj)->parameter_value;
	is_percentage = ((struct gov_parameter *) obj)->is_percentage;

	/* Check if we support this governor */
	i = 0;
	while (supported_governors[i] != NULL) {
		if (strcmp(supported_governors[i], new_policy->governor) == 0) {
			clog(LOG_INFO, "setting governor parameter %s = %ld%c\n",
					parameter, value, is_percentage ? '%' : ' ');
			set_parameter(cpu, new_policy->governor, parameter, value, is_percentage);
			return;
		}
		++i;
	}
	clog(LOG_INFO, "governor parameter %s specified, but %s is not supported by this plugin\n",
			parameter, new_policy->governor);
}


/* Initializes this plugin.
 */
static int gov_parameter_init(void)
{
	char sysfsmountpath[SYSFS_PATH_MAX];
	struct cpufreqd_info const *info;

	clog(LOG_DEBUG, "called\n");

	/* Get number of CPU's */
	info = get_cpufreqd_info();
	number_of_cpus = info->cpus;

	/* Construct sysfs base directory string for cpu device */
	if (sysfs_get_mnt_path(sysfsmountpath, SYSFS_PATH_MAX) < 0) {
		clog(LOG_ERR, "ERROR in call to libsysfs. Should not happen. Must be a bug..\n");
		return -1;
	}
	clog(LOG_DEBUG, "sysfs mount path = %s\n", sysfsmountpath);
	snprintf(syspath_cpu, SYSFS_PATH_MAX, "%s/%s", sysfsmountpath, SYS_CPU_DIR);
	clog(LOG_DEBUG, "sysfs path cpu device  = %s\n", syspath_cpu);
	return 0;
}


/* Called when this plugin is unloaded.
 */
static int gov_parameter_exit(void)
{
	clog(LOG_DEBUG, "called\n");
	return 0;
}


static struct cpufreqd_keyword kw[] = {
	{
		.word = "ignore_nice",
		.parse = &ignore_nice_parse,
		.profile_post_change = &gov_parameter_post_change
	},

	{
		.word = "ignore_nice_load",  /* New name for 'ignore_nice' since kernel version 2.6.16 */
		.parse = &ignore_nice_load_parse,
		.profile_post_change = &gov_parameter_post_change
	},

	{
		.word = "sampling_rate",
		.parse = &sampling_rate_parse,
		.profile_post_change = &gov_parameter_post_change
	},

	{
		.word = "sampling_down_factor",
		.parse = &sampling_down_factor_parse,
		.profile_post_change = &gov_parameter_post_change
	},

	{
		.word = "up_threshold",
		.parse = &up_threshold_parse,
		.profile_post_change = &gov_parameter_post_change
	},

	{
		.word = "down_threshold",
		.parse = &down_threshold_parse,
		.profile_post_change = &gov_parameter_post_change
	},

	{
		.word = "freq_step",
		.parse = &freq_step_parse,
		.profile_post_change = &gov_parameter_post_change
	},

	{.word = NULL, .parse = NULL, .evaluate = NULL, .free = NULL}
};


static struct cpufreqd_plugin governor_parameter = {
	.plugin_name = "governor_parameters",
	.keywords = kw,
	.plugin_init = &gov_parameter_init,
	.plugin_exit = &gov_parameter_exit,
};

struct cpufreqd_plugin *create_plugin(void)
{
	return &governor_parameter;
}
