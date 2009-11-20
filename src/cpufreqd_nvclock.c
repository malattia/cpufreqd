/*
 *  Copyright (C) 2005-2006  Mattia Dongili <malattia@gmail.com>
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
#include "nvclock.h"

/* avoid being called for _every_ cpu switching freq,
 * just run our stuff on the last one
 */
int nvcore_profile_calls;
int nvmem_profile_calls;

struct nvclock_elem {
	int card;
	unsigned int value; /* used for gpu and mem clock */
};

static void nvcore_change(struct nvclock_elem *nv) {

	if (nv->card < nvclock.num_cards) {
		clog(LOG_INFO, "Setting nv_core for card %i to (%u)\n", nv->card, nv->value);
		set_card(nv->card);
		if(nv_card.supported & GPU_OVERCLOCKING_SUPPORTED)
			nv_card.set_gpu_speed(nv->value);
		unset_card();
	}
}

static void nvmem_change(struct nvclock_elem *nv) {

	if (nv->card < nvclock.num_cards) {
		clog(LOG_INFO, "Setting nv_mem for card %i to (%u)\n", nv->card, nv->value);
		set_card(nv->card);
		if(nv_card.supported & MEM_OVERCLOCKING_SUPPORTED)
			nv_card.set_memory_speed(nv->value);
		unset_card();
	}
}

/* pre change events */
static void nvcore_profile_pre_change(void __UNUSED__ *obj,
		const struct cpufreq_policy __UNUSED__ *old,
		const struct cpufreq_policy __UNUSED__ *new,
		const unsigned int __UNUSED__ cpu) {
	nvcore_profile_calls++;
}

static void nvmem_profile_pre_change(void __UNUSED__ *obj,
		const struct cpufreq_policy __UNUSED__ *old,
		const struct cpufreq_policy __UNUSED__ *new,
		const unsigned int __UNUSED__ cpu) {
	nvmem_profile_calls++;
}

/* post change events */
static void nvcore_rule_post_change(void *obj,
		const struct rule __UNUSED__ *old,
		const struct rule __UNUSED__ *new) {
	nvcore_change(obj);
}

static void nvcore_profile_post_change(void *obj,
		const struct cpufreq_policy __UNUSED__ *old,
		const struct cpufreq_policy __UNUSED__ *new,
		const unsigned int __UNUSED__ cpu) {
	nvcore_profile_calls--;
	if (nvcore_profile_calls == 0)
		nvcore_change(obj);
}

static void nvmem_rule_post_change(void *obj,
		const struct rule __UNUSED__ *old,
		const struct rule __UNUSED__ *new) {
	nvmem_change(obj);
}

static void nvmem_profile_post_change(void *obj,
		const struct cpufreq_policy __UNUSED__ *old,
		const struct cpufreq_policy __UNUSED__ *new,
		const unsigned int __UNUSED__ cpu) {
	nvmem_profile_calls--;
	if (nvmem_profile_calls == 0)
		nvmem_change(obj);
}

static int nvclock_init(void) {
	return !init_nvclock();
}

/*
 * Parses entries of the form %d:%d (card:value)
 */
static int nvclock_parse(const char *ev, void **obj) {
	struct nvclock_elem *ret = calloc(1, sizeof(struct nvclock_elem));
	if (ret == NULL) {
		clog(LOG_ERR, "%s: couldn't make enough room for nv_elem (%s)\n",
				strerror(errno));
		return -1;
	}

	clog(LOG_DEBUG, "called with %s\n", ev);

	/* try to parse the %d:%d format */
	if (sscanf(ev, "%i:%u", &(ret->card), &(ret->value)) == 2) {
		clog(LOG_INFO, "parsed %i:%u\n", ret->card, ret->value);
		if (ret->card < 0  ||  ret->card >= MAX_CARDS) {
			clog(LOG_ERR,"Only %i cards supported!\n", MAX_CARDS);
			free(ret);
			return -1;
		}
	} else {
		free(ret);
		return -1;
	}

	*obj = ret;
	return 0;
}

static struct cpufreqd_keyword kw[] = {
	{	.word = "nv_core",
		.parse = &nvclock_parse,
		.profile_pre_change = &nvcore_profile_pre_change,
		.profile_post_change = &nvcore_profile_post_change,
		.rule_post_change = &nvcore_rule_post_change
	},
	{	.word = "nv_mem",
		.parse = &nvclock_parse,
		.profile_pre_change = &nvmem_profile_pre_change,
		.profile_post_change = &nvmem_profile_post_change,
		.rule_post_change = &nvmem_rule_post_change
	},
	{ .word = NULL }
};

static struct cpufreqd_plugin nvclock_plugin = {
	.plugin_name	= "nvclock",
	.keywords	= kw,
	.plugin_init	= nvclock_init,
	.plugin_exit	= NULL,
	.plugin_update	= NULL,
	.plugin_conf	= NULL,
	.plugin_post_conf= NULL,
};

struct cpufreqd_plugin *create_plugin (void) {
	return &nvclock_plugin;
}

