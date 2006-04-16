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
 * Version 0.2
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <libsysfs.h>

#include "cpufreqd_plugin.h"

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#define MAXLEN_PARAMETER_VALUE 24
#define SYS_CPU_ROOT "system/cpu"

static unsigned int number_of_cpus = 1;

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


/* Gets sysfs attribute for a governor parameter
 * Return pointer to the struct if successfull, NULL on error.
 */
static struct sysfs_attribute *get_governor_attribute(struct sysfs_root_device *cpufreqdev,
													  const char *governor, 
													  const char *parameter)
{
	struct sysfs_device *govdev = NULL;
	struct sysfs_attribute *govattr = NULL;
    struct dlist *devlist = NULL;
	
	/* cpufreqdev should have at least one child: the governor directory containing the parameter attributes */
    devlist = sysfs_get_root_devices(cpufreqdev);
    if (devlist == NULL) {
		clog(LOG_ERR, "ERROR: no governor found in sysfs.\n");
		return NULL;
	}
	
	/* Scan sub-devices for the governor (often only oneto scan) */
	dlist_for_each_data(devlist, govdev, struct sysfs_device) {
		if (!strcmp(govdev->name, governor)) {
			govattr = sysfs_get_device_attr(govdev, parameter);
			if (govattr == NULL) {
				clog(LOG_WARNING, "warning: %s governor does not support parameter %s.\n", governor, parameter);
				return NULL;
			}
			return govattr;
		}
	}
	clog(LOG_ERR, "ERROR: %s governor not found on sysfs\n", governor);
	return NULL;
}


/* Opens a sysfs root device for the cpufreq interface of a given CPU number.
 * Returns a pointer to the root device if successfull, NULL on error.
 */
static struct sysfs_root_device *open_cpufreq_root_device(unsigned int cpu)
{
	char cpufreqdev_path[SYSFS_PATH_MAX];
    struct sysfs_root_device *cpufreqdev = NULL;
	
	/* Construct sysfs root device path string for governor of this cpu */
	snprintf(cpufreqdev_path, SYSFS_PATH_MAX, "system/cpu/cpu%d/cpufreq", cpu);
	clog(LOG_DEBUG, "sysfs path cpufreq device = %s\n", cpufreqdev_path);
	
	/* Open sysfs governor device */
	cpufreqdev = sysfs_open_root_device(cpufreqdev_path);
	if (cpufreqdev == NULL) {
		clog(LOG_ERR, "ERROR: cpufreq interface not found in sysfs for cpu%d.\n", cpu);
		return NULL;
	}
	return cpufreqdev;
}


/* Reads the value of 'parameter' of the currently active governor on
 * sysfs root device 'cpufreqdev'.
 * Return -1 on error, or the parameter value otherwise.
 */
static long int get_parameter(struct sysfs_root_device *cpufreqdev, const char *governor,
							  const char *parameter)
{
	struct sysfs_attribute *govattr = NULL;

	govattr = get_governor_attribute(cpufreqdev, governor, parameter);
	if (govattr == NULL) {
		clog(LOG_ERR, "ERROR: sysfs attribute %s could not be read for %s governor\n", parameter, governor);
		return -1;
	}
	return atol(govattr->value);
}


/* Sets the value of 'parameter' of 'governor' to 'value'.
 */
static void set_parameter(const char *governor, const char *parameter, long int value, int is_percentage)
{
	unsigned int cpu;
	char value_str[MAXLEN_PARAMETER_VALUE];
    struct sysfs_root_device *cpufreqdev = NULL;
	struct sysfs_attribute *govattr = NULL;

	/* For each CPU */
	for (cpu = 0; cpu < number_of_cpus; ++cpu) {
		cpufreqdev = open_cpufreq_root_device(cpu);
		if (cpufreqdev == NULL) {
			return;
		}
		if (is_percentage) {
			long int min, max;
			long long int abs_value;
			char min_name[SYSFS_NAME_LEN];
			char max_name[SYSFS_NAME_LEN];

			/* Read minimum allowed value */
			snprintf(min_name, SYSFS_NAME_LEN, "%s_min", parameter);
			min = get_parameter(cpufreqdev, governor, min_name);
			clog(LOG_DEBUG, "minimal value for %s: %ld\n", parameter, min);
	
			/* Read maximum allowed value */
			snprintf(max_name, SYSFS_NAME_LEN, "%s_max", parameter);
			max = get_parameter(cpufreqdev, governor, max_name);
			clog(LOG_DEBUG, "maximal value for %s: %ld\n", parameter, max);
	
			/* Convert percentage to absolute value in a string */
			abs_value = (long long int)value * (max - min) / 100 + min;
			snprintf(value_str, MAXLEN_PARAMETER_VALUE, "%lu", (long int)abs_value);
			clog(LOG_DEBUG, "converted percentage %ld to absolute value: %s\n", value, value_str);
		} else {
			/* Convert value to string */
			snprintf(value_str, MAXLEN_PARAMETER_VALUE, "%lu", value);
		}

		/* Write new value to sysfs' parameter attribute */
		govattr = get_governor_attribute(cpufreqdev, governor, parameter);
		if (govattr == NULL) {
			return;
		}
		if (sysfs_write_attribute(govattr, value_str, strlen(value_str)) < 0) {
			clog(LOG_ERR, "ERROR: could not set parameter %s to %s for %s governor on cpu%u: %s\n",
				 parameter, value_str, governor, cpu, strerror(errno));
		}
		clog(LOG_DEBUG, "parameter %s set to %s for %s governor on cpu%u\n",
			 parameter, value_str, governor, cpu);

		/* Close root device. This will free all structures allocated by libsysfs */
		sysfs_close_root_device(cpufreqdev);
	}
}


/* Common keyword parse function.
 */
static int parameter_parse(const char *keyword, const char *value, void **obj, 
						   int accept_percentage, int accept_suffix)
{
	int i;
	long int val;
	int suffix_parsed;
	int multiplier;
    char strvalue[MAXLEN_PARAMETER_VALUE];
    char *convcheck;
    struct gov_parameter *ret;

    clog(LOG_DEBUG, "called: %s = %s\n", keyword, value);
    ret = malloc(sizeof(struct gov_parameter));
    if (ret == NULL) {
        clog(LOG_ERR, "ERROR: unable to make room for a governor parameter\n");
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
    snprintf(strvalue, MAXLEN_PARAMETER_VALUE, "%s", value);

	multiplier = 1;
	ret->is_percentage = FALSE;
	if (accept_percentage || accept_suffix) {
		suffix_parsed = 0;
		i = strlen(value) - 1;
		if (accept_percentage && strvalue[i] == '%') {
			ret->is_percentage = TRUE;
			suffix_parsed = 1;
		}
		if (accept_suffix) {
			switch (strvalue[i]) {
				case 'u':
					multiplier = 1;
					suffix_parsed = 1;
					break;
				case 'm':
					multiplier = 1000;
					suffix_parsed = 1;
					break;
				case 's':
					multiplier = 1000000;
					suffix_parsed = 1;
					break;
			}
		}

		/* Remove extra char from the string if needed */
		if (suffix_parsed) {
			do {
				--i;
			} while (i > 0 && (strvalue[i] == ' ' || strvalue[i] == '\t'));
			if (i < 0) {
				clog(LOG_WARNING, "governor parameter %s has no (percentage) value\n", keyword);
				free(ret);
				return -1;
			}
            strvalue[i + 1] = '\0';
		}
	}

	/* Convert string to long int and check if it's a valid number */
    val = strtol(strvalue, &convcheck, 10);
    if (val == LONG_MAX) {
        clog(LOG_WARNING, "governor parameter out of range %s=%s\n", keyword, strvalue);
        free(ret);
        return -1;
    }
    if (*convcheck != '\0') {
        clog(LOG_WARNING, "governor parameter value invalid %s=%s\n", keyword, strvalue);
        free(ret);
        return -1;
    }
	if (val < 0) {
        clog(LOG_WARNING, "governor parameter %s: negative numbers not allowed\n", keyword);
        free(ret);
        return -1;
    }
	if (ret->is_percentage && val > 100) {
        clog(LOG_WARNING, "governor parameter %s: percentage greater than 100%% not allowed\n", keyword);
        free(ret);
        return -1;
    }

	/* Apply multiplier from suffix if applicable */
	if (suffix_parsed) {
		val *=multiplier;
	}

    ret->parameter_value = val;
    *obj = ret;
    return 0;
}


/* Keyword parse functions. 
 * They just call a common parse function with the parameter name as an extra argument.
 */
static int ignore_nice_parse(const char *value, void **obj)
{ return parameter_parse("ignore_nice", value, obj, FALSE, FALSE); }

static int ignore_nice_load_parse(const char *value, void **obj)
{ return parameter_parse("ignore_nice_load", value, obj, FALSE, FALSE); }

static int sampling_rate_parse(const char *value, void **obj)
{ return parameter_parse("sampling_rate", value, obj, TRUE, TRUE); }

static int sampling_down_factor_parse(const char *value, void **obj)
{ return parameter_parse("sampling_down_factor", value, obj, FALSE, FALSE); }

static int up_threshold_parse(const char *value, void **obj)
{ return parameter_parse("up_threshold", value, obj, FALSE, FALSE); }

static int down_threshold_parse(const char *value, void **obj)
{ return parameter_parse("down_threshold", value, obj, FALSE, FALSE); }

static int freq_step_parse(const char *value, void **obj)
{ return parameter_parse("freq_step", value, obj, FALSE, FALSE); }


/* Event handler.
 * Called when cpufreqd just changed the profile.
 */
static void gov_parameter_post_change(void *obj, const struct cpufreq_policy *not_needed,
								  const struct cpufreq_policy *new_policy)
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
			clog(LOG_INFO, "setting governor parameter %s = %ld%c\n", parameter, value, is_percentage ? '%' : ' ');
			set_parameter(new_policy->governor, parameter, value, is_percentage);
			return;
		}
		++i;
	}
	clog(LOG_INFO, "governor parameter %s specified, but %s does not support parameters\n", 
		 parameter, new_policy->governor);
}


/* Initializes this plugin.
 */
static int gov_parameter_init(void)
{
	struct cpufreqd_info const *info;

    clog(LOG_DEBUG, "called\n");
	info = get_cpufreqd_info();
	number_of_cpus = info->cpus;
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
    {.word = "ignore_nice",
     .parse = &ignore_nice_parse,
     .profile_post_change = &gov_parameter_post_change},

    {.word = "ignore_nice_load",  /* New name for 'ignore_nice' since kernel version 2.6.16 */
     .parse = &ignore_nice_load_parse,
     .profile_post_change = &gov_parameter_post_change},

    {.word = "sampling_rate",
     .parse = &sampling_rate_parse,
     .profile_post_change = &gov_parameter_post_change},

    {.word = "sampling_down_factor",
     .parse = &sampling_down_factor_parse,
     .profile_post_change = &gov_parameter_post_change},

    {.word = "up_threshold",
     .parse = &up_threshold_parse,
     .profile_post_change = &gov_parameter_post_change},

    {.word = "down_threshold",
     .parse = &down_threshold_parse,
     .profile_post_change = &gov_parameter_post_change},

    {.word = "freq_step",
     .parse = &freq_step_parse,
     .profile_post_change = &gov_parameter_post_change},

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
