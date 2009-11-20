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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "cpufreqd.h"
#include "cpufreqd_log.h"
#include "cpufreq_utils.h"

/* normalizes the user supplied frequency to a cpufreq available freq
 * ROUNDS ALWAYS UP (except if the values is over the limits)!!
 */
unsigned long normalize_frequency (struct cpufreq_limits *limits,
		struct cpufreq_available_frequencies *freqs,
		unsigned long user_freq)
{

	struct cpufreq_available_frequencies *tmp = freqs;
	unsigned long higher=0L, lower=0L;

	/* if limits are available determine if an out of bounds values is given */
	if (limits != NULL) {
		if (user_freq<=limits->min)
			return limits->min;

		if (user_freq>=limits->max)
			return limits->max;
	}

	/* normalize freq */
	while (tmp != NULL) {
		if (tmp->frequency>=user_freq && (tmp->frequency<higher || higher==0))
			higher = tmp->frequency;

		if (tmp->frequency<=user_freq && (tmp->frequency>lower || lower==0))
			lower = tmp->frequency;

		tmp = tmp->next;
	}

	return user_freq >= ((higher-lower)/2)+lower ? higher : lower;
}

/* translate percent values to absolute values */
unsigned long percent_to_absolute(unsigned long max_freq, unsigned long user_freq) {
	return max_freq * ((float)user_freq / 100);
}

/* goes through the list and returns the highest frequency */
unsigned long get_max_available_freq(struct cpufreq_available_frequencies *freqs) {
	unsigned long max = 0;
	struct cpufreq_available_frequencies *tmp = freqs;
	while(tmp != NULL) {
		if (max < tmp->frequency)
			max = tmp->frequency;
		tmp = tmp->next;
	}
	return max;
}

/* goes through the list and returns the lowest frequency */
unsigned long get_min_available_freq(struct cpufreq_available_frequencies *freqs) {
	unsigned long min = 0;
	struct cpufreq_available_frequencies *tmp = freqs;
	while(tmp != NULL) {
		if (min > tmp->frequency)
			min = tmp->frequency;
		tmp = tmp->next;
	}
	return min;
}

/* int get_cpu_num(void)
 *
 * Gets the number of installed CPUs from procfs
 * and sets cpu_num appropriately.
 *
 * Returns always at least 1 (you can't run this function without any cpu!)
 */
int get_cpu_num(void) {
	FILE *fp;
	int n;
	char line[256];

	fp = fopen(CPUINFO_PROC, "r");
	if(!fp) {
		clog(LOG_ERR, "%s: %s\n", CPUINFO_PROC, strerror(errno));
		return 1;
	}

	n = 0;
	while(!feof(fp)) {
		fgets(line, 255, fp);
		if(!strncmp(line, "processor", 9))
			n++;
	}
	fclose(fp);

	clog(LOG_DEBUG, "found %i CPUs\n", n);

	return n > 0 ? n : 1;
}
