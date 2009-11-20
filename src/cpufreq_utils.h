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

#include <cpufreq.h>
#include "config_parser.h"

#define CPUINFO_PROC  "/proc/cpuinfo"

unsigned long normalize_frequency (struct cpufreq_limits *limits,
                                   struct cpufreq_available_frequencies *freqs,
                                   unsigned long user_freq);
unsigned long percent_to_absolute(unsigned long max_freq, unsigned long user_freq);
unsigned long get_max_available_freq(struct cpufreq_available_frequencies *freqs);
unsigned long get_min_available_freq(struct cpufreq_available_frequencies *freqs);
int get_cpu_num(void);

