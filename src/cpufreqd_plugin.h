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

#ifndef __CPUFREQD_PLUGIN_H__
#define __CPUFREQD_PLUGIN_H__ 1

#include <cpufreq.h>
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include "cpufreqd.h"
#include "cpufreqd_remote.h"
#include "config_parser.h"
#include "cpufreqd_log.h"

#define FALSE	0
#define TRUE	1
#define DONT_MATCH  0
#define MATCH       1

#define KERNEL_VERSION_26	1
#define KERNEL_VERSION_24	2

#define wake_cpufreqd()	kill(getpid(), SIGALRM)

/*
 *  Shared struct containing useful global informations
 *  probed from the core cpufreqd at noostrap time.
 */
struct cpufreq_limits {
	unsigned long min;
	unsigned long max;
};

struct cpufreq_sys_info {
	struct cpufreq_available_governors *governors;
	struct cpufreq_available_frequencies *frequencies;
	struct cpufreq_affected_cpus *affected_cpus;
};

struct cpufreqd_info {
	unsigned int kernel_version;
	unsigned int cpus;
	int cpufreqd_mode; /* operation mode (manual / dynamic) */
	struct cpufreq_limits *limits;
	struct cpufreq_sys_info *sys_info;
	struct profile **current_profiles;
	/* last update, IOW las call to cpufreqd_loop (see main.h)*/
	struct timeval timestamp;
};
struct cpufreqd_info * get_cpufreqd_info (void);

struct cpufreqd_plugin;

struct rule;

/*
 *  A cpufreqd keyword consists of the proper word to match at the
 *  beginning of the Rule line. The struct consists of two other function
 *  pointers, one will provide the function to be called if the keyword
 *  being considered matches with *word, the sencond will provide a function to
 *  be called during the main loop that will evaluate the current
 *  system state (as read by the same plugin) against it.
 *
 *  At least one out of evaluate, profile_pre_change, profile_post_change,
 *  rule_pre_change, rule_post_change MUST be defined for a single keyword.
 *  Otherwise it doesn't make sense...
 */
struct cpufreqd_keyword {

	/* The word that is managed.
	 *
	 * Can't be NULL.
	 */
	const char *word;

	/* function pointer to the keyword parser. line is a config file _value_
	 * (as in key=value) and obj must be assigned a structure that will be
	 * used by the evaulate functioned
	 *
	 * Must be non-NULL.
	 */
	int (*parse) (const char *line, void **obj);

	/* function pointer to the evaluator. obj is the structure provided by
	 * the parse function and that represent the system state that must eventually
	 * be matched. If the system state matches the function must return MATCH (1)
	 * otherwise DONT_MATCH (0).
	 *
	 * Can be NULL.
	 */
	int (*evaluate) (const void *obj);

	/* function pointer to the profile_pre_change event. obj is the structure
	 * previously provided by the parse function, old and new are the old
	 * and new policy pointer respctively.
	 * The function is called prior to the call to set_policy() when a new
	 * Profile is going to be set.
	 *
	 * Can be NULL.
	 */
	void (*profile_pre_change) (void *obj, const struct cpufreq_policy *old,
			const struct cpufreq_policy *new, const unsigned int cpu);

	/* function pointer to the profile_post_change event. The same as
	 * profile_pre_change applies except for the fact that everything is
	 * referred tto _after_ set_policy() has been called.
	 *
	 * Can be NULL.
	 */
	void (*profile_post_change) (void *obj, const struct cpufreq_policy *old,
			const struct cpufreq_policy *new, const unsigned int cpu);

	/* function pointer to the rule_pre_change event. obj is the structure
	 * previously provided by the parse function, old and new are the old
	 * and new policy pointer respctively.
	 * The function is called prior to the call to set_policy() when a new Rule
	 * applies the current system state. Note however that set_policy() will not
	 * be called if the Profile doesn't change (you can tell that by comparing the
	 * old and new policy pointers, if they are the same then set_policy() won't
	 * be called).
	 *
	 * Can be NULL.
	 */
	void (*rule_pre_change) (void *obj, const struct rule *old,
			const struct rule *new);

	/* function pointer to the rule_post_change event. The same as
	 * rule_pre_change applies except for the fact that everything is
	 * referred tto _after_ set_policy() has been called.
	 *
	 * Can be NULL.
	 */
	void (*rule_post_change) (void *obj, const struct rule *old,
			const struct rule *new);


	/* Allows the owner to define a specific function to be called when freeing
	 * malloced during the 'parse' call. Not required, if missing a libc call to
	 * 'free' is performed with the same obj argument.
	 *
	 * Can be NULL.
	 */
	void (*free) (void *obj);
};

/*
 *  A cpufreqd plugin is a collection of functions and settings able to
 *  monitor some kind of system resource/state and tell if the present
 *  state is conformant to the one configured.
 *  cpufreqd plugins must be decalared static to avoid symbol clashes.
 */
struct cpufreqd_plugin {

	/****************************************
	 *  PLUGIN IDENTIFICATION AND SETTINGS  *
	 ****************************************/
	/* plugin name, must be unique (see README.plugins?) */
	const char *plugin_name;

	/* array of keywords handled by this plugin */
	struct cpufreqd_keyword *keywords;

	/************************
	 *  FUNCTION POINTERS   *
	 ************************/
	/* Plugin intialization */
	int (*plugin_init) (void);

	/* Plugin cleanup */
	int (*plugin_exit) (void);

	/* Update plugin data */
	int (*plugin_update) (void);

	/* Plugin configuration */
	int (*plugin_conf) (const char *key, const char *value);

	/* Plugin post configuration
	 * This will be called after the configuration of the plugin
	 * is performed, that is IFF a section with the plugin name
	 * as been found and parsed succesfully.
	 */
	int (*plugin_post_conf) (void);

	/* Allow plugins to make some data available to others.
	 * This data can be retrieved using
	 * void *get_plugin_data(const char *name)
	 * exported by the core cpufreqd.
	 */
	void *data;
};

/*
 *  A cpufreqd plugin MUST define the following function to provide the
 *  core cpufreqd with the correct struct cpufreqd_plugin structure
 */
struct cpufreqd_plugin *create_plugin(void);

#if 0
/*  This is a hack to enable plugin cooperation. A plugin can read
 *  some status data from another one.
 *  Tha name "core" is reserved for cpufreqd core data (current
 *  policy, current cpu speed, ...)
 */
extern void *get_plugin_data(const char *name);
#endif

#endif
