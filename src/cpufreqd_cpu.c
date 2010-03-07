/*
 *  Copyright (C) 2002-2005  Mattia Dongili <malattia@linux.it>
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpufreqd_plugin.h"

#define CPU_ANY		0xffffffff
#define CPU_ALL		0xfffffffe

/* configured cpu intervals */
struct cpu_interval {
	unsigned int cpu;
	int min;
	int max;
	float nice_scale;
	struct cpu_interval *next;
};

struct cpu_usage {
	unsigned int c_user;
	unsigned int c_idle;
	unsigned int c_nice;
	unsigned int c_sys;
	unsigned int c_time;
	unsigned int delta_time;
};

static unsigned int kernel_version;
static struct cpu_usage *cusage;
static struct cpu_usage *cusage_old;

static void free_cpu_intervals(void *obj) {
	struct cpu_interval *ci = (struct cpu_interval *) obj;
	struct cpu_interval *temp = NULL;
	while ((temp = ci) != NULL) {
		ci = ci->next;
		free(temp);
	}
}

static int cpufreqd_cpu_init(void) {
	struct cpufreqd_info *cinfo = get_cpufreqd_info();
	clog(LOG_INFO, "called\n");
	kernel_version = cinfo->kernel_version;

	/* allocate cpu_usage structures:
	 * two for each cpu available and 2 more to
	 * store the full usage (better use some more memory
	 * than aving to calculate grant totals at each evaluation)
	 *
	 * So the all_cpus struct cpu_usage is cusage[cinfo->cpus] and will
	 * contain medium values
	 */
	if ((cusage = calloc((cinfo->cpus + 1), sizeof(struct cpu_usage))) == NULL) {
		clog(LOG_ERR, "Unable to make room for cpu usage structs (%s)\n",
				strerror(errno));
		return -1;
	}
	if ((cusage_old = calloc((cinfo->cpus + 1), sizeof(struct cpu_usage))) == NULL) {
		clog(LOG_ERR, "Unable to make room for cpu usage structs (%s)\n",
				strerror(errno));
		free(cusage);
		return -1;
	}

	return 0;
}

static int cpufreqd_cpu_exit(void) {
	clog(LOG_INFO, "called\n");
	free(cusage);
	free(cusage_old);
	return 0;
}

static int cpu_parse(const char *ev, void **obj)
{
	char temp_str[512];
	char *cpu_cmd = NULL;
	char wcards[4];
	unsigned int cpu_num = 0;
	int min = 0;
	int max = 0;
	float nice_scale = 0.0f;
	struct cpu_interval *ret = NULL, **temp_cint = NULL;
	struct cpufreqd_info *cinfo = get_cpufreqd_info();

	strncpy(temp_str, ev, 512);
	temp_str[511] = '\0';
	clog(LOG_DEBUG, "cpu interval: %s\n", temp_str);

	temp_cint = &ret;
	cpu_cmd = strtok(temp_str, ";");
	do {
		/* parse string */
		wcards[0] = '\0';
		cpu_num = cinfo->cpus;
		min = 0;
		max = 0;
		nice_scale = 3.0f;

		/* parse formats */
		if ((sscanf(cpu_cmd, "%d:%d-%d,%f", &cpu_num, &min, &max, &nice_scale) == 4
					&& cpu_num < cinfo->cpus)) {
		}
		else if (sscanf(cpu_cmd, "%d:%d-%d", &cpu_num, &min, &max) == 3
				&& cpu_num < cinfo->cpus) {
			nice_scale = 3.0f;
		}
		/* ALL or ANY cpus */
		else if (sscanf(cpu_cmd, "%3[a-zA-Z]:%d-%d,%f", wcards, &min, &max, &nice_scale) == 4) {
			wcards[3] = '\0';
			if (strstr(wcards, "ALL") == wcards)
				cpu_num = CPU_ALL;
			else if (strstr(wcards, "ANY") == wcards)
				cpu_num = CPU_ANY;
			else {
				clog(LOG_ERR, "Discarded wrong cpu wildcard fo cpu_interval: %s\n", cpu_cmd);
				continue;
			}
		}
		else if (sscanf(cpu_cmd, "%3[a-zA-Z]:%d-%d", wcards, &min, &max) == 3) {
			wcards[3] = '\0';
			if (strstr(wcards, "ALL") == wcards)
				cpu_num = CPU_ALL;
			else if (strstr(wcards, "ANY") == wcards)
				cpu_num = CPU_ANY;
			else {
				clog(LOG_ERR, "Discarded wrong cpu wildcard fo cpu_interval: %s\n", cpu_cmd);
				continue;
			}
		}
		else if (sscanf(cpu_cmd, "%d-%d,%f", &min, &max, &nice_scale) == 3) {
			cpu_num = cinfo->cpus;
		}
		else if (sscanf(cpu_cmd, "%d-%d", &min, &max) == 2) {
			cpu_num = cinfo->cpus;
			nice_scale = 3.0f;
		}
		else {
			clog(LOG_ERR, "Discarded wrong format for cpu_interval: %s\n", cpu_cmd);
			continue;
		}
		clog(LOG_INFO, "read CPU:%d MIN:%d MAX:%d SCALE:%.2f\n",
				cpu_num, min, max, nice_scale);

		/* validate values */
		if (nice_scale <= 0.0f) {
			clog(LOG_WARNING, "nice_scale value out of range(%.2f), "
					"resetting to the default value(3).\n",
					nice_scale);
			nice_scale = 3.0f;
		}

		if (min > max) {
			clog(LOG_ERR, "Min higher than Max?\n");
			free_cpu_intervals(ret);
			return -1;
		}

		/* store values */
		*temp_cint = calloc(1, sizeof(struct cpu_interval));
		if (*temp_cint == NULL) {
			clog(LOG_ERR, "Unable to make room for a cpu interval (%s)\n",
					strerror(errno));
			free_cpu_intervals(ret);
			return -1;
		}
		(*temp_cint)->cpu = cpu_num;
		(*temp_cint)->min = min;
		(*temp_cint)->max = max;
		(*temp_cint)->nice_scale = nice_scale;
		temp_cint = &(*temp_cint)->next;

	} while ((cpu_cmd = strtok(NULL,";")) != NULL);

	*obj = ret;
	return 0;
}

static int calculate_cpu_usage(struct cpu_usage *cur, struct cpu_usage *old, double nice_scale) {
	unsigned long weighted_activity = cur->c_user + cur->c_nice / nice_scale + cur->c_sys;
	unsigned long weighted_activity_old = old->c_user + old->c_nice / nice_scale + old->c_sys;
	unsigned long delta_activity = weighted_activity - weighted_activity_old;

	clog(LOG_DEBUG, "CPU delta_activity=%d delta_time=%d weighted_activity=%d.\n",
			delta_activity, cur->delta_time, weighted_activity);

	if ( delta_activity > cur->delta_time || cur->delta_time <= 0)
		return 100;
	else
		return delta_activity * 100 / cur->delta_time;
}

static int cpu_evaluate(const void *s) {
	int cpu_percent = 0;
	unsigned int i = 0;
	const struct cpu_interval *c = (const struct cpu_interval *) s;
	struct cpufreqd_info *cinfo = get_cpufreqd_info();

	while (c != NULL) {

		/* special handling for CPU_ALL and CPU_ANY */
		if (c->cpu == CPU_ANY || c->cpu == CPU_ALL) {
			for (i = 0; i < cinfo->cpus; i++) {
				clog(LOG_DEBUG, "CPU%d user=%d nice=%d sys=%d\n", i,
						cusage[i].c_user, cusage[i].c_nice, cusage[i].c_sys);
				cpu_percent = calculate_cpu_usage(&cusage[i], &cusage_old[i], c->nice_scale);
				clog(LOG_DEBUG, "CPU%d %d%% - min=%d max=%d scale=%.2f (%s)\n", i, cpu_percent,
						c->min, c->max, c->nice_scale, c->cpu == CPU_ANY ? "ANY" : "ALL");
				/* if CPU_ANY and CPUi matches the return MATCH */
				if (c->cpu == CPU_ANY && cpu_percent >= c->min && cpu_percent <= c->max)
					return MATCH;
				/* if CPU_ALL and CPUi doesn't match then break out the loop */
				if (c->cpu == CPU_ALL && !(cpu_percent >= c->min && cpu_percent <= c->max))
					break;
			}
			/* if this code is reached then
			 * either CPU_ANY and none matches
			 *     or CPU_ALL and all match, where i == cinfo->cpus
			 *     or CPU_ALL and break was called
			 */
			if (c->cpu == CPU_ALL && i == cinfo->cpus)
				return MATCH;

			c = c->next;
			continue;
		}

		/* cacluate weighted activity for the requested CPU */
		clog(LOG_DEBUG, "CPU%d user=%d nice=%d sys=%d\n", c->cpu, cusage[c->cpu].c_user,
				cusage[c->cpu].c_nice, cusage[c->cpu].c_sys);
		cpu_percent = calculate_cpu_usage(&cusage[c->cpu], &cusage_old[c->cpu], c->nice_scale);
		clog(LOG_DEBUG, "CPU%d %d%% - min=%d max=%d scale=%.2f\n", c->cpu, cpu_percent,
				c->min, c->max, c->nice_scale);
		/* return MATCH if any of the intervals match as multiple
		 * entries for a cpu_interval are OR-ed
		 */
		if (cpu_percent >= c->min && cpu_percent <= c->max)
			return MATCH;

		c = c->next;
	}
	return DONT_MATCH;
}

static int get_cpu(void) {

	FILE* fp = NULL;
	char line[256];
	int f = 0;
	unsigned int cpu_num = 0, c_user = 0, c_nice = 0, c_sys = 0, i = 0;
	unsigned long int c_idle=0, c_iowait=0, c_irq=0, c_softirq=0; /* for linux 2.6 only */
	struct cpufreqd_info *cinfo = get_cpufreqd_info();
	struct cpu_usage *temp_usage = cusage_old;

	clog(LOG_DEBUG, "called\n");

	/* swap old values */
	cusage_old = cusage;
	cusage = temp_usage;

	/* read raw jiffies... */
	fp = fopen ("/proc/stat", "r");
	if (!fp) {
		clog(LOG_ERR, "/proc/stat: %s\n", strerror(errno));
		return -1;
	}
	while (i < cinfo->cpus && !feof(fp)) {
		fgets(line, 256, fp);
		if (strstr(line, "cpu ") == line) {
			f = sscanf (line,
					"cpu %u %u %u %lu %lu %lu %lu%*s\n",
					&c_user, &c_nice, &c_sys,
					&c_idle, &c_iowait, &c_irq, &c_softirq);
			if (!((f == 4 && kernel_version == KERNEL_VERSION_24)
					|| (f == 7 && kernel_version == KERNEL_VERSION_26)))
				continue;
			/* set to all_cpus */
			cpu_num = cinfo->cpus;

		} else {
			f = sscanf (line,
					"cpu%u %u %u %u %lu %lu %lu %lu%*s\n",
					&cpu_num, &c_user, &c_nice, &c_sys,
					&c_idle, &c_iowait, &c_irq, &c_softirq);

			if (!((f == 5 && kernel_version == KERNEL_VERSION_24)
					|| (f == 8 && kernel_version == KERNEL_VERSION_26)))
				continue;
			/* got a CPU stats */
			i++;
		}

		clog(LOG_INFO, "CPU%d c_user=%d c_nice=%d c_sys=%d c_idle=%d "
				"c_iowait=%d c_irq=%d c_softirq=%d.\n",
				cpu_num, c_user, c_nice, c_sys, c_idle,
				c_iowait, c_irq, c_softirq);

		/* calculate total jiffies */
		cusage[cpu_num].c_user = c_user;
		cusage[cpu_num].c_sys = c_sys + c_irq + c_softirq;
		cusage[cpu_num].c_idle = c_idle + c_iowait;
		cusage[cpu_num].c_nice = c_nice;
		cusage[cpu_num].c_time = c_user + c_nice + c_sys + c_idle;
		/* calculate delta time */
		cusage[cpu_num].delta_time =
			cusage[cpu_num].c_time - cusage_old[cpu_num].c_time;
	}
	fclose(fp);
	return 0;
}

static struct cpufreqd_keyword kw[] = {
	{ .word = "cpu_interval", .parse = &cpu_parse, .evaluate = &cpu_evaluate, .free = &free_cpu_intervals, },
	{ .word = NULL, .parse = NULL, .evaluate = NULL, .free = NULL }
};

static struct cpufreqd_plugin cpu_plugin = {
	.plugin_name      = "cpu_plugin",	/* plugin_name */
	.keywords         = kw,			/* config_keywords */
	.plugin_init      = &cpufreqd_cpu_init,	/* plugin_init */
	.plugin_exit      = &cpufreqd_cpu_exit,	/* plugin_exit */
	.plugin_update    = &get_cpu		/* plugin_update */
};

/* MUST DEFINE THIS ONE */
struct cpufreqd_plugin *create_plugin (void) {
	return &cpu_plugin;
}
