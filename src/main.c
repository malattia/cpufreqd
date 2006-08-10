/*
 *  Copyright (C) 2002-2006  Mattia Dongili <malattia@linux.it>
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
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "config_parser.h"
#include "cpufreq_utils.h"
#include "cpufreqd.h"
#include "cpufreqd_log.h"
#include "cpufreqd_plugin.h"
#include "cpufreqd_remote.h"
#include "daemon_utils.h"
#include "list.h"
#include "plugin_utils.h"
#include "sock_utils.h"

#define TRIGGER_RULE_EVENT(event_func, directives, dir, old, new) \
do { \
	LIST_FOREACH_NODE(__node, (directives)) { \
		dir = (struct directive *)__node->content; \
		if (dir->keyword->event_func != NULL) { \
			clog(LOG_DEBUG, "Triggering " #event_func " for %s\n", dir->keyword->word); \
			dir->keyword->event_func(dir->obj, (old), (new)); \
		} \
	} \
} while (0);

#define TRIGGER_PROFILE_EVENT(event_func, directives, dir, old, new, cpu_num) \
do { \
	LIST_FOREACH_NODE(__node, (directives)) { \
		dir = (struct directive *)__node->content; \
		if (dir->keyword->event_func != NULL) { \
			clog(LOG_DEBUG, "Triggering " #event_func " for %s\n", dir->keyword->word); \
			dir->keyword->event_func(dir->obj, (old), (new), (cpu_num)); \
		} \
	} \
} while (0);

static struct cpufreqd_info *cpufreqd_info;
static struct rule *current_rule;
static int force_reinit = 0;
static int force_exit = 0;
static int timer_expired = 1; /* expired in order to run on the first loop */

/* default configuration */
static struct cpufreqd_conf default_configuration = {
	.config_file		= CPUFREQD_CONFDIR"cpufreqd.conf",
	.pidfile		= CPUFREQD_STATEDIR"cpufreqd.pid",
	.poll_intv		= { .tv_sec = DEFAULT_POLL, .tv_usec = 0 },
	.has_sysfs		= 1,
	.no_daemon		= 0,
	.log_level_overridden	= 0,
	.log_level		= DEFAULT_VERBOSITY,
	.enable_remote		= 0,
	.remote_gid		= 0,
	.double_check		= 0,
	.print_help		= 0,
	.print_version		= 0,
};
struct cpufreqd_conf *configuration;

/*
 * Try to read current kernel version.
 */
static int get_kversion(void) {
	FILE *fp;
	char kver[256];
	int f = 0;

	fp = fopen ("/proc/version", "r");
	if (!fp) {
		clog(LOG_ERR, "/proc/version: %s\n", strerror(errno));
		return -1;
	}
	do {
		f = fscanf (fp, "Linux version %s", kver);
	} while (f != 1);
	fclose(fp);
	kver[255] = '\0';

	clog(LOG_INFO, "read kernel version %s.\n", kver);

	if (strstr(kver, "2.6") == kver) {
		clog(LOG_DEBUG, "kernel version is 2.6.\n");
		return KERNEL_VERSION_26;
	} else if (strstr(kver, "2.4") == kver) {
		clog(LOG_DEBUG, "kernel version is 2.4.\n");
		return KERNEL_VERSION_24;
	} else {
		clog(LOG_WARNING, "Unknown kernel version, assuming a 2.6 kernel.\n");
		return KERNEL_VERSION_26;
	}

}


/*
 * Evaluates the full rule and returns the percentage score
 * for it.
 */
static unsigned int rule_score(struct rule *rule) {
	unsigned int hits = 0, directives = 0;
	struct directive *d = NULL;
	
	/* call plugin->evaluate for each rule */
	LIST_FOREACH_NODE(node, &rule->directives) {
		d = (struct directive *) node->content;
		/* compute scores for rules and keep the highest */
		if (d->keyword->evaluate != NULL) {
			directives++;
			if (d->keyword->evaluate(d->obj) == MATCH) {
				hits++;
				clog(LOG_DEBUG, "Rule \"%s\": %s matches.\n", rule->name,
						d->keyword->word);
			}
		}
	} /* end foreach rule entry */
	if (directives > 0)
		return hits + (100 * hits / directives);
	clog(LOG_INFO, "No evaulatable directives in Rule \"%s\".\n", rule->name);
	return 0;
}

/*  struct rule *update_rule_scores(struct LIST *rules)
 *  Updates rules score and return the one with the best
 *  one or NULL if every rule has a 0% score.
 */
static struct rule *update_rule_scores(struct LIST *rule_list) {
	struct rule *tmp_rule = NULL;
	struct rule *ret = NULL;
	unsigned int best_score = 0;

	LIST_FOREACH_NODE(node, rule_list) {
		tmp_rule = (struct rule *)node->content;
		
		clog(LOG_DEBUG, "Considering Rule \"%s\"\n", tmp_rule->name);
		tmp_rule->score = rule_score(tmp_rule);

		if (tmp_rule->score > best_score) {
			ret = tmp_rule;
			best_score = tmp_rule->score;
		}
		clog(LOG_INFO, "Rule \"%s\" score: %d%%\n", tmp_rule->name, tmp_rule->score);
	} /* end foreach rule */
	return ret;
}

/* 
 * sets the policy
 * new is never NULL
 *
 * Returns always 0 (success) except if double checking is enabled and setting
 * the policy fails in which case -1 is returned.
 */
static int cpufreqd_set_profile (struct profile **old, struct profile **new) {
	unsigned int i;
	struct directive *d;
	struct profile *old_profile = NULL, *new_profile = NULL;

	for (i = 0; i < cpufreqd_info->cpus; i++) {
		new_profile = new[i];

		if (new_profile == NULL) {
			clog(LOG_DEBUG, "No Profile available for CPU%d doing nothing.\n", i);
			continue;
		}

		if (old != NULL)
			old_profile = old[i];

		/* profile prechange event */
		if (new_profile->directives.first) {
			TRIGGER_PROFILE_EVENT(profile_pre_change, &new_profile->directives, d,
					old_profile != NULL ? &old_profile->policy : NULL,
					&new_profile->policy, i);
		}

		/* don't even try to set the profile if it hasn't changed */
		if (new_profile == old_profile) {
			clog(LOG_DEBUG, "Profile unchanged (\"%s\"-\"%s\"), for CPU%d doing nothing.\n",
					old_profile->name, new_profile->name, i);
		}
		/* int cpufreq_set_policy(unsigned int cpu, struct cpufreq_policy *policy) */ 
		else if (cpufreq_set_policy(i, &(new_profile->policy)) == 0) {
			clog(LOG_NOTICE, "Profile \"%s\" set for CPU%d\n", new_profile->name, i);

			/* double check if everything is OK (configurable) */
			if (configuration->double_check) {
				struct cpufreq_policy *check = NULL;
				check = cpufreq_get_policy(i);
				if (check->max != new_profile->policy.max
						|| check->min != new_profile->policy.min 
						|| strcmp(check->governor, new_profile->policy.governor) != 0) {
					/* written policy and subsequent read disagree */
					clog(LOG_ERR, "I haven't been able to set the chosen policy "
							"for CPU%d.\n"
							"I set %d-%d-%s\n"
							"System says %d-%d-%s\n",
							i, new_profile->policy.max, new_profile->policy.min,
							new_profile->policy.governor, check->max, 
							check->min, check->governor);
					cpufreq_put_policy(check);
					return -1;
				} else {
					clog(LOG_INFO, "Policy correctly set %d-%d-%s\n",
							new_profile->policy.max,
							new_profile->policy.min,
							new_profile->policy.governor);
				}
				cpufreq_put_policy(check);
				cpufreqd_info->current_profiles[i] = new_profile;
			} /* end if double_check */
		}
		else {
			clog(LOG_WARNING, "Couldn't set profile \"%s\" set for cpu%d\n",
					new_profile->name, i);
			return -1;
		}

		/* profile postchange event */
		if (new_profile->directives.first) {
			TRIGGER_PROFILE_EVENT(profile_post_change, &new_profile->directives, d,
					old != NULL ? &old_profile->policy : NULL,
					&new_profile->policy, i);
		}
	}
	return 0;
}

static int set_cpufreqd_runmode(int mode) {
	if (mode == MODE_DYNAMIC) {
		struct itimerval new_timer;
		new_timer.it_interval.tv_usec = configuration->poll_intv.tv_usec;
		new_timer.it_interval.tv_sec = configuration->poll_intv.tv_sec;
		new_timer.it_value.tv_usec = configuration->poll_intv.tv_usec;
		new_timer.it_value.tv_sec = configuration->poll_intv.tv_sec;
		/* set next alarm */
		if (setitimer(ITIMER_REAL, &new_timer, 0) < 0) {
			clog(LOG_CRIT, "Couldn't set timer: %s\n", strerror(errno));
			return errno;
		}
		timer_expired = 1;
	} 
	else if (mode == MODE_MANUAL) {
		/* reset alarm */
		if (setitimer(ITIMER_REAL, NULL, 0) < 0) {
			clog(LOG_CRIT, "Couldn't set timer: %s\n", strerror(errno));
			return errno;
		}
	}
	else {
		clog(LOG_WARNING, "Unknown mode %d\n", mode);
		return EINVAL;
	}
	cpufreqd_info->cpufreqd_mode = mode;
	return 0;
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
		{ "manual", 0, 0, 'm' },
		{ "verbosity", 1, 0, 'V' },
		{ 0, 0, 0, 0 }, 
	};
	int ch,option_index = 0;

	while ((ch = getopt_long(argc, argv, "hvf:DmV:", long_options, &option_index)) != -1) {
		switch (ch) {
		case '?':
		case 'h':
			configuration->print_help = 1;
			return 0;
		case 'v':
			configuration->print_version = 1;
			return 0;
		case 'f':
			if (realpath(optarg, configuration->config_file) == NULL) {
				clog(LOG_ERR, "Error reading command line argument (%s: %s).\n",
						optarg, strerror(errno));
				return -1;
			}
			clog(LOG_DEBUG, "Using configuration file at %s\n", configuration->config_file);
			break;
		case 'D':
			configuration->no_daemon = 1;
			break;
		case 'm':
			cpufreqd_info->cpufreqd_mode = MODE_MANUAL;
			return 0;
		case 'V':
			configuration->log_level = atoi(optarg);
			if (configuration->log_level>7) {
				configuration->log_level = 7;
			} else if (configuration->log_level<0) {
				configuration->log_level = 0;
			}
			configuration->log_level_overridden = 1;
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
	printf("Copyright 2002-2006 Mattia Dongili <"__CPUFREQD_MAINTAINER__">\n"
	       "                    George Staikos <staikos@0wned.org>\n");
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
			"  -m, --manual                 start in manual mode (ignored if the enable_remote is 0)\n"
			"  -V, --verbosity              verbosity level from 0 (less verbose) to 7 (most verbose)\n"
			"\n"
			"Report bugs to Mattia Dongili <malattia@linux.it>.\n", me);
}

static void term_handler(int signo) {
	clog(LOG_NOTICE, "Caught TERM signal (%s), forcing exit.\n", strsignal(signo));
	force_exit = 1;
}

static void int_handler(int signo) {
	clog(LOG_NOTICE, "Caught INT signal (%s), forcing exit.\n", strsignal(signo));
	force_exit = 1;
}

static void alarm_handler(int signo) {
	clog(LOG_DEBUG, "Caught ALARM signal (%s).\n", strsignal(signo));
	timer_expired = 1;
}

static void hup_handler(int signo) {
#if 0
	clog(LOG_NOTICE, "Caught HUP signal (%s), reloading configuration file.\n", strsignal(signo));
	force_reinit = 1;
#else
	clog(LOG_WARNING, "Caught HUP signal (%s), ignored.\n", strsignal(signo));
#endif
}

static void pipe_handler(int signo) {
  clog(LOG_NOTICE, "Caught PIPE signal (%s).\n", strsignal(signo));
}

static void cpufreqd_loop(struct cpufreqd_conf *conf) {
	int rule_equivalent = 0, ret = 0;
	unsigned int i = 0;
	struct rule *best_rule = NULL;
	struct directive *d = NULL;
	
	/* update timestamp */
	if (gettimeofday(&cpufreqd_info->timestamp, NULL) < 0) {
		clog(LOG_ERR, "Couldn't read current time: %s\n", strerror(errno));
	} else {
		clog(LOG_DEBUG, "Current time is: %lu::%lu\n",
				cpufreqd_info->timestamp.tv_sec,
				cpufreqd_info->timestamp.tv_usec);
	}

	update_plugin_states(&conf->plugins);
	best_rule = update_rule_scores(&conf->rules);

	/* set the policy associated with the highest score */
	if (best_rule == NULL) {
		clog(LOG_WARNING, "No Rule matches current system status.\n");

	} else if (current_rule != best_rule) {
		
		/* rule changed */

		/* try to be conservative, if the new rule has the same score
		 * as the old one then keep the old one.
		 * Check for profiles equivalence too.
		 */
		if (current_rule != NULL && current_rule->score == best_rule->score) {

			rule_equivalent = 1;
			for (i = 0; i < cpufreqd_info->cpus; i++) {
				/*
				 * if the new rule sets a profile for a cpu not 
				 * covered by the current rule then prefer the new rule
				 */
				 if (current_rule->prof[i] == NULL && best_rule->prof[i] != NULL) {
					 rule_equivalent = 0;
					 break;
				 }
			}
			if (rule_equivalent) {
				clog(LOG_INFO, "New Rule (\"%s\") is equivalent "
						"to the old one (\"%s\"), doing nothing.\n",
						best_rule->name, current_rule->name);
				return;
			}
		}
		
		clog(LOG_DEBUG, "New Rule (\"%s\"), applying.\n", 
				best_rule->name);
		/* pre change event */
		if (best_rule->directives.first != NULL) {
			TRIGGER_RULE_EVENT(rule_pre_change, &best_rule->directives, d,
					current_rule, best_rule);
		}

		/* change frequency */
		if (current_rule == NULL)
			ret = cpufreqd_set_profile(NULL, best_rule->prof);

		else if (best_rule->prof != current_rule->prof)
			ret = cpufreqd_set_profile(current_rule->prof, best_rule->prof);

		if (ret < 0) {
			clog(LOG_ERR, "Cannot set policy, Rule unchanged (\"%s\").\n", 
					current_rule != NULL ? current_rule->name : "none");
			return;
		}

		/* post change event */
		if (best_rule->directives.first != NULL) {
			TRIGGER_RULE_EVENT(rule_post_change, &best_rule->directives, d,
					current_rule, best_rule);
		}

		/* update current rule */
		current_rule = best_rule;

	} else {
		/* nothing new happened */
		clog(LOG_DEBUG, "Rule unchanged (\"%s\"), doing nothing.\n", 
				current_rule->name);
	}
}

/*
 * Parse and execute the client command
 */
static void execute_command(int sock, struct cpufreqd_conf *conf) {
	struct pollfd fds;
	char buf[MAX_STRING_LEN];
	unsigned int buflen = 0, counter = 0, i = 0;
	struct profile *p = NULL, **pp = NULL;
	uint32_t command = INVALID_CMD;

	/* we have a valid sock, wait for command
	 * don't wait more tha 0.5 sec
	 */
	fds.fd = sock;
	fds.events = POLLIN | POLLRDNORM;

	if (poll(&fds, 1, 500) != 1) {
		clog(LOG_ALERT, "Waited too long for data, aborting.\n");

	} else if (read(sock, &command, sizeof(uint32_t)) == -1) {
		clog(LOG_ALERT, "process_packet - read(): %s\n", strerror(errno));

	} else if (command != INVALID_CMD) {
		clog(LOG_INFO, "command received: %0.4x %0.4x\n",
				REMOTE_CMD(command), REMOTE_ARG(command));
		switch (REMOTE_CMD(command)) {
			case CMD_UPDATE_STATE:
				clog(LOG_DEBUG, "CMD_UPDATE_STATE\n");
				clog(LOG_ALERT, "Ignoring unimplemented command %0.8x\n", command);
				break;
			case CMD_LIST_RULES:
				clog(LOG_DEBUG, "CMD_LIST_RULES\n");
				clog(LOG_ALERT, "Ignoring unimplemented command %0.8x\n", command);
				break;
			case CMD_LIST_PROFILES:
				clog(LOG_DEBUG, "CMD_LIST_PROFILES\n");
				LIST_FOREACH_NODE(node, &conf->profiles) {
					p = (struct profile *) node->content;
					/* FIXME: the current profile is not checked
					 * as it may well be different from each cpu.
					 * See command CMD_CUR_PROFILES.
					 */
					/* format is:
					 * 1 always 0, currently unused
					 * 2 profile name
					 * 3 min freq
					 * 4 max freq
					 * 5 active governor
					 */
					buflen = snprintf(buf, MAX_STRING_LEN, "%d/%s/%lu/%lu/%s\n",
							0,
							p->name,
							p->policy.min, p->policy.max,
							p->policy.governor);
					write(sock, buf, buflen);
				}
				break;
			case CMD_CUR_PROFILES:
				clog(LOG_DEBUG, "CMD_CUR_PROFILES\n");
				if (!cpufreqd_info->current_profiles)
					break;

				for (i = 0; i < cpufreqd_info->cpus; i++) {
					if (!cpufreqd_info->current_profiles[i])
						continue;
					/* format is:
					 * 1 cpu where the profile is active
					 * 2 profile name
					 * 3 min freq
					 * 4 max freq
					 * 5 active governor
					 */
					buflen = snprintf(buf, MAX_STRING_LEN, "%d/%s/%lu/%lu/%s\n",
							i,
							cpufreqd_info->current_profiles[i]->name,
							cpufreqd_info->current_profiles[i]->policy.min,
							cpufreqd_info->current_profiles[i]->policy.max,
							cpufreqd_info->current_profiles[i]->policy.governor);
					write(sock, buf, buflen);
				}
				break;
			case CMD_SET_RULE:
				clog(LOG_DEBUG, "CMD_SET_RULE\n");
				clog(LOG_ALERT, "Ignoring unimplemented command %0.8x\n", command);
				break;
			case CMD_SET_MODE:
				clog(LOG_DEBUG, "CMD_SET_MODE\n");
				set_cpufreqd_runmode((int)REMOTE_ARG(command));
				break;
			case CMD_SET_PROFILE:
				clog(LOG_DEBUG, "CMD_SET_PROFILE\n");
				if (cpufreqd_info->cpufreqd_mode == MODE_DYNAMIC) {
					clog(LOG_ERR, "Couldn't set profile while running "
							"in DYNAMIC mode.\n");
					break;
				}
				if (!REMOTE_ARG(command)) {
					clog(LOG_ERR, "Invalid argument %0.4x\n",
							REMOTE_ARG(command));
					break;
				}
				LIST_FOREACH_NODE(node, &conf->profiles) {
					p = (struct profile *) node->content;
					counter++;
					if (counter == REMOTE_ARG(command)) {

						/* set this profile for every cpu */
						pp = calloc(cpufreqd_info->cpus, sizeof(struct profile *));
						if (pp == NULL) {
							clog(LOG_ERR, "Couldn't allocate enough memory "
									" to set profile \"%s\"\n",
									p->name);
							/* return immediately */
							return;
						}

						for(i = 0; i < cpufreqd_info->cpus; i++)
							pp[i] = p;

						cpufreqd_set_profile(NULL, pp);
						free(pp);

						/* reset the current rule to let
						 * the cpufreqd_loop set the correct
						 * one when going back to dynamic mode
						 */
						current_rule = NULL;
						counter = 0;
						break;
					}
				}
				if (counter > 0) {
					clog(LOG_ERR, "Couldn't find profile %d\n",
							REMOTE_ARG(command));
				}
				break;
			default:
				clog(LOG_ALERT, "Unable to process packet: %d\n",
						command);
				break;
		}
	}
}


struct cpufreqd_info * get_cpufreqd_info (void) {
	return cpufreqd_info;
}

/*
 *  main !
 *  Let's go
 */
int main (int argc, char *argv[]) {

	struct sigaction signal_action;
	sigset_t old_sigmask;
	fd_set rfds;
	unsigned int i = 0;
	int cpufreqd_sock = -1, peer_sock = -1; /* input pipe */
	char dirname[MAX_PATH_LEN];
	int ret = 0;
	
	configuration = malloc(sizeof(struct cpufreqd_conf));
	if (configuration == NULL) {
		ret = ENOMEM;
		goto out;
	}
	memcpy(configuration, &default_configuration, sizeof(struct cpufreqd_conf));

	cpufreqd_info = malloc(sizeof(struct cpufreqd_info));
	if (cpufreqd_info == NULL) {
		ret = ENOMEM;
		goto out;
	}
	memset(cpufreqd_info, 0, sizeof(struct cpufreqd_info));
	cpufreqd_info->cpufreqd_mode = MODE_DYNAMIC;

	/* 
	 *  check perms
	 */
#if 1
	if (geteuid() != 0) {
		cpufreqd_log(LOG_CRIT, "%s: must be run as root.\n", argv[0]);
		ret = EACCES;
		goto out;
	}
#endif

	/*
	 *  read_args
	 */
	if (read_args(argc, argv)!=0) {
		cpufreqd_log(LOG_CRIT, "Unable parse command line parameters, exiting.\n");
		ret = EINVAL;
		goto out;
	}
	if (configuration->print_help) {
		print_help(argv[0]);
		goto out;
	}
	if (configuration->print_version) {
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

	/* read kernel version */
	cpufreqd_info->kernel_version = get_kversion();
	
	/*
	 *  read how many cpus are available here
	 */
	cpufreqd_info->cpus = get_cpu_num();

	/*
	 *  find cpufreq information about each cpu
	 */
	cpufreqd_info->sys_info = calloc(1, cpufreqd_info->cpus * sizeof(struct cpufreq_sys_info));
	if (cpufreqd_info->sys_info == NULL) {
		clog(LOG_CRIT, "Unable to allocate memory (%s), exiting.\n", strerror(errno));
		ret = ENOMEM;
		goto out;
	}
	for (i = 0; i < cpufreqd_info->cpus; i++) {
		(cpufreqd_info->sys_info+i)->affected_cpus = cpufreq_get_affected_cpus(i);
		(cpufreqd_info->sys_info+i)->governors = cpufreq_get_available_governors(i);
		(cpufreqd_info->sys_info+i)->frequencies = cpufreq_get_available_frequencies(i);
	}
	/* 
	 * per-cpu profiles
	 */
	cpufreqd_info->current_profiles = calloc(1, cpufreqd_info->cpus * sizeof(struct profile *));
	if (cpufreqd_info->current_profiles == NULL) {
		clog(LOG_CRIT, "Unable to allocate memory (%s), exiting.\n", strerror(errno));
		ret = ENOMEM;
		goto out;
	}


	/* SMP: with different speed cpus */
	cpufreqd_info->limits = calloc(1, cpufreqd_info->cpus * sizeof(struct cpufreq_limits));
	if (cpufreqd_info->limits == NULL) {
		clog(LOG_CRIT, "Unable to allocate memory (%s), exiting.\n", strerror(errno));
		ret = ENOMEM;
		goto out;
	}
	for (i = 0; i < cpufreqd_info->cpus; i++) {
		/* if one of the probes fails remove all the others also */
		struct cpufreq_limits *tmp_lim = cpufreqd_info->limits+i;
		if (cpufreq_get_hardware_limits(i, &tmp_lim->min, &tmp_lim->max) != 0) {
			/* TODO: if libcpufreq fails try to read /proc/cpuinfo
			 * and warn about this not being reliable
			 */
			clog(LOG_WARNING, "Unable to get hardware frequency limits for CPU%d.\n", i);
			free(cpufreqd_info->limits);
			cpufreqd_info->limits = NULL;
			break;
		} else {
			clog(LOG_INFO, "Limits for cpu%d: MIN=%lu - MAX=%lu\n", i, 
					tmp_lim->min, tmp_lim->max);
		}
	}

	/*
	 *  daemonize if necessary
	 */
	if (configuration->no_daemon==0 && daemonize()!=0) {
		clog(LOG_CRIT, "Unable to go background, exiting.\n");
		ret = ECHILD;
		goto out;
	}

cpufreqd_start:

	if (init_configuration(configuration) < 0) {
		clog(LOG_CRIT, "Unable to parse config file: %s\n", configuration->config_file);
		ret = EINVAL;
		goto out_config_read;
	}

	/* setup UNIX socket if necessary */
	if (configuration->enable_remote) {
		dirname[0] = '\0';
		if (create_temp_dir(dirname, configuration->remote_gid) == NULL) {
			clog(LOG_ERR, "Couldn't create temporary directory %s\n", dirname);
			cpufreqd_sock = -1;
		} else if ((cpufreqd_sock = open_unix_sock(dirname, configuration->remote_gid)) == -1) {
			delete_temp_dir(dirname);
			clog(LOG_ERR, "Couldn't open socket, remote controls disabled\n");
		} else {
			clog(LOG_INFO, "Remote controls enabled\n");
			if (cpufreqd_info->cpufreqd_mode == MODE_MANUAL)
				clog(LOG_INFO, "Starting in manual mode\n");
		}
	}

	/* Validate plugins, if none left exit.... */
	if (validate_plugins(&configuration->plugins) == 0) {
		cpufreqd_log(LOG_CRIT, "Hey! all the plugins I loaded are useless, "
				"maybe your configuration needs some rework.\n"
				"Exiting.\n");
		ret = EINVAL;
		goto out_socket;
	}

	/* write pidfile */
	if (write_cpufreqd_pid(configuration->pidfile) < 0) {
		clog(LOG_CRIT, "Unable to write pid file: %s\n", configuration->pidfile);
		ret = EINVAL;
		goto out_socket;
	}

	/* if we are going to pselect the socket
	 * then block all signals to avoid races,
	 * will be unblocked by pselect
	 *
	 * NOTE: Since Linux today does not have a pselect() system  call,  the
	 *       current glibc2 routine still contains this race.
	 *       (man 2 pselect).
	 *       I'll make all the efforts to avoid that race (the code between
	 *       setitimer and pselect is as short as possible, but...)
	 *
	 */
	if (cpufreqd_sock > 0) {
		sigemptyset(&signal_action.sa_mask);
		sigaddset(&signal_action.sa_mask, SIGALRM);
		sigprocmask(SIG_BLOCK, &signal_action.sa_mask, &old_sigmask);
	}
	
	set_cpufreqd_runmode(cpufreqd_info->cpufreqd_mode);
	/*
	 *  Looooooooop
	 */
	while (!force_exit && !force_reinit) {
		/*
		 * Run the system scan and rule selection and set timer
		 * if running in DYNAMIC mode AND the timer is expired
		 */
		if (cpufreqd_info->cpufreqd_mode == MODE_DYNAMIC && timer_expired) {
			cpufreqd_loop(configuration);
			/* can safely reset the expired flag now */
			timer_expired = 0;
		}

		/* if the socket opened successfully */
		if (cpufreqd_sock > 0) {
			/* wait for a command */
			FD_ZERO(&rfds);
			FD_SET(cpufreqd_sock, &rfds);
			
			if (!timer_expired || cpufreqd_info->cpufreqd_mode == MODE_MANUAL) {
				switch (pselect(cpufreqd_sock+1, &rfds, NULL, NULL, NULL, &old_sigmask)) {
					case 0:
						/* timed out. check to see if things have changed */
						/* will never happen actually... */
						break;
					case -1:
						/* caused by SIGALARM (mostly) log if not so */
						if (errno != EINTR)
							clog(LOG_NOTICE, "pselect(): %s.\n", strerror(errno));
						break;
					case 1:
						/* somebody tried to contact us. see what he wants */
						peer_sock = accept(cpufreqd_sock, NULL, 0);
						if (peer_sock == -1) {
							clog(LOG_ALERT, "Unable to accept connection: "
									" %s\n", strerror(errno));
						}
						execute_command(peer_sock, configuration);
						close(peer_sock);
						peer_sock = -1;
						break;
					default:
						clog(LOG_ALERT, "pselect(): Internal error caught.\n");
						break;
				}
			}
		}
		/* paranoid check for timer expiration
		 * (might actually happen...)
		 */
		else if (!timer_expired) {
			pause();
		}
	}

	/*
	 * Clean pidfile
	 */
	clear_cpufreqd_pid(configuration->pidfile);

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
	free_configuration(configuration);
	if (force_reinit && !force_exit) {
		force_reinit = 0;
		cpufreqd_log(LOG_INFO, "Restarting cpufreqd\n");
		goto cpufreqd_start;
	}

out:
	if (cpufreqd_info != NULL) {
		if (cpufreqd_info->limits != NULL)
			free(cpufreqd_info->limits);

		if (cpufreqd_info->sys_info != NULL) {
			for (i=0; i<cpufreqd_info->cpus; i++) {
				if ((cpufreqd_info->sys_info+i)->governors!=NULL)
					cpufreq_put_available_governors((cpufreqd_info->sys_info+i)->governors);
				if ((cpufreqd_info->sys_info+i)->affected_cpus!=NULL)
					cpufreq_put_affected_cpus((cpufreqd_info->sys_info+i)->affected_cpus);
				if ((cpufreqd_info->sys_info+i)->frequencies!=NULL)
					cpufreq_put_available_frequencies((cpufreqd_info->sys_info+i)->frequencies);
			}
			free(cpufreqd_info->sys_info);
		}

		if (cpufreqd_info->current_profiles != NULL)
			free(cpufreqd_info->current_profiles);

		free(cpufreqd_info);
	}
	if (configuration != NULL)
		free(configuration);
	/*
	 *  bye bye  
	 */
	return ret;
}
