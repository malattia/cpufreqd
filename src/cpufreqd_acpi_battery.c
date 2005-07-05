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

#define ACPI_BATTERY_DIR        "/proc/acpi/battery/"
#define ACPI_BATTERY_STATE_FILE "/state"
#define ACPI_BATTERY_INFO_FILE  "/info"
#define ACPI_BATTERY_FULL_CAPACITY_FMT  "last full capacity:      %d %sh\n"
#define ACPI_BATTERY_REM_CAPACITY_FMT   "remaining capacity:      %d %sh\n"

struct battery_interval {
  int min, max;
};

struct battery_info {
  int capacity;
  int present;
  char path[100];
};

static struct battery_info *infos   = 0L;
static int bat_num                  = 0;
static int battery_level            = 0;

static int no_dots(const struct dirent *d);
static int check_battery(struct battery_info *info);
static int acpi_battery_init(void);
static int acpi_battery_exit(void);
static int acpi_battery_parse(const char *ev, void **obj);
static int acpi_battery_evaluate(const void *s);
static int acpi_battery_update(void);

static struct cpufreqd_keyword kw[] = {
  { .word = "battery_interval",  .parse = &acpi_battery_parse,  .evaluate = &acpi_battery_evaluate },
  { .word = NULL,       .parse = NULL,            .evaluate = NULL }
};

static struct cpufreqd_plugin acpi_battery = {
  .plugin_name      = "acpi_battery_plugin",      /* plugin_name */
  .keywords         = kw,                    /* config_keywords */
  .poll_interval    = 1000,                  /* poll_interval (1 second) */
  .plugin_init      = &acpi_battery_init,         /* plugin_init */
  .plugin_exit      = &acpi_battery_exit,         /* plugin_exit */
  .plugin_update    = &acpi_battery_update,       /* plugin_update */
  .cfdprint         = NULL
};

/* int no_dots(const struct dirent *d)
 * 
 * Filter function for scandir, returns
 * 0 if the first char of d_name is not
 * a dot.
 */
static int no_dots(const struct dirent *d) {
  return d->d_name[0]!= '.';
}

/*  static int check_battery(char *dirname)
 *
 *  check if the battery is present and feed the global
 *  bat_full_capacity variable.
 *  Returns 1 if the battery is there and has been able
 *  to read its capacity, 0 otherwise.
 */
static int check_battery(struct battery_info *info) {
  FILE *fp;
  char file_name[256];
  char ignore[100];
  char line[100];
  int tmp_capacity;
  
  snprintf(file_name, 256, "%s%s", info->path, ACPI_BATTERY_INFO_FILE);
  /** /proc/acpi/battery/.../info **/
  fp = fopen(file_name, "r");
  if (!fp) {
    acpi_battery.cfdprint(LOG_ERR , "%s - check_battery(): %s: %s\n", 
        acpi_battery.plugin_name, file_name, strerror(errno));
    return 0;
  }

  while (fgets(line, 100, fp)) {
    if(sscanf(line, ACPI_BATTERY_FULL_CAPACITY_FMT, &tmp_capacity, ignore) == 2) {
      info->capacity = tmp_capacity;
      info->present = 1;
    }
  }
  fclose(fp);

  return (tmp_capacity != 0) ? 1 : 0;
}


/*  static int acpi_battery_init(void)
 *
 *  test if BATTERY dirs are present
 */
static int acpi_battery_init(void) {
  struct dirent **namelist = NULL;
  int n = 0;

  /* get batteries paths */
  bat_num = n = scandir(ACPI_BATTERY_DIR, &namelist, no_dots, NULL);
  if (n > 0) {
    infos = malloc( bat_num*sizeof(struct battery_info) );
    
    while (n--) {
      /* put the path into the array */
      snprintf(infos[n].path, 100, "%s%s", ACPI_BATTERY_DIR, namelist[n]->d_name);
      infos[n].present = 0;
      infos[n].capacity = 0;
      /* check this battery */
      check_battery(&(infos[n]));
      
      acpi_battery.cfdprint(LOG_INFO, "acpi_battery_init() - battery path: %s, %s, capacity:%d\n",
          infos[n].path, infos[n].present?"present":"absent", infos[n].capacity);
      free(namelist[n]);
    }
    free(namelist);
    acpi_battery.cfdprint(LOG_INFO, "acpi_battery_init() - found %d battery slots\n", bat_num);
    
  } else if (n < 0) {
    acpi_battery.cfdprint(LOG_ERR, "acpi_battery_init() - error, acpi_battery module not compiled or inserted (%s: %s)?\n",
        ACPI_BATTERY_DIR, strerror(errno));
    acpi_battery.cfdprint(LOG_ERR, "acpi_battery_init() - exiting.\n");
    return -1;   

  } else {
    acpi_battery.cfdprint(LOG_ERR, "acpi_battery_init() - no batteries found, not a laptop?\n");
    acpi_battery.cfdprint(LOG_ERR, "acpi_battery_init() - exiting.\n");
    return -1;
  }
  
  return 0;
}

static int acpi_battery_exit(void) {
  if (infos != NULL) {
    free(infos);
  }
  acpi_battery.cfdprint(LOG_INFO, "%s - exited.\n", acpi_battery.plugin_name);
  return 0;
}

/*
 *  Parses entries of the form %d-%d (min-max)
 */
static int acpi_battery_parse(const char *ev, void **obj) {
  struct battery_interval *ret = malloc(sizeof(struct battery_interval));
  if (ret == NULL) {
    acpi_battery.cfdprint(LOG_ERR, 
        "%s - acpi_battery_parse() couldn't make enough room for battery_interval (%s)\n",
        strerror(errno));
    return -1;
  }
  
  ret->min = ret->max = 0;
  
  acpi_battery.cfdprint(LOG_DEBUG, "%s - acpi_battery_parse() called with: %s\n",
      acpi_battery.plugin_name, ev);
  
  if (sscanf(ev, "%d-%d", &(ret->min), &(ret->max)) != 2) {
    acpi_battery.cfdprint(LOG_ERR, "%s - acpi_battery_parse() wrong parameter: %s\n",
        acpi_battery.plugin_name, ev);
    free(ret);
    return -1;
  }
  
  acpi_battery.cfdprint(LOG_INFO, "%s - acpi_battery_parse() parsed: %d-%d\n",
      acpi_battery.plugin_name, ret->min, ret->max);

  *obj = ret;
  return 0;
}


static int acpi_battery_evaluate(const void *s) {
  const struct battery_interval *bi = (const struct battery_interval *)s;
  
  acpi_battery.cfdprint(LOG_DEBUG, "%s - acpi_battery_evaluate() called: %d-%d [%d]\n",
      acpi_battery.plugin_name, bi->min, bi->max, battery_level);

  return (battery_level>=bi->min && battery_level<=bi->max) ? MATCH : DONT_MATCH;
}

/*  static int acpi_battery_update(void)
 *  
 *  reads temperature valuse ant compute a medium value
 */
static int acpi_battery_update(void) {
  FILE *fp;
  char ignore[100];
  char file_name[256];
  char line[100];
  int i=0, capacity=0, remaining=0, tmp_remaining=0, n_read=0;
  
  battery_level = 0;
  
  /* Read battery informations */
  for (i=0; i<bat_num; i++) {

#if 0
    /** 
     ** /proc/acpi/battery/.../info
     **/
    snprintf(file_name, 256, "%s%s", bat_dirs[i], ACPI_BATTERY_INFO_FILE);

    /* avoid reading the info file if configured */
    if (!configuration->acpi_workaround) {
      
      fp = fopen(file_name, "r");
      if (!fp) {
        acpi_battery.cfdprint(LOG_ERR , "acpi_battery_update(): %s: %s\n", file_name, strerror(errno));
        return -1;
      }
 
      while (!feof(fp)) {
        if (fscanf(ACPI_BATTERY_FULL_CAPACITY_FMT, &tmp_capacity, ignore) == 2) {
          bat_full_capacity += tmp_capacity;
          present_batteries++;
        }
      }
      fclose(fp);
    
    } else {
      acpi_battery.cfdprint(LOG_INFO, "acpi_battery_update(): not reading %s, ACPI workaround enabled.\n", file_name);
    }
#endif

    /* only if battery present */
    if (infos[i].present) {
      /**
       ** /proc/acpi/battery/.../state
       **/
      snprintf(file_name, 256, "%s%s", infos[i].path, ACPI_BATTERY_STATE_FILE);
      fp = fopen(file_name, "r");
      if (fp) {

        while (fgets(line, 100, fp)) {
          if (sscanf(line, ACPI_BATTERY_REM_CAPACITY_FMT, &tmp_remaining, ignore) == 2) {
            remaining += tmp_remaining;
            capacity += infos[i].capacity;
            n_read++;
          }
        }
        fclose(fp);

      } else {
        acpi_battery.cfdprint(LOG_ERR, "acpi_battery_update(): %s: %s\n", file_name, strerror(errno));
        acpi_battery.cfdprint(LOG_ERR,
            "acpi_battery_update(): battery path %s disappeared? send SIGHUP to re-read batteries\n",
            infos[i].path);
      }
    } /* if present */
      
  } /* end infos loop */

  /* calculates medium battery life between all batteries */
  if (n_read > 0)
    battery_level = 100 * (remaining / (double)capacity);
  else
    battery_level = 0;

  acpi_battery.cfdprint(LOG_INFO, "acpi_battery_update(): battery life %d%%\n",
      battery_level);
  
  return 0;
}

struct cpufreqd_plugin *create_plugin (void) {
  return &acpi_battery;
}
