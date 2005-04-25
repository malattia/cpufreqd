/*
 *  Copyright (C) 2002,2003,2004  Mattia Dongili<dongili@supereva.it>
 *                                George Staikos <staikos@0wned.org>
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

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <limits.h>
#include <stdlib.h>
#include <cpufreq.h>
#include <string.h>
#include "main.h"
#include "cpufreqd.h"
#include "daemon_utils.h"
#include "plugin_utils.h"
#include "config_parser.h"
#include "cpufreqd_log.h"
#include "cpufreq_utils.h"

/* default configuration */
struct cpufreqd_conf configuration = {
  .config_file = CPUFREQD_CONFDIR"cpufreqd.conf",
  .pidfile = "b",
  .cpu_num = 1,
  .poll_interval = DEFAULT_POLL,
  .has_sysfs = 1,
  .no_daemon = 0,
  .log_level_overridden = 0,
  .log_level = DEFAULT_VERBOSITY,
  .acpi_workaround=0,
  .print_help = 0,
  .print_version = 0
};

static int force_reinit = 0;
static int force_exit = 0;

/* intialize the cpufreqd_conf object 
 * by reading the configuration file
 */
int init_configuration(void) {
  FILE *fp_config;
  struct NODE *n, *np_iter, *nr_iter;
  struct plugin_obj *o_plugin;
  char buf[256];

  /* configuration file */
  cpufreqd_log(LOG_INFO, "init_configuration(): reading configuration file %s\n", configuration.config_file);
  fp_config = fopen(configuration.config_file, "r");
  if (!fp_config) {
    cpufreqd_log(LOG_ERR, "init_configuration(): %s: %s\n", configuration.config_file, strerror(errno));
    return -1;
  }

  while (!feof(fp_config)) {
    char *clean = 0L;

    clean = read_clean_line(fp_config, buf, 256);

    if (!clean[0]) /* returned an empty line */
      continue;

    /* if General scan general options */
    if (strstr(clean,"[General]")) {
      if (parse_config_general(fp_config) < 0) {
        return -1;
      } else {
        /* load plugins here! */
        /*
         *  Load plugins
         */
        n=configuration.plugins.first;
        while (n != NULL) {
          o_plugin = (struct plugin_obj*)n->content;
          /* take care!! nasty if statement!! */
          if (
              load_plugin(o_plugin)==0 &&
              get_cpufreqd_object(o_plugin)==0 &&
              initialize_plugin(o_plugin) == 0
              ) { 
              cpufreqd_log(LOG_INFO, "plugin loaded: %s\n", o_plugin->plugin->plugin_name);
              n=n->next;
              
          } else {
            cpufreqd_log(LOG_INFO, "plugin failed to load: %s\n", o_plugin->name);
            /* remove the list item and assing n the next node (returned from list_remove_node) */
            cpufreqd_log(LOG_NOTICE, "discarded plugin %s\n", o_plugin->name);
            n = list_remove_node(&(configuration.plugins), n);
          } /* end else */
        } /* end while */
      } /* end else */
      continue;
    }

    /* if Profile scan profile options */
    if (strstr(clean,"[Profile]")) {

      n = node_new(NULL, sizeof(struct profile));
      if (n == NULL) {
        cpufreqd_log(LOG_ERR, "init_configuration(): cannot make enough room for a new Profile (%s)\n",
            strerror(errno));
        return -1;
      }
      /* create governor string */
      if ((((struct profile *)n->content)->policy.governor = malloc(MAX_STRING_LEN*sizeof(char))) ==NULL) {
        cpufreqd_log(LOG_ERR, "init_configuration(): cannot make enough room for a new Profile governor (%s)\n",
            strerror(errno));
        return -1;
      }
      
      if ( parse_config_profile(fp_config, n->content) != -1) {
        /* checks duplicate names */
        for (np_iter=configuration.profiles.first; np_iter!=NULL; np_iter=np_iter->next) {
          if (strcmp(((struct profile *)np_iter->content)->name, ((struct profile *)n->content)->name) == 0) {
            cpufreqd_log(LOG_CRIT, 
                "init_configuration(): [Profile] name \"%s\" already exists.\n", 
                ((struct profile *)n->content)->name);
            node_free(n);
            fclose(fp_config);
            return -1;
          }
        }
        list_append(&configuration.profiles, n);
        
      } else {
        cpufreqd_log(LOG_CRIT, 
                      "init_configuration(): [Profile] error parsing %s, see logs for details.\n",
                      configuration.config_file);
        fclose(fp_config);
        return -1;
      }
      /* avoid continuing */
      continue;
    }

    /* if Rule scan rules options */
    if (strstr(clean,"[Rule]")) {
      
      n = node_new(NULL, sizeof(struct rule));
      if (n == NULL) {
        cpufreqd_log(LOG_ERR, "init_configuration(): cannot make enough room for a new Rule (%s)\n",
            strerror(errno));
        return -1;
      }
      
      if ( parse_config_rule(fp_config, n->content) != -1) {
        for (nr_iter=configuration.rules.first; nr_iter!=NULL; nr_iter=nr_iter->next) {
          /* check duplicate names */
          if (strcmp(((struct profile *)nr_iter->content)->name, ((struct profile *)n->content)->name) == 0) {
            cpufreqd_log(LOG_CRIT, 
                "init_configuration(): [Rule] name \"%s\" already exists.\n", 
                ((struct profile *)n->content)->name);
            node_free(n);
            return -1;
          }
        }
        list_append(&configuration.rules, n);
      } else {
        cpufreqd_log(LOG_CRIT, 
            "init_configuration(): [Rule] error parsing %s, see logs for details.\n", 
            configuration.config_file);
        return -1;
      }
      /* avoid continuing */
      continue;
    }
  }
  fclose(fp_config);

  /* did I read something? 
   * check if I read at least one rule, otherwise exit
   */
  if (configuration.rules.first == NULL) {
    cpufreqd_log(LOG_ERR, "init_configuration(): No rules found!\n");
    return -1;
  }
  
  /*
   * associate rules->profiles
   * go through rules and associate to the proper profile
   */
  for (nr_iter=configuration.rules.first; nr_iter!=NULL; nr_iter=nr_iter->next) {
    int profile_found = 0;

    for (np_iter=configuration.profiles.first; np_iter!=NULL; np_iter=np_iter->next) {
    /* go through profiles */
      if (strcmp(
            ((struct rule *)nr_iter->content)->profile_name, 
            ((struct profile *)np_iter->content)->name)==0) {
        /* a profile is allowed to be addressed by more than 1 rule */
        ((struct rule *)nr_iter->content)->prof = np_iter->content;
        profile_found = 1;
        break;
      }
    }

    if (!profile_found) {
      cpufreqd_log(LOG_CRIT, "init_configuration(): Syntax error: no Profile section found for Rule \"%s\" \
                        (requested Profile \"%s\")\n",
                       ((struct rule *)nr_iter->content)->name,
                       ((struct rule *)nr_iter->content)->profile_name);
      return -1;
    }
  }

  for (nr_iter=configuration.rules.first; nr_iter!=NULL; nr_iter=nr_iter->next) {
     cpufreqd_log(LOG_INFO, 
         "init_configuration(): Rule \"%s\" has Profile \"%s\"\n", 
         ((struct rule *)nr_iter->content)->name, ((struct rule *)nr_iter->content)->prof->name);
  }
  return 0;
}

/* 
 * Frees the structures allocated.
 */
void free_configuration(void) {
  struct NODE *nr_iter, *ent;
  
  /* cleanup rule entries */
  cpufreqd_log(LOG_INFO, "free_config(): freeing rules entries.\n");
  for (nr_iter=configuration.rules.first; nr_iter!=NULL; nr_iter=nr_iter->next) {
    for (ent=((struct rule *)nr_iter->content)->entries.first; ent!=NULL; ent=ent->next) {
      free(((struct rule_en *)ent->content)->obj);
    }
  }
  
  /* cleanup config structs */
  cpufreqd_log(LOG_INFO, "free_config(): freeing rules.\n"); 
  list_free_sublist(&(configuration.rules), configuration.rules.first);
  
  cpufreqd_log(LOG_INFO, "free_config(): freeing profiles.\n"); 
  list_free_sublist(&(configuration.profiles), configuration.profiles.first);
  
  cpufreqd_log(LOG_INFO, "free_config(): freeing plugins.\n"); 
  list_free_sublist(&(configuration.plugins), configuration.plugins.first);

  /* clean other values */
  configuration.poll_interval = DEFAULT_POLL;
  configuration.has_sysfs = 0;
  configuration.acpi_workaround = 0;
  configuration.cpu_min_freq = 0;
  configuration.cpu_max_freq = 0;
  
  if (!configuration.log_level_overridden)
    configuration.log_level = DEFAULT_VERBOSITY;
}

/*  int read_args (int argc, char *argv[])
 *  Reads command line arguments
 */
int read_args (int argc, char *argv[]) {
  
  static struct option long_options[] = {
    { "help", 0, 0, 'h' }, 
    { "version", 0, 0, 'v' }, 
    { "file", 1, 0, 'f' },
    { "no-daemon", 0, 0, 'D' },
    { "verbosity", 1, 0, 'V' },
    { 0, 0, 0, 0 }, 
  };
  int ch,option_index = 0;

  while ((ch = getopt_long(argc, argv, "hvf:DV:", long_options, &option_index)) != -1) {
    switch (ch) {
      case '?':
      case 'h':
        configuration.print_help = 1;
        return 0;
      case 'v':
        configuration.print_version = 1;
        return 0;
      case 'f':
        if (realpath(optarg, configuration.config_file) == NULL) {
          cpufreqd_log(LOG_ERR, "Error reading command line argument (%s: %s).\n", optarg, strerror(errno));
          return -1;
        }
        break;
      case 'D':
        configuration.no_daemon = 1;
        break;
      case 'V':
        configuration.log_level = atoi(optarg);
        if (configuration.log_level>7) {
          configuration.log_level = 7;
        } else if (configuration.log_level<0) {
          configuration.log_level = 0;
        }
        configuration.log_level_overridden = 1;
        break;
      default:
        break;
    }
  }
  return 0;
}

/*
 * Prints program version
 */
void print_version(const char *me) {
  printf("%s version "__CPUFREQD_VERSION__".\n", me);
  printf("Copyright 2002,2003,2004 Mattia Dongili <dongili@supereva.it>\n"
	    	 "                         George Staikos <staikos@0wned.org>\n");
}

/*  void print_help(const char *me)
 *  Prints program help
 */
void print_help(const char *me) {
  printf("Usage: %s [OPTION]...\n\n"
"  -h, --help                   display this help and exit\n"
"  -v, --version                display version information and exit\n"
"  -f, --file                   config file (default: "CPUFREQD_CONFIG")\n"
"  -D, --no-daemon              stay in foreground and print log to stdout (used to debug)\n"
"  -V, --verbosity              verbosity level from 0 (less verbose) to 7 (most verbose)\n"
"\n"
"Report bugs to Mattia Dongili <dongili@supereva.it>.\n", me);
}

void term_handler(int signo) {
  cpufreqd_log(LOG_NOTICE, "Caught TERM signal (%s), forcing exit.\n", strsignal(signo));
  force_exit = 1;
}

void int_handler(int signo) {
  cpufreqd_log(LOG_NOTICE, "Caught INT signal (%s), forcing exit.\n", strsignal(signo));
  force_exit = 1;
}

void hup_handler(int signo) {
  cpufreqd_log(LOG_NOTICE, "Caught HUP signal (%s), reloading configuration file.\n", strsignal(signo));
  force_reinit = 1;
}

/*
 *  main !
 *  Let's go
 */
int main (int argc, char *argv[]) {
  
  struct NODE *nd = NULL;
  struct NODE *n1 = NULL;
  struct sigaction signal_action;
  struct plugin_obj *o_plugin;
  struct rule_en *re;
  unsigned int i;
  int ret = 0;

  /* 
   *  check perms
   */
#if 0
  if (geteuid() != 0) {
    cpufreqd_log(LOG_CRIT, "%s: must be run as root.\n", argv[0]);
    ret = 1;
    goto out;
  }
#endif

  /*
   *  read_args
   */
  if (read_args(argc, argv)!=0) {
    cpufreqd_log(LOG_CRIT, "Unable parse command line parameters, exiting.\n");
    ret = 1;
    goto out;
  }
  if (configuration.print_help) {
    print_help(argv[0]);
    goto out;
  }
  if (configuration.print_version) {
    print_version(argv[0]);
    goto out;
  }

  /* setup signal handlers */
  sigemptyset(&signal_action.sa_mask);
  sigaddset(&signal_action.sa_mask, SIGTERM);
  sigaddset(&signal_action.sa_mask, SIGINT);
  sigaddset(&signal_action.sa_mask, SIGHUP);
  signal_action.sa_flags = 0;
                             
  signal_action.sa_handler = term_handler;
  sigaction(SIGTERM, &signal_action, 0);

  signal_action.sa_handler = int_handler;
  sigaction(SIGINT, &signal_action, 0);

  signal_action.sa_handler = hup_handler;
  sigaction(SIGHUP, &signal_action, 0);
 
  /*
   *  read how many cpus are available here
   */
  configuration.cpu_num = get_cpu_num();

  /*
   *  find cpufreq information about each cpu
   */
  if ((configuration.sys_info = malloc(configuration.cpu_num * sizeof(struct cpufreq_sys_info))) == NULL) {
    cpufreqd_log(LOG_CRIT, "Unable to allocate memory (%s), exiting.\n", strerror(errno));
    ret = 1;
    goto out;
  }
  for (i=0; i<configuration.cpu_num; i++) {
    (configuration.sys_info+i)->affected_cpus = cpufreq_get_affected_cpus(i);
    (configuration.sys_info+i)->governors = cpufreq_get_available_governors(i);
    (configuration.sys_info+i)->frequencies = cpufreq_get_available_frequencies(i);
  }

  /* SMP with different speed cpus awareness */
  if ((configuration.limits = malloc(configuration.cpu_num * sizeof(struct cpufreq_limits))) == NULL) {
    cpufreqd_log(LOG_CRIT, "Unable to allocate memory (%s), exiting.\n", strerror(errno));
    ret = 1;
    goto out_sys_info;
  }
  memset(configuration.limits, 0, configuration.cpu_num * sizeof(struct cpufreq_limits));
  for (i=0; i<configuration.cpu_num; i++) {
    /* if one of the probes fails remove all the others also */
    if (cpufreq_get_hardware_limits(i, &(configuration.limits->min), &(configuration.limits->max))!=0) {
      /* TODO: if libcpufreq fails try to read /proc/cpuinfo and warn about this not baing reliable */
      cpufreqd_log(LOG_WARNING, "Unable to get hardware frequency limits for CPU%d.\n", i);
      free(configuration.limits);
      configuration.limits = NULL;
      break;
    }
  }

  /*
   *  daemonize if necessary
   */
  if (configuration.no_daemon==0 && daemonize()!=0) {
    cpufreqd_log(LOG_CRIT, "Unable to go background, exiting.\n");
    ret = 1;
    goto out_limits;
  }
  
  /*
   *  1- open config file
   *  2- start reading
   *  3- look for general section first
   *
   *  4- load plugins
   *  
   *  5- parse rules
   *  6- parse profiles
   *
   *  7- check if rules/plugins are used
   *
   *  8- go! baby, go! (loop)
   */
  if (init_configuration() < 0) {
    cpufreqd_log(LOG_CRIT, "Unable to parse config file: %s\n", configuration.config_file);
    ret = 1;
    goto out_config_read;
  }

  /*  
   *  Clean up plugins if they don't have any associated rule entry
   */
  nd=configuration.plugins.first;
  while (nd != NULL) {
    o_plugin = (struct plugin_obj*)nd->content;
    if (o_plugin->used==0) {
      finalize_plugin((struct plugin_obj*)nd->content);
      close_plugin((struct plugin_obj*)nd->content);
      nd = list_remove_node(&(configuration.plugins), nd);
    } else {
      nd=nd->next;
    }
  }
  /* if no rules left exit.... */
  if (configuration.plugins.first == NULL) {
    cpufreqd_log(LOG_CRIT, "Hey! all the plugins I loaded are useless, maybe your configuration needs some rework.\n");
    cpufreqd_log(LOG_CRIT, "Exiting.\n");
    ret = 1;
    goto out_config_read;
  }
  
  /*
   *  Looooooooop
   */
  while (!force_exit) {

    /* update plugin states */
    for (nd=configuration.plugins.first; nd!=NULL; nd=nd->next) {
      o_plugin = (struct plugin_obj*)nd->content;
      if (o_plugin!=NULL && o_plugin->used>0) {
        o_plugin->plugin->plugin_update();
      }
    }

    /* got objects and config now test it, call plugin->eval for each rule */
    /* O(rules*entries) */
    for (nd=configuration.rules.first; nd!=NULL; nd=nd->next) {
      for (n1=((struct rule*)nd->content)->entries.first; n1!=NULL; n1=n1->next) {
        re = (struct rule_en *)n1->content;

        /* compute scores for rules and keep the highest */
        if (re->eval(re->obj) == MATCH) {
          ((struct rule*)nd->content)->score++;
          cpufreqd_log(LOG_INFO, "Rule %s matches entry.\n", ((struct rule*)nd->content)->name);
        }
      }
      cpufreqd_log(LOG_INFO, "Rule %s score: %d\n", ((struct rule*)nd->content)->name, 
          ((struct rule*)nd->content)->score++);
      /* reset the score for the next run */
      ((struct rule*)nd->content)->score = 0;
    }
    /* TODO: set the policy associated with the highest score */
    /* ... */

    sleep(configuration.poll_interval);
  }
  
  /*
   *  Unload plugins
   */
  for (nd=configuration.plugins.first; nd!=NULL; nd=nd->next) {
    finalize_plugin((struct plugin_obj*)nd->content);
    close_plugin((struct plugin_obj*)nd->content);
  }

  /*
   *  Free configuration structures
   */
out_config_read:
  free_configuration();

out_limits:
  if (configuration.limits != NULL)
    free(configuration.limits);
  
out_sys_info:
  if (configuration.sys_info != NULL) {
    for (i=0; i<configuration.cpu_num; i++) {
      if ((configuration.sys_info+i)->governors!=NULL)
        cpufreq_put_available_governors((configuration.sys_info+i)->governors);
      if ((configuration.sys_info+i)->affected_cpus!=NULL)
        cpufreq_put_affected_cpus((configuration.sys_info+i)->affected_cpus);
      if ((configuration.sys_info+i)->frequencies!=NULL)
        cpufreq_put_available_frequencies((configuration.sys_info+i)->frequencies);
    }
    free(configuration.sys_info);
  }
  
  /*
   *  bye bye  
   */
out:
  return ret;
}
