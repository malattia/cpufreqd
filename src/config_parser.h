/*
 *  Copyright (C) 2002-2005  Mattia Dongili <malattia@gmail.com>
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
#include "list.h"

struct rule_en {
  void *obj;
  int (*eval) (const void *ev);
};

struct profile {
  char name[MAX_STRING_LEN];
  unsigned int cpu;
  struct cpufreq_policy policy;
#if 0
  char policy_name[255];
  long int min_freq;
  long int max_freq;
  unsigned char sep;
#endif
};

struct rule {
  char name[MAX_STRING_LEN];
  char profile_name[MAX_STRING_LEN];
  struct LIST entries; /* list of struct rule_en */
  struct profile *prof;
  unsigned int score;
};

struct cpufreq_sys_info {
  struct cpufreq_available_governors *governors;
  struct cpufreq_available_frequencies *frequencies;
  struct cpufreq_affected_cpus *affected_cpus;
};

struct cpufreq_limits {
  unsigned long min;
  unsigned long max;
};

struct cpufreqd_conf {
  
  char config_file[MAX_PATH_LEN];
  char pidfile[MAX_PATH_LEN];
  int log_level;
  unsigned int cpu_num;
  unsigned int acpi_workaround;
  unsigned int poll_interval;
  unsigned int has_sysfs;
  unsigned int no_daemon;
  unsigned int log_level_overridden;
  unsigned int print_help;
  unsigned int print_version;
  unsigned long cpu_min_freq;
  unsigned long cpu_max_freq;
  
  struct cpufreq_limits *limits;
  struct cpufreq_sys_info *sys_info;

  struct LIST rules; /* list of configured struct rule */
  struct LIST profiles; /* list of configured struct profile */

  struct LIST plugins; /* list of configured plugins */

};


/* Configuration functions */
int parse_config_profile (FILE *config, struct profile *p);
int parse_config_rule    (FILE *config, struct rule *r);
int parse_config_general (FILE *config);
char *read_clean_line    (FILE *fp, char *buf, int n);

#endif /* _CONFIG_PARSER_H */
