/*
 *  Copyright (C) 2005  Mattia Dongili <malattia@gmail.com>
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
#include "cpufreqd_log.h"
#include "cpufreqd_plugin.h"

static int vcore_parse(const char *ev, void **obj);
static void vcore_change(void *obj, const struct cpufreq_policy *old, 
		const struct cpufreq_policy *new);

static struct cpufreqd_keyword kw[] = {
	{	.word = "vcore", 
		.parse = &vcore_parse, 
		.profile_pre_change = vcore_change, 
		.profile_post_change = vcore_change 
	},
	/*
	{ .word = "nv_core", .parse = &nv_core_parse, .pre_change = nv_core_change, .post_change = nv_core_change },
	{ .word = "nv_mem", .parse = &nv_mem_parse, .pre_change = nv_mem_change,  .post_change = nv_mem_change },
	*/
	{ .word = NULL, .parse = NULL, .evaluate = NULL, .free = NULL }
};

static struct cpufreqd_plugin nforce2 = {
	.plugin_name      = "nforce2",	/* plugin_name */
	.keywords         = kw,		/* config_keywords */
	.plugin_init      = NULL,	/* plugin_init */
	.plugin_exit      = NULL,	/* plugin_exit */
	.plugin_update    = NULL,	/* plugin_update */
};

/*
 * Parses entries of the form %d (millivolt)
 */
static int vcore_parse(const char *ev, void **obj) {
	int *ret = calloc(1, sizeof(int));
	if (ret == NULL) {
		cpufreqd_log(LOG_ERR, 
				"%s: couldn't make enough room for battery_interval (%s)\n",
				__func__, strerror(errno));
		return -1;
	}

	cpufreqd_log(LOG_DEBUG, "%s - %s: called with %s\n",
				nforce2.plugin_name, __func__, ev);

	/* try to parse the %d:%d format */
	if (sscanf(ev, "%d", ret) == 1) {
		cpufreqd_log(LOG_INFO, "%s - %s: parsed %d\n",
				nforce2.plugin_name, __func__, *ret);

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
static void vcore_change(void *obj, const struct cpufreq_policy *old, 
		const struct cpufreq_policy *new) {
	cpufreqd_log(LOG_INFO, "%s: Doing my duties (%d)\n", __func__, *(int *)(obj));	
}

struct cpufreqd_plugin *create_plugin (void) {
	return &nforce2;
}

