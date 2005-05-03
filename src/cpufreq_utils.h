#include <cpufreq.h>
#include "config_parser.h"

#define CPUINFO_PROC  "/proc/cpuinfo"

int cpufreqd_set_profile (struct profile *p);
unsigned long normalize_frequency (struct cpufreq_limits *limits,  
                                   struct cpufreq_available_frequencies *freqs, 
                                   unsigned long user_freq);
unsigned long percent_to_absolute(unsigned long max_freq, unsigned long user_freq);
unsigned long get_max_available_freq(struct cpufreq_available_frequencies *freqs);
unsigned long get_min_available_freq(struct cpufreq_available_frequencies *freqs);
int get_cpu_num(void);

