#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include "cpufreqd_plugin.h"
#include "cpufreqd.h"

#define ACPI_AC_DIR "/proc/acpi/ac_adapter/"
#define ACPI_AC_FILE "/state"
#define ACPI_AC_FORMAT "state:                   %s\n"

#define PLUGGED   1
#define UNPLUGGED 0

static char *ac_filelist[64];
static unsigned short ac_state  = 0;
static int ac_dir_num           = 0;

static int no_dots(const struct dirent *d);
static int acpi_ac_init(void);
static int acpi_ac_exit(void);
static int acpi_ac_parse(const char *ev, void **obj);
static int acpi_ac_evaluate(const void *s);
static int acpi_ac_update(void);

static struct cpufreqd_keyword kw[] = {
  { .word = "ac",  .parse = &acpi_ac_parse,  .evaluate = &acpi_ac_evaluate },
  { .word = NULL,       .parse = NULL,            .evaluate = NULL }
};

static struct cpufreqd_plugin acpi_ac = {
  .plugin_name      = "acpi_ac_plugin",      /* plugin_name */
  .keywords         = kw,                    /* config_keywords */
  .poll_interval    = 1000,                  /* poll_interval (1 second) */
  .plugin_init      = &acpi_ac_init,         /* plugin_init */
  .plugin_exit      = &acpi_ac_exit,         /* plugin_exit */
  .plugin_update    = &acpi_ac_update,       /* plugin_update */
  .cfdprint         = NULL
};

static int no_dots(const struct dirent *d) {
  return d->d_name[0]!= '.';
}

/*  static int acpi_ac_init(void)
 *
 *  test if AC dirs are present
 */
static int acpi_ac_init(void) {
  struct dirent **namelist = NULL;
  int n = 0;

  /* get AC path */
  n = scandir(ACPI_AC_DIR, &namelist, no_dots, NULL);
  if (n > 0) {
    ac_dir_num = n;
    *ac_filelist = malloc(n * 64 * sizeof(char));
    while (n--) {
      snprintf(ac_filelist[n], 64, "%s%s%s", ACPI_AC_DIR, namelist[n]->d_name, ACPI_AC_FILE);
      acpi_ac.cfdprint(LOG_INFO, "%s - AC path: %s\n", acpi_ac.plugin_name, ac_filelist[n]);
      free(namelist[n]);
    } 
    free(namelist);

  } else if (n < 0) {
    acpi_ac.cfdprint(LOG_DEBUG, "acpi_ac_init(): no acpi_ac module compiled or inserted? (%s: %s)\n", 
          ACPI_AC_DIR, strerror(errno));
    acpi_ac.cfdprint(LOG_ERR, "%s: exiting.\n");
    return -1;
    
  } else {
    acpi_ac.cfdprint(LOG_NOTICE, "acpi_ac_init(): no ac adapters found, not a laptop?\n");
    return -1;
  }
  return 0;
}

static int acpi_ac_exit(void) {
  if (ac_filelist != NULL)
    free(*ac_filelist);
  acpi_ac.cfdprint(LOG_INFO, "%s - exited.\n", acpi_ac.plugin_name);
  return 0;
}

static int acpi_ac_parse(const char *ev, void **obj) {
  int *ret = malloc(sizeof(int));
  if (ret == NULL) {
    acpi_ac.cfdprint(LOG_ERR, 
        "%s - acpi_ac_parse() couldn't make enough room for ac_status (%s)\n",
        strerror(errno));
    /*free(ret);*/
    return -1;
  }
  
  *ret = 0;
  
  acpi_ac.cfdprint(LOG_DEBUG, "%s - acpi_ac_parse() called with: %s\n",
      acpi_ac.plugin_name, ev);
  
  if (strncmp(ev, "on", 2) == 0) {
    *ret = PLUGGED;
  } else if (strncmp(ev, "off", 3) == 0) {
    *ret = UNPLUGGED;
  } else {
    acpi_ac.cfdprint(LOG_ERR, "%s - acpi_ac_parse() couldn't parse %s\n", ev);
    /*free(ret);*/
    return -1;
  }
  
  acpi_ac.cfdprint(LOG_INFO, "%s - acpi_ac_parse() parsed: %s\n",
      acpi_ac.plugin_name, *ret==PLUGGED ? "on" : "off");

  *obj = ret;
  return 0;
}

static int acpi_ac_evaluate(const void *s) {
  const int *ac = (const int *)s;
  
  acpi_ac.cfdprint(LOG_DEBUG, "%s - evaluate() called: %s [%s]\n",
      acpi_ac.plugin_name, *ac==PLUGGED ? "on" : "off", ac_state==PLUGGED ? "on" : "off");

  return (*ac == ac_state) ? MATCH : DONT_MATCH;
}

/*  static int acpi_ac_update(void)
 *  
 *  reads temperature valuse ant compute a medium value
 */
static int acpi_ac_update(void) {
  char temp[50];
  int i=0;
  FILE *fp = NULL;
  
  ac_state = UNPLUGGED;
  acpi_ac.cfdprint(LOG_DEBUG, "%s - update() called\n", acpi_ac.plugin_name);
  for (i=0; i<ac_dir_num; i++) {
    fp = fopen(ac_filelist[i], "r");
    if (!fp) {
      acpi_ac.cfdprint(LOG_ERR, "acpi_ac_update(): %s: %s\n",
         ac_filelist[i], strerror(errno));
      return -1;
    }
    fscanf(fp, ACPI_AC_FORMAT, temp);
    fclose(fp);
    
    acpi_ac.cfdprint(LOG_DEBUG, "acpi_ac_update(): read %s\n", temp);
    ac_state |= (strncmp(temp, "on-line", 7)==0 ? PLUGGED : UNPLUGGED);
  }
  
  acpi_ac.cfdprint(LOG_INFO, "acpi_ac_update(): ac_adapter is %s\n",
      ac_state==PLUGGED ? "on-line" : "off-line");
  return 0;
}

struct cpufreqd_plugin *create_plugin (void) {
  return &acpi_ac;
}
