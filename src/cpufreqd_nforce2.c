/*
 *  Copyright (C) 2005  Mattia Dongili <malattia@gmail.com>
 *                      Prakash Punnoor <prakash@punnoor.de>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cpufreqd_log.h"
#include "cpufreqd_plugin.h"

static char vcore_path[MAX_PATH_LEN];
static int vcore_default;

static int vcore_parse(const char *ev, void **obj);
static void vcore_pre_change(void *obj, const struct cpufreq_policy *old,
		const struct cpufreq_policy *new);
static void vcore_post_change(void *obj, const struct cpufreq_policy *old,
		const struct cpufreq_policy *new);

static int nforce2_conf(const char *key, const char *value);
static int nforce2_post_conf(void);
static int nforce2_exit(void);

static struct cpufreqd_keyword kw[] = {
	{	.word = "vcore",
		.parse = &vcore_parse,
		.profile_pre_change = &vcore_pre_change,
		.profile_post_change = &vcore_post_change
	},
	{ .word = NULL }
};

static struct cpufreqd_plugin nforce2 = {
	.plugin_name	= "nforce2_atxp1",
	.keywords	= kw,
	.plugin_init	= NULL,
	.plugin_exit	= nforce2_exit,
	.plugin_update	= NULL,
	.plugin_conf	= nforce2_conf,
	.plugin_post_conf= nforce2_post_conf,
};

static const int min_vcore = 1200;
static const int max_vcore = 1850;

static int limit_vcore(int read_vcore)
{
	if (read_vcore >= min_vcore  &&  read_vcore <= max_vcore) {
		return read_vcore;
	} else {
		int limited_vcore = (read_vcore < min_vcore) ? min_vcore : max_vcore;
		cpufreqd_log(LOG_WARNING, "Desired Vcore %i out of range, setting to %i\n",
				read_vcore, limited_vcore);
		
		return limited_vcore;
	}
}

static inline void set_vcore(int vcore)
{
	if (vcore) {
		FILE *fp = fopen(vcore_path, "w");
		if (!fp) {
			cpufreqd_log(LOG_ERR, "Could not write Vcore %i to vcore_path (%s)!\n",
					vcore, vcore_path);
		} else {
			fprintf(fp, "%i", vcore);
			fclose(fp);
			cpufreqd_log(LOG_DEBUG, "Vcore %i set\n", vcore);
		}
	}
}

static int nforce2_post_conf(void) {
	struct stat sb;

	/* check vcore_path */
	if (stat(vcore_path, &sb) != 0) {
		cpufreqd_log(LOG_CRIT, "%s: Unable to find %s\n",
				__func__, vcore_path);
		return -1;
	}
	return 0;
}

static int nforce2_conf(const char *key, const char *value) {

	if (strncmp(key, "vcore_path", 10) == 0 && value !=NULL) {
		snprintf(vcore_path, MAX_PATH_LEN, "%s", value);
		cpufreqd_log(LOG_DEBUG, "%s: vcore_path is %s\n",
				__func__, vcore_path);
		return 0;
	}
	else if (strncmp(key, "vcore_default", 13) == 0 && value !=NULL) {
		vcore_default = limit_vcore(atoi(value));
		cpufreqd_log(LOG_DEBUG, "%s: vcore_default is %d\n",
				__func__, vcore_default);
		return 0;
	}
	return -1;
}

static int nforce2_exit(void) {
	
	set_vcore(vcore_default);
	return 0;
}

/*
 * Parses entries of the form %d (millivolt)
 */
static int vcore_parse(const char *ev, void **obj) {
	int *ret = calloc(1, sizeof(int));
	if (ret == NULL) {
		cpufreqd_log(LOG_ERR,
				"%s: couldn't make enough room for vcore (%s)\n",
				__func__, strerror(errno));
		return -1;
	}

	cpufreqd_log(LOG_DEBUG, "%s - %s: called with %s\n",
				nforce2.plugin_name, __func__, ev);

	/* try to parse the %d format */
	if (sscanf(ev, "%d", ret) == 1) {
		cpufreqd_log(LOG_INFO, "%s - %s: parsed %d\n",
				nforce2.plugin_name, __func__, *ret);
		*ret = limit_vcore(*ret);
	} else {
		free(ret);
		return -1;
	}

	*obj = ret;
	return 0;
}

/*
 * Applies vcore settings
 */

static unsigned int cur_freq = 0;

static void vcore_pre_change(void *obj, const struct cpufreq_policy *old,
		const struct cpufreq_policy *new) {
	
	cur_freq = cpufreq_get(0);
	
	if (cur_freq <= new->max) {
		int vcore = *(int *)obj;
		
		cpufreqd_log(LOG_INFO, "%s: Setting Vcore to (%d)\n", __func__, vcore);
		set_vcore(vcore);
	}
}

static void vcore_post_change(void *obj, const struct cpufreq_policy *old,
		const struct cpufreq_policy *new) {
	
	if (cur_freq > new->max) {
		int vcore = *(int *)obj;
		
		cpufreqd_log(LOG_INFO, "%s: Setting Vcore to (%d)\n", __func__, vcore);
		set_vcore(vcore);
	}
}

struct cpufreqd_plugin *create_plugin (void) {
	return &nforce2;
}

