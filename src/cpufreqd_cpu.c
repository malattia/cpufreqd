#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "cpufreqd_plugin.h"
#include "cpufreqd.h"

#define KVER_26 0
#define KVER_24 1

struct cpu_interval {
  int min;
  int max;
  float nice_scale;
};

static int cpufreqd_cpu_init(void);
static int cpufreqd_cpu_exit(void);
static int cpu_parse(const char *ev, void **obj);
static int cpu_evaluate(const void *s);
static int get_cpu(void);

static unsigned int c_user, c_nice, c_sys;
static unsigned int old_weighted_activity, old_time, delta_time, kernel_version;
static unsigned long int delta_activity=0, weighted_activity=0;

static struct cpufreqd_keyword kw[] = {
  { .word = "cpu", .parse = &cpu_parse,   .evaluate = &cpu_evaluate },
  { .word = NULL,  .parse = NULL,         .evaluate = NULL }
};

static struct cpufreqd_plugin cpu_plugin = {
  .plugin_name      = "cpu_plugin",               /* plugin_name */
  .keywords         = kw,                         /* config_keywords */
  .poll_interval    = 1000,                       /* poll_interval (1 second) */
  .plugin_init      = &cpufreqd_cpu_init,         /* plugin_init */
  .plugin_exit      = &cpufreqd_cpu_exit,         /* plugin_exit */
  .plugin_update    = &get_cpu,                   /* plugin_update */
  .cfdprint         = NULL
};

/*
 *  Reads kernel version for use when parsing /proc/stat output
 */
static int get_kversion(void) {
  FILE *fp;
  char kver[256];
  int f = 0;
  
  fp = fopen ("/proc/version", "r");
  if (!fp) {
    cpu_plugin.cfdprint(LOG_ERR, "get_kversion(): %s: %s\n", "/proc/version", strerror(errno));
    return -1;
  }
  do {
    f = fscanf (fp, "Linux version %s", kver);
  } while (f != 1);
  fclose(fp);
  kver[255] = '\0';

  cpu_plugin.cfdprint(LOG_INFO, "get_kversion(): read kernel version %s.\n", kver);
  
  if (strstr(kver, "2.6") == kver) {
    cpu_plugin.cfdprint(LOG_DEBUG, "get_kversion(): kernel version is 2.6.\n");
    return KVER_26;
  } else if (strstr(kver, "2.4") == kver) {
    cpu_plugin.cfdprint(LOG_DEBUG, "get_kversion(): kernel version is 2.4.\n");
    return KVER_24;
  } else {
    cpu_plugin.cfdprint(LOG_WARNING, "Unknown kernel version let's try to continue assuming a 2.6 kernel.\n");
    return KVER_26;
  }

}

static int cpufreqd_cpu_init(void) {
  cpu_plugin.cfdprint(LOG_INFO, "%s - init() called\n", cpu_plugin.plugin_name);
  kernel_version = get_kversion();
  return 0;
}

static int cpufreqd_cpu_exit(void) {
  cpu_plugin.cfdprint(LOG_INFO, "%s - exit() called\n", cpu_plugin.plugin_name);
  return 0;
}

static int cpu_parse(const char *ev, void **obj) {
  struct cpu_interval *myObj;

  myObj = malloc(sizeof(struct cpu_interval));
  if (myObj == NULL) {
    cpu_plugin.cfdprint(LOG_ERR, "%s - cpu_parse(): Unable to make room for a cpu interval (%s)\n", 
        cpu_plugin.plugin_name, strerror(errno));
    return -1;
  }
  myObj->min = myObj->max = 0;
  myObj->nice_scale = 3;
  
  cpu_plugin.cfdprint(LOG_DEBUG, "%s - cpu interval: %s\n", cpu_plugin.plugin_name, ev);

  sscanf(ev, "%d-%d,%f", &(myObj->min), &(myObj->max), &(myObj->nice_scale));
  cpu_plugin.cfdprint(LOG_INFO, "%s - read MIN:%d MAX:%d SCALE:%f\n", 
      cpu_plugin.plugin_name, myObj->min, myObj->max, myObj->nice_scale);

  if (myObj->nice_scale < 0.0) {
    cpu_plugin.cfdprint(LOG_WARNING, "%s - nice_scale value out of range(%f), resetting to default value(3).\n",
        cpu_plugin.plugin_name, myObj->nice_scale);
  }
  
  *obj = myObj;
  return 0;
}

static int cpu_evaluate(const void *s) {
  int cpu_percent = 0;
  const struct cpu_interval *c = (const struct cpu_interval *) s;

  cpu_plugin.cfdprint(LOG_DEBUG,
         "cpu_evaluate(): CPU delta_activity=%d delta_time=%d weighted_activity=%d.\n",
         delta_activity, delta_time, weighted_activity);

  if ( delta_activity > delta_time || delta_time <= 0) {
    cpu_percent = 100;
  } else {
    cpu_percent = delta_activity * 100 / delta_time;
  }
  
  cpu_plugin.cfdprint(LOG_DEBUG, "cpu_evaluate(): CPU usage = %d.\n", cpu_percent);
  cpu_plugin.cfdprint(LOG_DEBUG, "%s - cpu_evaluate() called with min=%d max=%d\n", 
      cpu_plugin.plugin_name, c->min, c->max);

  return (cpu_percent >= c->min && cpu_percent <= c->max) ? MATCH : DONT_MATCH;
}

static int get_cpu(void) {
  
  FILE* fp;
  int f;
  unsigned int c_time=0;
  unsigned long int c_idle=0, c_iowait=0, c_irq=0, c_softirq=0; /* for linux 2.6 only */

  cpu_plugin.cfdprint(LOG_DEBUG, "%s - update() called\n", cpu_plugin.plugin_name);
  /* read raw jiffies... */
  fp = fopen ("/proc/stat", "r");
  if (!fp) {
    cpu_plugin.cfdprint(LOG_ERR, "get_cpu(): %s: %s\n", "/proc/stat", strerror(errno));
    return -1;
  }
  do {
    f = fscanf (fp,
                "cpu  %u %u %u %lu %lu %lu %lu",
                &c_user, &c_nice, &c_sys, &c_idle, &c_iowait, &c_irq, &c_softirq);

  } while ((f!=4 && kernel_version==KVER_24) || (f!=7 && kernel_version==KVER_26));
  fclose(fp);

  cpu_plugin.cfdprint(LOG_INFO,
         "get_cpu(): CPU c_user=%d c_nice=%d c_sys=%d c_idle=%d c_iowait=%d c_irq=%d c_softirq=%d.\n",
         c_user, c_nice, c_sys, c_idle, c_iowait, c_irq, c_softirq);
  /* calculate total jiffies, weight them and save */
  c_sys += c_irq + c_softirq;
  c_idle += c_iowait;
  c_time = c_user + c_nice + c_sys + c_idle;
  delta_time = c_time - old_time;
  old_time = c_time;
  
  weighted_activity = c_user + c_nice / 3 + c_sys;
  delta_activity = weighted_activity - old_weighted_activity;
  old_weighted_activity = weighted_activity;

  return 0;
}

/* MUST DEFINE THIS ONE */
struct cpufreqd_plugin *create_plugin (void) {
  return &cpu_plugin;
}
