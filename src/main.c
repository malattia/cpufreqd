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

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cpufreq.h>
#include <string.h>
#include "main.h"
#include "cpufreqd.h"
#include "cpufreqd_plugin.h"
#include "cpufreqd_remote.h"
#include "daemon_utils.h"
#include "plugin_utils.h"
#include "config_parser.h"
#include "cpufreqd_log.h"
#include "cpufreq_utils.h"

/* default configuration */
struct cpufreqd_conf configuration = {
	.config_file		= CPUFREQD_CONFDIR"cpufreqd.conf",
	.pidfile		= CPUFREQD_STATEDIR"cpufreqd.pid",
	.sockfile		= "/tmp/cpufreqd.sock",
	.cpu_num		= 1,
	.poll_interval		= DEFAULT_POLL,
	.has_sysfs		= 1,
	.no_daemon		= 0,
	.log_level_overridden	= 0,
	.log_level		= DEFAULT_VERBOSITY,
	.enable_remote		= 0,
	.print_help		= 0,
	.print_version		= 0
};

static int force_reinit = 0;
static int force_exit = 0;

/* intialize the cpufreqd_conf object 
 * by reading the configuration file
 */
static int init_configuration(void) {
	FILE *fp_config;
	struct NODE *n, *np_iter, *nr_iter;
	struct profile *tmp_profile;
	struct rule *tmp_rule;
	char buf[256];

	/* configuration file */
	cpufreqd_log(LOG_INFO, "init_configuration(): reading configuration file %s\n",
			configuration.config_file);
	fp_config = fopen(configuration.config_file, "r");
	if (!fp_config) {
		cpufreqd_log(LOG_ERR, "init_configuration(): %s: %s\n",
				configuration.config_file, strerror(errno));
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
				fclose(fp_config);
				return -1;
			}
			/*
			 *  Load plugins
			 *  just after having read the General section
			 *  and before the rest in order to be able to hadle
			 *  options with them.
			 */
			load_plugin_list(&configuration.plugins);
			continue;
		}

		/* if Profile scan profile options */
		if (strstr(clean,"[Profile]")) {

			n = node_new(NULL, sizeof(struct profile));
			if (n == NULL) {
				cpufreqd_log(LOG_ERR, "init_configuration(): cannot make "
						"enough room for a new Profile (%s)\n",
						strerror(errno));
				fclose(fp_config);
				return -1;
			}
			/* create governor string */
			tmp_profile = (struct profile *)n->content;
			if ((tmp_profile->policy.governor = malloc(MAX_STRING_LEN*sizeof(char))) ==NULL) {
				cpufreqd_log(LOG_ERR, "init_configuration(): cannot make enough room "
						"for a new Profile governor (%s)\n",
						strerror(errno));
				node_free(n);
				fclose(fp_config);
				return -1;
			}

			if ( parse_config_profile(fp_config, tmp_profile) != -1) {
				/* checks duplicate names */
				for (np_iter=configuration.profiles.first; np_iter!=NULL; np_iter=np_iter->next) {
					if (strcmp(((struct profile *)np_iter->content)->name, tmp_profile->name) == 0) {
						cpufreqd_log(LOG_CRIT, 
								"init_configuration(): [Profile] "
								"name \"%s\" already exists.\n", 
								tmp_profile->name);
						node_free(n);
						fclose(fp_config);
						return -1;
					}
				}
				list_append(&configuration.profiles, n);

			} else {
				cpufreqd_log(LOG_CRIT, 
						"init_configuration(): [Profile] "
						"error parsing %s, see logs for details.\n",
						configuration.config_file);
				node_free(n);
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
				fclose(fp_config);
				return -1;
			}

			tmp_rule = (struct rule *)n->content;
			if ( parse_config_rule(fp_config, tmp_rule) != -1) {
				for (nr_iter=configuration.rules.first; nr_iter!=NULL; nr_iter=nr_iter->next) {
					/* check duplicate names */
					if (strcmp(((struct rule *)nr_iter->content)->name, tmp_rule->name) == 0) {
						cpufreqd_log(LOG_CRIT, 
								"init_configuration(): [Rule] name \"%s\" already exists.\n", 
								tmp_rule->name);
						node_free(n);
						fclose(fp_config);
						return -1;
					}
				}
				/* check if there are options */
				if (tmp_rule->entries.first == NULL) {
					cpufreqd_log(LOG_CRIT, 
							"init_configuration(): [Rule] name \"%s\" has no options, discarding.\n", 
							tmp_rule->name);
					node_free(n);
					continue;
				}
				list_append(&configuration.rules, n);
			} else {
				cpufreqd_log(LOG_CRIT, 
						"init_configuration(): [Rule] error parsing %s, see logs for details.\n", 
						configuration.config_file);
				node_free(n);
				fclose(fp_config);
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
		tmp_rule = (struct rule *)nr_iter->content;
		int profile_found = 0;

		for (np_iter=configuration.profiles.first; np_iter!=NULL; np_iter=np_iter->next) {
			tmp_profile = (struct profile *)np_iter->content;
			/* go through profiles */
			if (strcmp(tmp_rule->profile_name, tmp_profile->name)==0) {
				/* a profile is allowed to be addressed by more than 1 rule */
				((struct rule *)nr_iter->content)->prof = np_iter->content;
				profile_found = 1;
				break;
			}
		}

		if (!profile_found) {
			cpufreqd_log(LOG_CRIT, "init_configuration(): Syntax error: no Profile section found for Rule \"%s\" \
					(requested Profile \"%s\")\n", tmp_rule->name, tmp_rule->profile_name);
			return -1;
		}
	}

	for (nr_iter=configuration.rules.first; nr_iter!=NULL; nr_iter=nr_iter->next) {
		tmp_rule = (struct rule *)nr_iter->content;
		cpufreqd_log(LOG_INFO, 
				"init_configuration(): Rule \"%s\" has Profile \"%s\"\n", 
				tmp_rule->name, tmp_rule->prof->name);
	}
	return 0;
}

/* 
 * Frees the structures allocated.
 */
static void free_configuration(void)
{
	struct NODE *n_iter, *ent;
	struct rule *tmp_rule;
	struct rule_en *tmp_rule_en;
	struct profile *tmp_profile;
	struct plugin_obj *o_plugin;

	/* cleanup rule entries */
	cpufreqd_log(LOG_INFO, "free_config(): freeing rules entries.\n");
	for (n_iter=configuration.rules.first; n_iter!=NULL; n_iter=n_iter->next) {
		tmp_rule = (struct rule *)n_iter->content;

		for (ent=tmp_rule->entries.first; ent!=NULL; ent=ent->next) {
			tmp_rule_en = (struct rule_en *)ent->content;
			if (tmp_rule_en->keyword->free != NULL)
				tmp_rule_en->keyword->free(tmp_rule_en->obj);
			else 
				free(tmp_rule_en->obj);
		}
		list_free_sublist(&tmp_rule->entries, tmp_rule->entries.first);
	}

	/* cleanup config structs */
	cpufreqd_log(LOG_INFO, "free_config(): freeing rules.\n"); 
	list_free_sublist(&(configuration.rules), configuration.rules.first);

	cpufreqd_log(LOG_INFO, "free_config(): freeing profiles.\n"); 
	/* free policy governors string */
	for (n_iter=configuration.profiles.first; n_iter!=NULL; n_iter=n_iter->next) {
		tmp_profile = (struct profile *)n_iter->content;
		free(tmp_profile->policy.governor);
	}
	list_free_sublist(&(configuration.profiles), configuration.profiles.first);

	/* clean other values */
	configuration.poll_interval = DEFAULT_POLL;
	configuration.has_sysfs = 0;
	configuration.enable_remote = 0;
	configuration.cpu_min_freq = 0;
	configuration.cpu_max_freq = 0;

	if (!configuration.log_level_overridden)
		configuration.log_level = DEFAULT_VERBOSITY;

	/* finalize plugins!!!! */
	/*
	 *  Unload plugins
	 */
	cpufreqd_log(LOG_INFO, "free_config(): freeing plugins.\n"); 
	for (n_iter=configuration.plugins.first; n_iter!=NULL; n_iter=n_iter->next) {
		o_plugin = (struct plugin_obj*)n_iter->content;
		finalize_plugin(o_plugin);
		close_plugin(o_plugin);
	}
	list_free_sublist(&(configuration.plugins), configuration.plugins.first);
}

/*  struct rule *update_rule_scores(struct LIST *rules)
 *  Updates rules score and return the one with the best
 *  one or NULL if every rule has a 0% score.
 */
static struct rule *update_rule_scores(struct LIST *rules) {
	struct NODE *nd = NULL;
	struct NODE *nd1 = NULL;
	struct rule *tmp_rule = NULL;
	struct rule *ret = NULL;
	struct rule_en *re = NULL;
	int entries_count = 0;
	unsigned int best_score = 0;

	/* call plugin->eval for each rule */
	/* O(rules*entries) */
	for (nd=rules->first; nd!=NULL; nd=nd->next) {
		tmp_rule = (struct rule*)nd->content;
		
		/* reset the score before counting */
		tmp_rule->score = 0;
		entries_count = 0;
		cpufreqd_log(LOG_DEBUG, "Considering Rule \"%s\"\n", tmp_rule->name);

		for (nd1=tmp_rule->entries.first; nd1!=NULL; nd1=nd1->next) {
			re = (struct rule_en *)nd1->content;
			entries_count++;
			/* compute scores for rules and keep the highest */
			if (re->keyword->evaluate != NULL && re->keyword->evaluate(re->obj) == MATCH) {
				tmp_rule->score++;
				cpufreqd_log(LOG_DEBUG, "Rule \"%s\": entry matches.\n", tmp_rule->name);
			}
		} /* end foreach rule entry */

		/* calculate score on a percentage base 
		 * so that a single entry rule might be the best match
		 */
		tmp_rule->score = tmp_rule->score + (100 * tmp_rule->score / entries_count);
		if (tmp_rule->score > best_score) {
			ret = tmp_rule;
			best_score = tmp_rule->score;
		}

		cpufreqd_log(LOG_INFO, "Rule \"%s\" score: %d%%\n", tmp_rule->name,
				tmp_rule->score+(100*tmp_rule->score)/entries_count);
	} /* end foreach rule */
	return ret;
}

/*  int read_args (int argc, char *argv[])
 *  Reads command line arguments
 */
static int read_args (int argc, char *argv[]) {

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
			cpufreqd_log(LOG_DEBUG, "Using configuration file at %s\n", configuration.config_file);
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
static void print_version(const char *me) {
	printf("%s version "__CPUFREQD_VERSION__".\n", me);
	printf("Copyright 2002,2003,2004 Mattia Dongili <malattia@gmail.com>\n"
			"                         George Staikos <staikos@0wned.org>\n");
}

/*  void print_help(const char *me)
 *  Prints program help
 */
static void print_help(const char *me) {
	printf("Usage: %s [OPTION]...\n\n"
			"  -h, --help                   display this help and exit\n"
			"  -v, --version                display version information and exit\n"
			"  -f, --file                   config file (default: "CPUFREQD_CONFIG")\n"
			"  -D, --no-daemon              stay in foreground and print log to stdout (used to debug)\n"
			"  -V, --verbosity              verbosity level from 0 (less verbose) to 7 (most verbose)\n"
			"\n"
			"Report bugs to Mattia Dongili <malattia@gmail.com>.\n", me);
}

static void term_handler(int signo) {
	cpufreqd_log(LOG_NOTICE, "Caught TERM signal (%s), forcing exit.\n", strsignal(signo));
	force_exit = 1;
}

static void int_handler(int signo) {
	cpufreqd_log(LOG_NOTICE, "Caught INT signal (%s), forcing exit.\n", strsignal(signo));
	force_exit = 1;
}

static void hup_handler(int signo) {
	cpufreqd_log(LOG_NOTICE, "Caught HUP signal (%s), ignored.\n", strsignal(signo));
#if 0
	cpufreqd_log(LOG_NOTICE, "Caught HUP signal (%s), reloading configuration file.\n", strsignal(signo));
	force_reinit = 1;
#endif
}

static void pipe_handler(int signo) {
  cpufreqd_log(LOG_NOTICE, "Caught PIPE signal (%s).\n", strsignal(signo));
}

/*
 *  main !
 *  Let's go
 */
int main (int argc, char *argv[]) {

	struct sigaction signal_action;
	struct pollfd fds;
	struct sockaddr_un cpufreqd_sa;

	struct NODE *nd = NULL;
	struct NODE *n1 = NULL;
	struct plugin_obj *o_plugin = NULL;
	struct rule *current_rule = NULL, *tmp_rule = NULL, *best_rule = NULL;
	struct profile *current_profile = NULL, *tmp_profile = NULL;
	struct rule_en *re = NULL;
	unsigned int i = 0, tmp_score = 0;
	int cpufreqd_sock = -1; /* input pipe */
	int manual_mode = 0; /* manual_control */
	char remote_command[MAX_CMD_BUF];
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
	sigaddset(&signal_action.sa_mask, SIGPIPE);
	signal_action.sa_flags = 0;

	signal_action.sa_handler = term_handler;
	sigaction(SIGTERM, &signal_action, 0);

	signal_action.sa_handler = int_handler;
	sigaction(SIGINT, &signal_action, 0);

	signal_action.sa_handler = hup_handler;
	sigaction(SIGHUP, &signal_action, 0);

	signal_action.sa_handler = pipe_handler;
	sigaction(SIGPIPE, &signal_action, 0);

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

	/* SMP: with different speed cpus */
	if ((configuration.limits = malloc(configuration.cpu_num * sizeof(struct cpufreq_limits))) == NULL) {
		cpufreqd_log(LOG_CRIT, "Unable to allocate memory (%s), exiting.\n", strerror(errno));
		ret = 1;
		goto out_sys_info;
	}
	memset(configuration.limits, 0, configuration.cpu_num * sizeof(struct cpufreq_limits));
	for (i=0; i<configuration.cpu_num; i++) {
		/* if one of the probes fails remove all the others also */
		if (cpufreq_get_hardware_limits(i, &((configuration.limits+i)->min), &((configuration.limits+i)->max))!=0) {
			/* TODO: if libcpufreq fails try to read /proc/cpuinfo and warn about this not being reliable */
			cpufreqd_log(LOG_WARNING, "Unable to get hardware frequency limits for CPU%d.\n", i);
			free(configuration.limits);
			configuration.limits = NULL;
			break;
		} else {
			cpufreqd_log(LOG_INFO, "Limits for cpu%d: MIN=%lu - MAX=%lu\n", i, 
					(configuration.limits+i)->min, (configuration.limits+i)->max);
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

cpufreqd_start:
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

	/* write pidfile */
	if (write_cpufreqd_pid(configuration.pidfile) < 0) {
		cpufreqd_log(LOG_CRIT, "Unable to write pid file: %s\n", configuration.pidfile);
		ret = 1;
		goto out_config_read;
	}

	/* setup UNIX socket if necessary */
	if (configuration.enable_remote) {
		cpufreqd_sa.sun_family = AF_UNIX;
		strncpy(cpufreqd_sa.sun_path, configuration.sockfile, MAX_PATH_LEN);
		if ((cpufreqd_sock = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
			cpufreqd_log(LOG_ALERT, "socket(): %s.\n", strerror(errno));
			cpufreqd_sock = -1;
		} else if (bind(cpufreqd_sock, &cpufreqd_sa, sizeof(cpufreqd_sa)) == -1) {
			cpufreqd_log(LOG_ALERT, "bind(): %s.\n", strerror(errno));
			close(cpufreqd_sock);
			cpufreqd_sock = -1;
		} else if (fcntl(cpufreqd_sock, F_SETFL, O_NONBLOCK) == -1) {
			cpufreqd_log(LOG_ALERT, "fcntl(): %s.\n", strerror(errno));
			close(cpufreqd_sock);
			cpufreqd_sock = -1;
		} else if (listen(cpufreqd_sock, 5) == -1) {
			cpufreqd_log(LOG_ALERT, "listen(): %s.\n", strerror(errno));
			close(cpufreqd_sock);
			cpufreqd_sock = -1;
		}
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
		cpufreqd_log(LOG_CRIT, "Hey! all the plugins I loaded are useless, "
				"maybe your configuration needs some rework.\n");
		cpufreqd_log(LOG_CRIT, "Exiting.\n");
		ret = 1;
		goto out_config_read;
	}

	fds.fd = cpufreqd_sock;
	fds.events = POLLIN | POLLRDNORM;
	remote_command[0] = '\0';
	
	/*
	 *  Looooooooop
	 */
	while (!force_exit && !force_reinit) {
		best_rule = NULL;
		tmp_profile = NULL;
		tmp_score = 0;

		update_plugin_states(&configuration.plugins);

		best_rule = update_rule_scores(&configuration.rules);

		/* set the policy associated with the highest score */
		if (best_rule == NULL) {
			cpufreqd_log(LOG_WARNING, "No Rule matches current system status.\n");

		/* rule changed */
		} else if (current_rule != best_rule) {
			/* pre change event */
			cpufreqd_log(LOG_DEBUG, "Triggering pre-change event\n");
			for (n1=best_rule->entries.first; n1!=NULL; n1=n1->next) {
				re = (struct rule_en *)n1->content;
				if (re->keyword->pre_change != NULL) {
					re->keyword->pre_change(re->obj, 
							&current_rule->prof->policy,
							&tmp_rule->prof->policy);
				}
			} /* end foreach rule entry */

			/* change frequency */
			if (tmp_profile != current_profile) {
				cpufreqd_set_profile(tmp_profile);
			} else {
				cpufreqd_log(LOG_DEBUG, "Profile unchanged (\"%s\"-\"%s\"), doing nothing.\n", 
						current_profile->name, tmp_profile->name);
			}

			/* post change event */
			cpufreqd_log(LOG_DEBUG, "Triggering post-change event\n");
			for (n1=best_rule->entries.first; n1!=NULL; n1=n1->next) {
				re = (struct rule_en *)n1->content;
				if (re->keyword->post_change != NULL) {
					re->keyword->post_change(re->obj, 
							&current_rule->prof->policy,
							&tmp_rule->prof->policy);
				}
			} /* end foreach rule entry */
			current_rule = best_rule;
			current_profile = tmp_profile;

		/* nothing new happened */
		} else {
			cpufreqd_log(LOG_DEBUG, "Rule unchanged (\"%s\"), doing nothing.\n", 
					current_rule->name);
		}

		sleep(configuration.poll_interval);
	}

	/*
	 * Clean pidfile
	 */
	clear_cpufreqd_pid(configuration.pidfile);

	/* close socket */
	if (cpufreqd_sock != -1)
		close(cpufreqd_sock);

	/*
	 *  Free configuration structures
	 */
out_config_read:
	free_configuration();
	if (force_reinit && !force_exit) {
		force_reinit = 0;
		cpufreqd_log(LOG_INFO, "Restarting cpufreqd\n");
		goto cpufreqd_start;
	}

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
