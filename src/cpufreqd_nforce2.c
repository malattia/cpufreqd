/*
 *  Copyright (C) 2005-2006  Mattia Dongili <malattia@linux.it>
 *                2005       Prakash Punnoor <prakash@punnoor.de>
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
#include "cpufreqd_plugin.h"

static char vcore_path[MAX_PATH_LEN];
static int vcore_default;

static const int min_vcore = 1200;
static const int max_vcore = 1850;

static int limit_vcore(int read_vcore)
{
	if (read_vcore >= min_vcore  &&  read_vcore <= max_vcore) {
		return read_vcore;
	} else {
		int limited_vcore = (read_vcore < min_vcore) ? min_vcore : max_vcore;
		clog(LOG_WARNING, "Desired Vcore %i out of range, setting to %i\n",
				read_vcore, limited_vcore);

		return limited_vcore;
	}
}

static inline void set_vcore(int vcore)
{
	if (vcore) {
		FILE *fp = fopen(vcore_path, "w");
		if (!fp) {
			clog(LOG_ERR, "Could not write Vcore %i to vcore_path (%s)!\n",
					vcore, vcore_path);
		} else {
			fprintf(fp, "%i", vcore);
			fclose(fp);
			clog(LOG_DEBUG, "Vcore %i set\n", vcore);
		}
	}
}

static int nforce2_post_conf(void) {
	struct stat sb;

	if (!vcore_path[0]) {
		clog(LOG_INFO, "Unconfigured, exiting.\n");
		return -1;
	}
	/* check vcore_path */
	if (stat(vcore_path, &sb) != 0) {
		clog(LOG_INFO, "Unable to find %s.\n", vcore_path);
		return -1;
	}
	return 0;
}

static int nforce2_conf(const char *key, const char *value) {

	if (strncmp(key, "vcore_path", 10) == 0 && value !=NULL) {
		snprintf(vcore_path, MAX_PATH_LEN, "%s", value);
		clog(LOG_DEBUG, "vcore_path is %s.\n", vcore_path);
		return 0;
	}
	else if (strncmp(key, "vcore_default", 13) == 0 && value !=NULL) {
		vcore_default = limit_vcore(atoi(value));
		clog(LOG_DEBUG, "vcore_default is %d.\n", vcore_default);
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
		clog(LOG_ERR, "couldn't make enough room for vcore (%s)\n",
				strerror(errno));
		return -1;
	}

	clog(LOG_DEBUG, "called with %s\n", ev);

	/* try to parse the %d format */
	if (sscanf(ev, "%d", ret) == 1) {
		clog(LOG_INFO, "parsed %d\n", *ret);
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

/* avoid being called for _every_ cpu switching freq,
 * just run our stuff on the first one on -pre events,
 * on the last one on -post events
 */
int vcore_profile_calls;

static void vcore_pre_change(void *obj,
		const struct cpufreq_policy __UNUSED__ *old,
		const struct cpufreq_policy *new,
		const unsigned int __UNUSED__ cpu) {

	if (vcore_profile_calls == 0) {
		cur_freq = cpufreq_get(0);

		if (cur_freq <= new->max) {
			int vcore = *(int *)obj;

			clog(LOG_INFO, "Setting Vcore to (%d)\n", vcore);
			set_vcore(vcore);
		}
	}
	vcore_profile_calls++;
}

static void vcore_post_change(void *obj,
		const struct cpufreq_policy __UNUSED__ *old,
		const struct cpufreq_policy *new,
		const unsigned int __UNUSED__ cpu) {

	vcore_profile_calls--;
	if (vcore_profile_calls == 0) {
		if (cur_freq > new->max) {
			int vcore = *(int *)obj;

			clog(LOG_INFO, "Setting Vcore to (%d)\n", vcore);
			set_vcore(vcore);
		}
	}
}

static int nforce2_update(void) {
	vcore_profile_calls = 0;
	return 0;
}

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
	.plugin_exit	= &nforce2_exit,
	.plugin_update	= &nforce2_update,
	.plugin_conf	= &nforce2_conf,
	.plugin_post_conf= &nforce2_post_conf,
};

struct cpufreqd_plugin *create_plugin (void) {
	return &nforce2;
}

