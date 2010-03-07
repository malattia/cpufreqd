/*
 *  Copyright (C) 2006  Victor de la Fuente Castillo <vniebla@gmail.com>
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
 *
 *  Based upon the pmu version:
 *  Copyright (C) 2003,2004  Rene Rebe <rene@rocklinux.org>
 *                2005       Mattia Dongili <malattia@linux.it>
 *
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpufreqd_plugin.h"

#  define CPU_INFO_FILE		"/proc/cpuinfo"

struct temperature_interval {
	int min, max;
};

struct temperature_interval tau_temperature;

static char tag[255];
static char val[255];

static int tokenize (FILE *fp, char *t, char *v) {
	char str[255];
	char *s1, *s2;

	t[0] = v[0] = '\0';

	if (fgets(str, 255, fp) == NULL)
		return EOF;

	if ((s1 = strtok(str, ":")) == NULL)
		return 0;

	s2 = s1 + strlen (s1) - 1;
	/* remove trailing spaces */
	for ( ; s1 != s2 ; --s2) {
		if (!isspace(*s2)) {
			break;
		} else {
			*s2 = '\0';
		}
	}

	strncpy (t, s1, 255);
	t[254] = '\0';

	if ((s1 = strtok(NULL, ":")) == NULL)
		return 0;

	for ( ; *s1 != 0 ; ++s1) {
		if (!isspace(*s1)) {
			break;
		}
	}
	s2 = s1 + strlen (s1) - 1;
	/* remove trailing spaces */
	for ( ; s1 != s2 ; --s2) {
		if (!isspace(*s2)) {
			break;
		} else {
			*s2 = '\0';
		}
	}

	strncpy (v, s1, 255);
	v[254] = '\0';

	return 1;
}

static int tau_init(void) {
	FILE *fp;

	fp = fopen(CPU_INFO_FILE, "r");
	if (!fp) {
		clog(LOG_INFO, "%s: %s\n", CPU_INFO_FILE, strerror(errno));
		return -1;
	}

	fclose(fp);

	clog(LOG_NOTICE, "/proc/cpuinfo file found\n");

	return 0;
}

static int tau_update(void) {

	FILE *fp;

	fp = fopen(CPU_INFO_FILE, "r");
	if (!fp) {
		clog(LOG_ERR, "%s: %s\n", CPU_INFO_FILE, strerror(errno));
		return -1;
	}

	char str[255];

	if (fgets(str, 255, fp) == NULL){
	  clog(LOG_INFO,"%s",str);
	  fclose(fp);
	  return -1;
	}

	while (tokenize(fp, tag, val) != EOF) {
		if (strcmp(tag, "temperature") == 0) {
		  int readed;
		  if (((readed=sscanf(val, "%d-%d", &(tau_temperature.min), &(tau_temperature.max))) < 1)
		      || (readed >2)) {
		    clog(LOG_ERR, "wrong temperature value %s\n", val);
		    fclose(fp);
		    return -1;
		  } else if (readed == 1) {
		    //Temperature is not an interval
		    tau_temperature.max = tau_temperature.min;
		  }
		  clog(LOG_INFO,"TAU temperature = %d-%d",tau_temperature.min, tau_temperature.max);
		  break; //Reading more is a waste of time
		}
	}
	fclose(fp);

	return 0;
}

static int tau_parse(const char *ev, void **obj) {
	struct temperature_interval *tau_temperature_interval = malloc(sizeof(struct temperature_interval));
	if (tau_temperature_interval == NULL) {
		clog(LOG_ERR, "couldn't make enough room for tau_temperature_interval (%s)\n",
				strerror(errno));
		return -1;
	}
	tau_temperature_interval->min = tau_temperature_interval->max = 0;
	clog(LOG_DEBUG, "called with %s\n", ev);

	if (sscanf(ev, "%d-%d", &(tau_temperature_interval->min), &(tau_temperature_interval->max)) != 2) {
		clog(LOG_ERR, "wrong parameter %s\n", ev);
		free(tau_temperature_interval);
		return -1;
	}

	clog(LOG_INFO, "parsed %d-%d\n", tau_temperature_interval->min, tau_temperature_interval->max);

	*obj = tau_temperature_interval;
	return 0;
}

static int tau_evaluate(const void *s) {
	const struct temperature_interval *ti = (const struct temperature_interval *)s;

	clog(LOG_DEBUG, "called %d-%d , actual temperature: %d-%d\n", ti->min, ti->max, tau_temperature.min, tau_temperature.max);

	return ((tau_temperature.min<=ti->min && tau_temperature.max>=ti->min)
		|| (tau_temperature.min<=ti->max && tau_temperature.max>=ti->max)
		|| (ti->max >= tau_temperature.max && ti->min<=tau_temperature.min))? MATCH : DONT_MATCH;
}

static struct cpufreqd_keyword kw[] = {
	{ .word = "tau_temperature",  .parse = &tau_parse, .evaluate = &tau_evaluate },
	{ .word = NULL,       .parse = NULL,           .evaluate = NULL              }
};

static struct cpufreqd_plugin tau = {
	.plugin_name      = "tau_plugin",	/* plugin_name */
	.keywords         = kw,			/* config_keywords */
	.plugin_init      = &tau_init,		/* plugin_init */
	.plugin_update    = &tau_update		/* plugin_update */
};

struct cpufreqd_plugin *create_plugin (void) {
	return &tau;
}
