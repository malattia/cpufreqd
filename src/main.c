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

#include <cpufreq.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include "config_parser.h"
#include "cpufreq_utils.h"
#include "cpufreqd.h"
#include "cpufreqd_log.h"
#include "cpufreqd_plugin.h"
#include "cpufreqd_remote.h"
#include "daemon_utils.h"
#include "list.h"
#include "main.h"
#include "plugin_utils.h"
#include "sock_utils.h"

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
static int timer_expired = 1; /* expired in order to run on the first loop */
static int cpufreqd_mode = ARG_DYNAMIC; /* operation mode (manual / dynamic) */

/* intialize the cpufreqd_conf object 
 * by reading the configuration file
 */
static int init_configuration(void) {
	FILE *fp_config;
	struct NODE *n;
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

			if (parse_config_profile(fp_config, tmp_profile) < 0) {
				cpufreqd_log(LOG_CRIT, 
						"init_configuration(): [Profile] "
						"error parsing %s, see logs for details.\n",
						configuration.config_file);
				node_free(n);
				fclose(fp_config);
				return -1;
			}
			/* checks duplicate names */
			LIST_FOREACH_NODE(node, &configuration.profiles) {
				struct profile *tmp = (struct profile *)node->content;
				if (strcmp(tmp->name, tmp_profile->name) != 0)
					continue;
				cpufreqd_log(LOG_CRIT, 
						"init_configuration(): [Profile] "
						"name \"%s\" already exists.\n", 
						tmp_profile->name);
				node_free(n);
			}
			list_append(&configuration.profiles, n);
			/* avoid continuing */
			continue;
		}

		/* if Rule scan rules options */
		if (strstr(clean,"[Rule]")) {

			n = node_new(NULL, sizeof(struct rule));
			if (n == NULL) {
				cpufreqd_log(LOG_ERR, "init_configuration(): "
						"cannot make enough room for a new Rule (%s)\n",
						strerror(errno));
				fclose(fp_config);
				return -1;
			}
			tmp_rule = (struct rule *)n->content;
			if (parse_config_rule(fp_config, tmp_rule) < 0) {
				cpufreqd_log(LOG_CRIT, 
						"init_configuration(): [Rule] "
						"error parsing %s, see logs for details.\n", 
						configuration.config_file);
				node_free(n);
				fclose(fp_config);
				return -1;
			}
				
			/* check duplicate names */
			LIST_FOREACH_NODE(node, &configuration.rules) {
				struct rule *tmp = (struct rule *)node->content;
				if (strcmp(tmp->name, tmp_rule->name) != 0)
					continue;
				
				cpufreqd_log(LOG_ERR, 
						"init_configuration(): [Rule] "
						"name \"%s\" already exists. Skipped\n", 
						tmp_rule->name);
				node_free(n);
			}
			/* check if there are options */
			if (tmp_rule->directives.first == NULL) {
				cpufreqd_log(LOG_CRIT, 
						"init_configuration(): [Rule] "
						"name \"%s\" has no options, discarding.\n",
						tmp_rule->name);
				node_free(n);
				continue;
			}
			list_append(&configuration.rules, n);
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
	LIST_FOREACH_NODE(node, &configuration.rules) {
	/*for (nr_iter=configuration.rules.first; nr_iter!=NULL; nr_iter=nr_iter->next) {*/
		tmp_rule = (struct rule *)node->content;
		int profile_found = 0;

		LIST_FOREACH_NODE(node1, &configuration.profiles) {
		/*for (np_iter=configuration.profiles.first; np_iter!=NULL; np_iter=np_iter->next) {*/
			tmp_profile = (struct profile *)node1->content;
			/* go through profiles */
			if (strcmp(tmp_rule->profile_name, tmp_profile->name)==0) {
				/* a profile is allowed to be addressed by more than 1 rule */
				tmp_rule->prof = tmp_profile;
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

	LIST_FOREACH_NODE(node, &configuration.rules) {
		tmp_rule = (struct rule *)node->content;
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
	struct rule *tmp_rule;
	struct directive *tmp_directive;
	struct profile *tmp_profile;
	struct plugin_obj *o_plugin;

	/* cleanup rule directives */
	cpufreqd_log(LOG_INFO, "free_config(): freeing rules directives.\n");
	LIST_FOREACH_NODE(node, &configuration.rules) {
		tmp_rule = (struct rule *) node->content;

		LIST_FOREACH_NODE(node1, &tmp_rule->directives) {
			tmp_directive = (struct directive *) node1->content;
			free_keyword_object(tmp_directive->keyword, tmp_directive->obj);
		}
		list_free_sublist(&tmp_rule->directives, tmp_rule->directives.first);
	}

	/* cleanup config structs */
	cpufreqd_log(LOG_INFO, "free_config(): freeing rules.\n"); 
	list_free_sublist(&(configuration.rules), configuration.rules.first);

	cpufreqd_log(LOG_INFO, "free_config(): freeing profiles.\n"); 
	/* free policy governors string */
	LIST_FOREACH_NODE(node, &configuration.profiles) {
		tmp_profile = (struct profile *)node->content;
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
	LIST_FOREACH_NODE(node, &configuration.plugins) {
		o_plugin = (struct plugin_obj*) node->content;
		finalize_plugin(o_plugin);
		close_plugin(o_plugin);
	}
	list_free_sublist(&(configuration.plugins), configuration.plugins.first);
}

/*
 * Evaluates the full rule and returns the percentage score
 * for it.
 */
static unsigned int rule_score(struct rule *rule) {
	unsigned int hits = 0;
	struct directive *d = NULL;
	
	/* call plugin->evaluate for each rule */
	LIST_FOREACH_NODE(node, &rule->directives) {
		d = (struct directive *) node->content;
		/* compute scores for rules and keep the highest */
		if (d->keyword->evaluate != NULL && d->keyword->evaluate(d->obj) == MATCH) {
			hits++;
			cpufreqd_log(LOG_DEBUG, "Rule \"%s\": %s matches.\n", rule->name,
					d->keyword->word);
		}
	} /* end foreach rule entry */
	return hits + (100 * hits / rule->directives_count);
}

/*  struct rule *update_rule_scores(struct LIST *rules)
 *  Updates rules score and return the one with the best
 *  one or NULL if every rule has a 0% score.
 */
static struct rule *update_rule_scores(struct LIST *rule_list) {
	struct rule *tmp_rule = NULL;
	struct rule *ret = NULL;
	unsigned int best_score = 0, tmp_score = 0;

	LIST_FOREACH_NODE(node, rule_list) {
		tmp_rule = (struct rule *)node->content;
		
		cpufreqd_log(LOG_DEBUG, "Considering Rule \"%s\"\n", tmp_rule->name);
		tmp_score = rule_score(tmp_rule);

		if (tmp_score > best_score) {
			ret = tmp_rule;
			best_score = tmp_score;
		}
		cpufreqd_log(LOG_INFO, "Rule \"%s\" score: %d%%\n", tmp_rule->name, tmp_score);
	} /* end foreach rule */
	return ret;
}

/* Invoke pre_change for each rul entry
 */
static void rule_prechange_event(const struct rule *rule, const struct cpufreq_policy *old,
		const struct cpufreq_policy *new) {
	struct directive *d;
	cpufreqd_log(LOG_DEBUG, "Triggering pre-change event\n");
	LIST_FOREACH_NODE(node, &rule->directives) {
		d = (struct directive *)node->content;
		if (d->keyword->pre_change != NULL) {
			d->keyword->pre_change(d->obj, old, new);
		}
	} /* end foreach rule entry */
}

/* Invoke post_change for each rul entry
 */
static void rule_postchange_event(const struct rule *rule, const struct cpufreq_policy *old,
		const struct cpufreq_policy *new) {
	struct directive *d;
	cpufreqd_log(LOG_DEBUG, "Triggering post-change event\n");
	LIST_FOREACH_NODE(node, &rule->directives) {
		d = (struct directive *)node->content;
		if (d->keyword->post_change != NULL) {
			d->keyword->post_change(d->obj, old, new);
		}
	} /* end foreach rule entry */
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

static void alarm_handler(int signo) {
	cpufreqd_log(LOG_DEBUG, "Caught ALARM signal (%s).\n", strsignal(signo));
	timer_expired = 1;
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

static struct rule *cpufreqd_loop(struct cpufreqd_conf *conf, struct rule *current) {
	struct rule *best_rule = NULL;
	
	update_plugin_states(&conf->plugins);
	best_rule = update_rule_scores(&conf->rules);

	/* set the policy associated with the highest score */
	if (best_rule == NULL) {
		cpufreqd_log(LOG_WARNING, "No Rule matches current system status.\n");

		/* rule changed */
	} else if (current != best_rule) {
		cpufreqd_log(LOG_DEBUG, "New Rule (\"%s\"), applying.\n", 
				best_rule->name);
		/* pre change event */
		if (current != NULL) {
			rule_prechange_event(best_rule, &current->prof->policy,
					&best_rule->prof->policy);
		}

		/* change frequency */
		if (current == NULL || best_rule->prof != current->prof) {
			cpufreqd_set_profile(best_rule->prof);
		} else {
			cpufreqd_log(LOG_DEBUG, "Profile unchanged (\"%s\"-\"%s\"), doing nothing.\n", 
					current->prof->name, best_rule->prof->name);
		}

		/* post change event */
		if (current != NULL) {
			rule_postchange_event(best_rule, &current->prof->policy,
					&best_rule->prof->policy);
		}

		/* nothing new happened */
	} else {
		cpufreqd_log(LOG_DEBUG, "Rule unchanged (\"%s\"), doing nothing.\n", 
				current->name);
	}
	return best_rule;
}

/*
 * Parse and execute the client command
 */
static void execute_command(int sock) {
	struct pollfd fds;
	uint32_t command = INVALID_CMD;
	/* we have a valid sock, wait for command
	 * don't wait more tha 0.5 sec
	 */
	fds.fd = sock;
	fds.events = POLLIN | POLLRDNORM;

	if (poll(&fds, 1, 500) != 1) {
		cpufreqd_log(LOG_ALERT, "Wiated too long for data, aborting.\n");

	} else if (read(sock, &command, sizeof(uint32_t)) == -1) {
		cpufreqd_log(LOG_ALERT, "process_packet - read(): %s\n", strerror(errno));

	} else if (command != INVALID_CMD) {
		cpufreqd_log(LOG_INFO, "command received: %0.4x %0.4x\n",
				REMOTE_CMD(command), REMOTE_ARG(command));
		switch (REMOTE_CMD(command)) {
			case CMD_UPDATE_STATE:
				cpufreqd_log(LOG_DEBUG, "CMD_UPDATE_STATE\n");
				break;
			case CMD_LIST_RULES:
				cpufreqd_log(LOG_DEBUG, "CMD_LIST_RULES\n");
				break;
			case CMD_LIST_PROFILES:
				cpufreqd_log(LOG_DEBUG, "CMD_LIST_PROFILES\n");
				break;
			case CMD_SET_RULE:
				cpufreqd_log(LOG_DEBUG, "CMD_SET_RULE\n");
				break;
			case CMD_SET_MODE:
				cpufreqd_log(LOG_DEBUG, "CMD_SET_MODE\n");
				cpufreqd_mode = REMOTE_ARG(command);
				break;
			case CMD_SET_PROFILE:
				cpufreqd_log(LOG_DEBUG, "CMD_SET_PROFILE\n");
				break;
			default:
				cpufreqd_log(LOG_ALERT,
						"Unable to process packet: %d\n",
						command);
				break;
		}
	}
}

/*
 *  main !
 *  Let's go
 */
int main (int argc, char *argv[]) {

	struct sigaction signal_action;
	struct pollfd fds;
	struct itimerval new_timer;

	struct rule *current_rule = NULL;
	unsigned int i = 0;
	int cpufreqd_sock = -1, peer_sock = -1; /* input pipe */
	char dirname[MAX_PATH_LEN];
	int ret = 0;

	/* 
	 *  check perms
	 */
#if 1
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
	sigaddset(&signal_action.sa_mask, SIGALRM);
	sigaddset(&signal_action.sa_mask, SIGPIPE);
	signal_action.sa_flags = 0;

	signal_action.sa_handler = term_handler;
	sigaction(SIGTERM, &signal_action, 0);

	signal_action.sa_handler = int_handler;
	sigaction(SIGINT, &signal_action, 0);

	signal_action.sa_handler = hup_handler;
	sigaction(SIGHUP, &signal_action, 0);

	signal_action.sa_handler = alarm_handler;
	sigaction(SIGALRM, &signal_action, 0);

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

	/* setup UNIX socket if necessary */
	if (configuration.enable_remote) {
		dirname[0] = '\0';
		if (create_temp_dir(dirname) == NULL) {
			cpufreqd_log(LOG_ERR, "Couldn't create temporary directory %s\n", dirname);
			cpufreqd_sock = -1;
		} else if ((cpufreqd_sock = open_unix_sock(dirname)) == -1) {
			delete_temp_dir(dirname);
			cpufreqd_log(LOG_ERR, "Couldn't open socket, remote controls disabled\n");
		} else {
			cpufreqd_log(LOG_INFO, "Remote controls enabled\n");
		}
	}

	/* Validate plugins, if no rules left exit.... */
	if (validate_plugins(&configuration.plugins) == 0) {
		cpufreqd_log(LOG_CRIT, "Hey! all the plugins I loaded are useless, "
				"maybe your configuration needs some rework.\n");
		cpufreqd_log(LOG_CRIT, "Exiting.\n");
		ret = 1;
		goto out_socket;
	}

	/* write pidfile */
	if (write_cpufreqd_pid(configuration.pidfile) < 0) {
		cpufreqd_log(LOG_CRIT, "Unable to write pid file: %s\n", configuration.pidfile);
		ret = 1;
		goto out_pid;
	}

	/*
	 *  Looooooooop
	 */
	while (!force_exit && !force_reinit) {
		/*
		 * Set timer and run the system scan and rule selection
		 * if running in DYNAMIC mode
		 * Also check if we still need to expire the timer 
		 * (happens in case a command has been received
		 * before the timer expires)
		 */
		if (cpufreqd_mode == ARG_DYNAMIC && timer_expired) {
			new_timer.it_interval.tv_usec = 0;
			new_timer.it_interval.tv_sec = 0;
			new_timer.it_value.tv_usec = 0;
			new_timer.it_value.tv_sec = configuration.poll_interval;
			if (setitimer(ITIMER_REAL, &new_timer, 0) < 0) {
				cpufreqd_log(LOG_CRIT, "Couldn't set timer: %s\n", strerror(errno));
				ret = 1;
				break;
			}
			current_rule = cpufreqd_loop(&configuration, current_rule);
			timer_expired = 0;
		}

		/* if the socket opened successfully */
		if (cpufreqd_sock != -1) {
			/* wait either for a command */
			fds.fd = cpufreqd_sock;
			fds.events = POLLIN | POLLRDNORM;
			switch (poll(&fds, 1, -1)) {
				case 0:
					/* timed out. check to see if things have changed */
					break;
				case -1:
					/* caused by SIGALARM (mostly) */
					if (errno != EINTR)
						cpufreqd_log(LOG_NOTICE, "poll(): %s.\n", strerror(errno));
					break;
				case 1:
					/* somebody tried to contact us. see what he wants */
					peer_sock = accept(cpufreqd_sock, NULL, 0);
					if (peer_sock == -1) {
						cpufreqd_log(LOG_ALERT, "Unable to accept connection: "
								" %s\n", strerror(errno));
					}
					execute_command(peer_sock);
					close(peer_sock);
					peer_sock = -1;
					break;
				default:
					cpufreqd_log(LOG_ALERT, "poll(): Internal error caught.\n");
					break;
			}

		/* no socket available, simply pause() */
		} else {
			pause();
		}
	}

	/*
	 * Clean pidfile
	 */
out_pid:
	clear_cpufreqd_pid(configuration.pidfile);

	/* close socket */
out_socket:
	if (cpufreqd_sock != -1) {
		close_unix_sock(cpufreqd_sock);
		delete_temp_dir(dirname);
	}

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
