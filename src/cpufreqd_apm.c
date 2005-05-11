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

#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include "cpufreqd_plugin.h"
#include "cpufreqd.h"

#define APM_FILE "/proc/apm"

struct battery_interval {
  int min, max;
};

static int battery_present;
static int battery_percent;
static int ac_state;

static int apm_init(void);
static int apm_exit(void);
static int apm_update(void);
static int apm_ac_parse(const char *ev, void **obj);
static int apm_ac_evaluate(const void *s);
static int apm_bat_parse(const char *ev, void **obj);
static int apm_bat_evaluate(const void *s);

static struct cpufreqd_keyword kw[] = {
  { .word = "ac",       .parse = &apm_ac_parse,  .evaluate = &apm_ac_evaluate  },
  { .word = "battery",  .parse = &apm_bat_parse, .evaluate = &apm_bat_evaluate },
  { .word = NULL,       .parse = NULL,           .evaluate = NULL              }
};

static struct cpufreqd_plugin apm = {
  .plugin_name      = "apm_plugin",          /* plugin_name */
  .keywords         = kw,                    /* config_keywords */
  .poll_interval    = 1000,                  /* poll_interval (1 second) */
  .plugin_init      = &apm_init,             /* plugin_init */
  .plugin_exit      = &apm_exit,             /* plugin_exit */
  .plugin_update    = &apm_update,           /* plugin_update */
  .cfdprint         = NULL
};

/*  static int apm_init(void)
 *
 *  test if apm file id present
 */
static int apm_init(void) {
  struct stat sb;
  int rc;
  
  rc = stat(APM_PROC_FILE, &sb);
  if (rc < 0) {
    apm.cfdprint(LOG_ERR, "apm_init(): %s: %s\n", APM_PROC_FILE, strerror(errno));
    return -1;
  }
  return 0;
}

static int apm_exit(void) {
  return 0;
}

/*  static int apm_ac_update(void)
 *  
 *  reads temperature valuse ant compute a medium value
 */
static int apm_update(void) {
  FILE *fp;
  char buf[101];
  
  /***** APM SCAN *****/
  char ignore3[101];
  int ignore;
  unsigned int ignore2;
  unsigned int batt_flag;
    
  apm.cfdprint(LOG_DEBUG, "%s - update() called\n", apm.plugin_name);

  fp = fopen(APM_PROC_FILE , "r");
  if (!fp) {
    cp_log(LOG_ERR, "scan_system_info(): %s: %s\n", APM_PROC_FILE, strerror(errno));
    return -1;
  }
    
  if (!fgets(buf, 100, fp)) {
    fclose(fp);
    cp_log(LOG_ERR, "scan_system_info(): %s: %s\n", APM_PROC_FILE, strerror(errno));
    return -1;
  }
    
  sscanf(buf, "%s %d.%d %x %x %x %x %d%% %d %s\n",
                    ignore3, &ignore, &ignore, &ignore, ac_state, &ignore2,
                    &batt_flag, battery_percent, &ignore, ignore3);

  if (battery_percent > 100) {
    battery_percent = -1;
  }

  battery_present = batt_flag < 128;
    
  fclose(fp);
  
  cp_log(LOG_INFO, "scan_system_info(): battery %s - %d - ac: %s\n",
                    battery_present?"present":"absent", 
                    battery_percent, 
                    ac_state?"on-line":"off-line");
  return 0;
}

/*
 *  parse the 'ac' keywork
 */
static int apm_ac_parse(const char *ev, void **obj) {
  int *ret = malloc(sizeof(int));
  if (ret == NULL) {
    apm.cfdprint(LOG_ERR, 
        "%s - apm_ac_parse() couldn't make enough room for ac_status (%s)\n",
        strerror(errno));
    return -1;
  }
  
  *ret = 0;
  
  apm.cfdprint(LOG_DEBUG, "%s - apm_ac_parse() called with: %s\n",
      apm.plugin_name, ev);
  
  if (strncmp(ev, "on", 2) == 0) {
    *ret = PLUGGED;
  } else if (strncmp(ev, "off", 3) == 0) {
    *ret = UNPLUGGED;
  } else {
    apm.cfdprint(LOG_ERR, "%s - apm_parse() couldn't parse %s\n", ev);
    free(ret);
    return -1;
  }
  
  apm.cfdprint(LOG_INFO, "%s - apm_ac_parse() parsed: %s\n",
      apm.plugin_name, *ret==PLUGGED ? "on" : "off");

  *obj = ret;
  return 0;
}

/*
 *  evaluate the 'ac' keywork
 */
static int apm_ac_evaluate(const void *s) {
  const int *ac = (const int *)s;
  
  apm.cfdprint(LOG_DEBUG, "%s - evaluate() called: %s [%s]\n",
      apm.plugin_name, *ac==PLUGGED ? "on" : "off", ac_state==PLUGGED ? "on" : "off");

  return (*ac == ac_state) ? MATCH : DONT_MATCH;
}

/*
 *  parse the 'battery' keywork
 */
static int apm_bat_parse(const char *ev, void **obj) {
  struct battery_interval *ret = malloc(sizeof(struct battery_interval));
  if (ret == NULL) {
    apm.cfdprint(LOG_ERR, 
        "%s - apm_bat_parse() couldn't make enough room for battery_interval (%s)\n",
        strerror(errno));
    return -1;
  }
  
  ret->min = ret->max = 0;
  
  apm.cfdprint(LOG_DEBUG, "%s - apm_bat_parse() called with: %s\n",
      apm.plugin_name, ev);
  
  if (sscanf(ev, "%d-%d", &(ret->min), &(ret->max)) != 2) {
    apm.cfdprint(LOG_ERR, "%s - apm_bat_parse() wrong parameter: %s\n",
        apm.plugin_name, ev);
    free(ret);
    return -1;
  }
  
  apm.cfdprint(LOG_INFO, "%s - apm_bat_parse() parsed: %d-%d\n",
      apm.plugin_name, ret->min, ret->max);

  *obj = ret;
  return 0;
}

/*
 *  evaluate the 'battery' keywork
 */
static int apm_bat_evaluate(const void *s) {
  const struct battery_interval *bi = (const struct battery_interval *)s;
  
  apm.cfdprint(LOG_DEBUG, "%s - apm_bat_evaluate() called: %d-%d [%d]\n",
      apm.plugin_name, bi->min, bi->max, battery_level);

  return (battery_level>=bi->min && battery_level<=bi->max) ? MATCH : DONT_MATCH;
}

struct cpufreqd_plugin *create_plugin (void) {
  return &apm;
}
