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

#ifndef _CONFIG_PARSER_H
#define _CONFIG_PARSER_H 1

#include <stdio.h>
#include <cpufreq.h>
#include "cpufreqd.h"
#include "cpufreqd_plugin.h"
#include "list.h"

struct directive {
	void *obj;
	struct cpufreqd_keyword *keyword;
	struct cpufreqd_plugin *plugin;
};

struct profile {
	char name[MAX_STRING_LEN];
	unsigned int cpu;
	struct cpufreq_policy policy;
	struct LIST directives; /* list of struct directive */
	unsigned int directives_count;
};

struct rule {
	char name[MAX_STRING_LEN];
	char profile_name[MAX_STRING_LEN]; /* this is a list actually, eg: "CPU0:prof0;CPU1:prof1" */
	struct LIST directives; /* list of struct directive */
	struct profile **prof; /* profiles per CPU */
	unsigned long assigned_cpus; /* bit map holding which cpus have been assigned a Profile for this rule */
	unsigned int score;
	unsigned int directives_count;
};

struct cpufreqd_conf {

	char config_file[MAX_PATH_LEN];
	char pidfile[MAX_PATH_LEN];
	int log_level;
	unsigned int enable_remote;
	gid_t remote_gid;
	unsigned int double_check;
	struct timeval poll_intv;
	unsigned int has_sysfs;
	unsigned int no_daemon;
	unsigned int log_level_overridden;
	unsigned int print_help;
	unsigned int print_version;

	struct LIST rules; /* list of configured struct rule */
	struct LIST profiles; /* list of configured struct profile */
	struct LIST plugins; /* list of configured plugins struct o_plugin */

};

int	init_configuration	(struct cpufreqd_conf *config);
void	free_configuration	(struct cpufreqd_conf *config);

#endif /* _CONFIG_PARSER_H */
