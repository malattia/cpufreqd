#ifndef __CPUFREQD_H__
#define __CPUFREQD_H__

#define __CPUFREQD_VERSION__  "2.0.0"

/* log levels, for use in the whole application */
#define LOG_EMERG	  0	
#define LOG_ALERT	  1
#define LOG_CRIT	  2
#define LOG_ERR		  3
#define LOG_WARNING	4
#define LOG_NOTICE	5
#define LOG_INFO	  6
#define LOG_DEBUG	  7

#define DEFAULT_POLL 1
#define DEFAULT_VERBOSITY 3

#define MAX_STRING_LEN 255
#define MAX_PATH_LEN 512

#endif /* __CPUFREQD_H__ */
