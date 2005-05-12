/*
 *  Copyright (C) 2002-2005  Mattia Dongili <malattia@gmail.com>
 *                           George Staikos <staikos@0wned.org>
 *  2004.10.12
 *  - reworked to support new plugin architecture
 *    by Mattia Dongili
 *  
 *  2004.08.22
 *  - added percentage/absolute frequency translation based on a patch submitted
 *    by Herv� Eychenne
 *
 *  2003.16.08
 *  - added support for cpu monitoring, base code by Dietz Proepper and minor
 *    fixes by Mattia Dongili
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

#include <errno.h>
#include <ctype.h>
#include <string.h>
#include "config_parser.h"
#include "cpufreqd_log.h"
#include "main.h"
#include "plugin_utils.h"
#include "cpufreq_utils.h"

extern struct cpufreqd_conf configuration;

/* local functions */
static char *strip_comments_line  (char *str);
static char *clean_config_line    (char *str);

/* char *clean_config_line (char *str)
 *
 * Removes trailing blanks and CR off the string str
 *
 * Returns a pointer to the cleaned string.
 * WARNING: it modifies the input string!
 */
char *clean_config_line (char *str) {
  int i = 0;

  /* remove white spaces at the beginning */
  while (isspace(str[0])) {
    str++;
  }

  /* remove end line white space */
  i = strlen(str) - 1;
  while (i >= 0 && isspace(str[i])) {
    str[i] = 0;
    i--;
  }

  return str;
}

/* char *strip_comments_line (char *str)
 *
 * Removes comments off the string str
 *
 * Returns a pointer to the cleaned string.
 * WARNING: it modifies the input string!
 */
char *strip_comments_line (char *str) {
  int i;

  /* remove comment */
  for (i = strlen(str); i >= 0; i--) {
    if (str[i] == '#') {
      str[i] = '\0';
    }
  }

  return str;
}

char *read_clean_line(FILE *fp, char *buf, int n) {
  if (fgets(buf, n, fp)) {
    buf[n] = '\0';
    buf = strip_comments_line(buf);
    /* returned an empty line ? */
    if (buf[0]) {
      buf = clean_config_line(buf);
    }
  }
  return buf;
}

/*
 * parse the [General] section
 *
 * Returns -1 if required properties are missing, 0 otherwise
 */
int parse_config_general (FILE *config) {
  char buf[MAX_STRING_LEN];
  char *clean;
  char *name;
  char *value;
  char *token;
  struct plugin_obj o_plugin;
  struct NODE *n_plugin;

  while (!feof(config)) {

    clean = read_clean_line(config, buf, MAX_STRING_LEN);

    if (!clean[0]) /* returned an empty line */
      continue;

    /* end of section */
    if (strcmp(clean,"[/General]") == 0)
      break;
      
    name = strtok(clean, "=");
    value = strtok(NULL, "");

      
    if (strcmp(name,"poll_interval") == 0) {
      if (value != NULL) {
        configuration.poll_interval = atoi (value);
      }
      /* validate */
      if (configuration.poll_interval < 1) {
        cpufreqd_log(LOG_WARNING, 
            "WARNING: [General] poll_interval has invalid value, using default.\n");
        configuration.poll_interval = DEFAULT_POLL;
      }
    } else if (strcmp(name,"verbosity") == 0) {
      if (!configuration.log_level_overridden) {
        if (value != NULL) {
          configuration.log_level = atoi (value);
          /* validate */
          if (configuration.log_level < 0 || configuration.log_level > 7) {
            cpufreqd_log(LOG_WARNING, 
                "WARNING: [General] verbosity has invalid value, using default (%d).\n", DEFAULT_VERBOSITY);
            configuration.log_level = DEFAULT_VERBOSITY;
          }
        } else {
          configuration.log_level = DEFAULT_VERBOSITY;
          cpufreqd_log(LOG_WARNING, 
              "WARNING: [General] verbosity has empty value, using default (%d).\n", DEFAULT_VERBOSITY);
        }
      } else {
          cpufreqd_log(LOG_DEBUG, 
              "parse_config_general(): skipping \"verbosity\", overridden in the command line.\n");
      }
    /*
    } else if (strcmp(name,"pm_type") == 0) {
      if (value != NULL) {
        strncpy(configuration.pm_plugin, value, 101);
      } else {
        cpufreqd_log(LOG_WARNING, 
            "parse_config_general(): empty \"pm_type\", using default "DEFAULT_PMPLUGIN".\n");
        strncpy(configuration.pm_plugin, DEFAULT_PMPLUGIN, 101);
      }
      configuration.pm_plugin[100] = 0;
    */
    } else if (strcmp(name,"enable_plugins") == 0) {
      /*
       *  tokenize the value and set the list of plugins
       */
      token = strtok(value,",");
      do {
        o_plugin.library = NULL;
        o_plugin.plugin = NULL;
        o_plugin.used = 0;
        token = clean_config_line(token);
        if (token == NULL)
          continue;

        strncpy(o_plugin.name, token, MAX_STRING_LEN);
        o_plugin.name[MAX_STRING_LEN-1] = '\0';
        
        n_plugin = node_new(&o_plugin, sizeof(struct plugin_obj));
        list_append(&(configuration.plugins), n_plugin);
        cpufreqd_log(LOG_DEBUG, "parse_config_general(): read plugin: %s\n", token);

      } while ((token = strtok(NULL,",")) != NULL);
  
      
    } else if (strcmp(name,"pidfile") == 0) {
      if (value != NULL) {
        strncpy(configuration.pidfile, value, MAX_PATH_LEN);
      } else {
        cpufreqd_log(LOG_WARNING, 
            "parse_config_general(): empty \"pidfile\", using default "CPUFREQD_PIDFILE".\n");
        strncpy(configuration.pidfile, CPUFREQD_PIDFILE, MAX_PATH_LEN);
      }
      configuration.pidfile[MAX_PATH_LEN-1] = '\0';
      
    } else if (strcmp(name,"acpi_workaround") == 0) {
      if (value != NULL) {
        configuration.acpi_workaround = atoi (value);
        cpufreqd_log(LOG_WARNING, "parse_config_general(): ACPI workaround enabled.\n");
      }
    } else {
      cpufreqd_log(LOG_WARNING, "WARNING: [General] skipping unknown config option \"%s\"\n", name);
    }
  }

  return 0;
}

/*
 * parse a [Profile] section
 *
 * Returns -1 if required properties are missing, 0 otherwise
 */
int parse_config_profile (FILE *config, struct profile *p) {
#define HAS_NAME    (1<<0)
#define HAS_MIN     (1<<1)
#define HAS_MAX     (1<<2)
#define HAS_POLICY  (1<<3)
#define HAS_CPU     (1<<4)
  int state=0, min_is_percent=0, max_is_percent=0, tmp_freq=0;
  char buf[MAX_STRING_LEN];

  while (!feof(config)) {
    char *clean;
    char *name;
    char *value;

    clean = read_clean_line(config, buf, MAX_STRING_LEN);

    if (!clean[0]) /* returned an empty line */
      continue;

    /* end of section */
    if (strcmp(clean,"[/Profile]") == 0)
      break;
      
    name = strtok(clean, "=");
    value = strtok(NULL, "");
    
    /* empty value: skip */
    if (value == NULL)
      continue;
    
    if (strcmp(name,"name")==0) {
      strncpy(p->name, value, MAX_STRING_LEN);
      p->name[MAX_STRING_LEN-1] = '\0';
      state |= HAS_NAME;
    } else if (strcmp(name,"minfreq")==0) {
      
      if (sscanf(value, "%d", &tmp_freq) != 1) {
        cpufreqd_log(LOG_ERR, "parse_config_profile(): unable to parse MIN value %s.\n", value);
        return -1;
      }
      if (strstr(value, "%") != NULL) {
        min_is_percent=1;
      }
      if (tmp_freq < 0) {
        cpufreqd_log(LOG_NOTICE, "parse_config_profile(): MIN freq below 0, resetting\n");
        p->policy.min = 0;
      } else {
        p->policy.min = tmp_freq;
      }
      state |= HAS_MIN;
      
    } else if (strcmp(name,"maxfreq") == 0) {
      
      if (sscanf(value, "%d", &tmp_freq) != 1) {
        cpufreqd_log(LOG_ERR, "parse_config_profile(): unable to parse MAX value %s.\n", value);
        return -1;
      }
      if (strstr(value, "%") != NULL) {
        max_is_percent=1;
      }
      if (tmp_freq < 0) {
        cpufreqd_log(LOG_NOTICE, "parse_config_profile(): MAX freq below 0, resetting\n");
        p->policy.max = 0;
      } else {
        p->policy.max = tmp_freq;
      }
      state |= HAS_MAX;
    
    } else if (strcmp(name,"cpu") == 0) {
      
      if (sscanf(value, "%u", &(p->cpu)) != 1) {
        cpufreqd_log(LOG_ERR, "parse_config_profile(): unable to parse CPU value %s.\n", value);
        return -1;
      }
      state |= HAS_CPU;
      
    } else if (strcmp(name,"policy") == 0) {
      
      strncpy(p->policy.governor, value, MAX_STRING_LEN);
      p->policy.governor[MAX_STRING_LEN-1] = 0;
      state |= HAS_POLICY;
    
    } else {
      cpufreqd_log(LOG_WARNING, "WARNING: [Profile] skipping unknown config option \"%s\"\n", name);
    }
  }

  if (!(state & HAS_NAME)) {
    cpufreqd_log(LOG_ERR, "parse_config_profile(): [Profile] missing required property \"name\".\n");
    return -1;
  }
  if (!(state & HAS_MIN)) {
    cpufreqd_log(LOG_ERR, 
        "parse_config_profile(): [Profile] \"%s\" missing required property \"minfreq\".\n", p->name);
    return -1;
  }
  if (!(state & HAS_MAX)) {
    cpufreqd_log(LOG_ERR, 
        "parse_config_profile(): [Profile] \"%s\" missing required property \"maxfreq\".\n", p->name);
    return -1;
  }
  if (!(state & HAS_POLICY)) {
    cpufreqd_log(LOG_ERR, 
        "parse_config_profile(): [Profile] \"%s\" missing required property \"policy\".\n", p->name);
    return -1;
  }

  /* TODO: check if the selected governor is available */


  if (configuration.limits!=NULL) {
    /* calculate actual frequncies if percent where given frequencies */
    if (state & HAS_CPU) {
      if (min_is_percent)
        p->policy.min = percent_to_absolute(configuration.limits[p->cpu].max, p->policy.min);
      if (max_is_percent)
        p->policy.max = percent_to_absolute(configuration.limits[p->cpu].max, p->policy.max);
    } else {
      if (min_is_percent)
        p->policy.min = percent_to_absolute(configuration.limits[0].max, p->policy.min);
      if (max_is_percent)
        p->policy.max = percent_to_absolute(configuration.limits[0].max, p->policy.max);
    }
    /* normalize frequencies if such informations are available */
    p->policy.max = normalize_frequency(configuration.limits, configuration.sys_info->frequencies, p->policy.max);
    p->policy.min = normalize_frequency(configuration.limits, configuration.sys_info->frequencies, p->policy.min);
  } else {
    if (min_is_percent || max_is_percent)
      cpufreqd_log(LOG_WARNING, "Unable to calculate absolute values for profile \"%s\".\n", p->name);
    cpufreqd_log(LOG_WARNING, "Unable to normalize frequencies for profile \"%s\".\n", p->name);
  }

  cpufreqd_log(LOG_DEBUG,
      "parse_config_profile(): [Profile] \"%s\" MAX is %ld, MIN is %ld\n", p->name,
      p->policy.max, p->policy.min);

  if (p->policy.min > p->policy.max) {
    cpufreqd_log(LOG_WARNING, "parse_config_profile(): [Profile] \"%s\" uh! MIN freq is higher than MAX freq??\n",
        p->name);
  }
  
  return 0;
}

/*
 * parses a [Rule] section
 *
 * Returns -1 if required properties are missing, 0 otherwise
 */
int parse_config_rule (FILE *config, struct rule *r) {
#define HAS_NAME    (1<<0) 
#define HAS_PROFILE (1<<1)
  int state=0, keyword_handler_found=0;
  char buf[MAX_STRING_LEN];
  char *clean, *name, *value;
  struct NODE *n, *ren;
  struct plugin_obj *o_plugin;
  struct cpufreqd_keyword *ckw;

  /* reset profile ref */
  r->prof = 0;
  
  while (!feof(config)) {

    clean = read_clean_line(config, buf, MAX_STRING_LEN);

    if (!clean[0]) /* returned an empty line */
      continue;

    if (strcmp(clean,"[/Rule]") == 0)
      break;

    name = strtok(clean, "=");
    value = strtok(NULL, "");

    /* empty value: skip */
    if (value == NULL)
      continue;

    if (strcmp(name, "profile") == 0) {
      strncpy(r->profile_name, value, MAX_STRING_LEN);
      r->profile_name[MAX_STRING_LEN-1] = '\0';
      state |= HAS_PROFILE;

    } else if (strcmp(name,"name") == 0) {
      strncpy(r->name, value, MAX_STRING_LEN);
      r->name[MAX_STRING_LEN-1] = '\0';
      state |= HAS_NAME;

    } else {
      /* foreach plugin */
      for (n=configuration.plugins.first; n!=NULL; n=n->next) {
        o_plugin = (struct plugin_obj*)n->content;
        if (o_plugin!=NULL && o_plugin->plugin!=NULL && o_plugin->plugin->keywords!=NULL) {
          /* foreach keyword */
          for(ckw=o_plugin->plugin->keywords; ckw->word!=NULL; ckw++) {
            if (strcmp(ckw->word, name) == 0) {
              cpufreqd_log(LOG_DEBUG, "Plugin %s handles keyword %s (value=%s)\n",
                  o_plugin->plugin->plugin_name, name, value);
              /* increase plugin use count */
              o_plugin->used++;

              ren = node_new(NULL, sizeof(struct rule_en));
              if (ren == NULL) {
                cpufreqd_log(LOG_ERR, "parse_config_rule(): [Rule] cannot make enough room for a new entry (%s).\n",
                    strerror(errno));
                return -1;
              }
              
              if (ckw->parse(value, &(((struct rule_en *)ren->content)->obj)) !=0) {
                cpufreqd_log(LOG_ERR, "parse_config_rule(): [Rule] Plugin %s is unable to parse this value \"%s\".\n",
                    o_plugin->plugin->plugin_name, value);
                return -1;
              }
              ((struct rule_en *)ren->content)->eval = ckw->evaluate;
              list_append(&(r->entries), ren);
              keyword_handler_found=1;
            }
          } /* end foreach keyword */
        } /* end if plugin_obj!=NULL */
      } /* enf foreach plugin */
      if (!keyword_handler_found)
        cpufreqd_log(LOG_WARNING, "WARNING: [Rule] skipping unknown config option \"%s\"\n", name);
      keyword_handler_found=0;
    }
  }

  if (!(state & HAS_NAME)) {
    cpufreqd_log(LOG_ERR, "parse_config_rule(): [Rule] missing required property \"name\".\n");
    return -1;
  }

  if (!(state & HAS_PROFILE)) {
    cpufreqd_log(LOG_ERR, "parse_config_rule(): [Rule] \"%s\" missing required property \"profile\".\n", 
                 r->name);
    return -1;
  }

  return 0;
}
