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

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <sys/types.h>
#include <string.h>
#include "config_parser.h"
#include "cpufreq_utils.h"
#include "cpufreqd_log.h"
#include "plugin_utils.h"

/* char *clean_config_line (char *str)
 *
 * Removes trailing blanks and CR off the string str
 *
 * Returns a pointer to the cleaned string.
 * WARNING: it modifies the input string!
 */
static char *clean_config_line (char *str) {
	int i = 0;

	/* remove white spaces at the beginning */
	while (isspace(str[0])) {
		str++;
	}

	/* remove end line white space */
	i = strlen(str) - 1;
	while (i >= 0 && isspace(str[i])) {
		str[i] = '\0';
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
static char *strip_comments_line (char *str) {
	char *ch = str;
	while (ch[0]) {
		if (ch[0] == '#') {
			ch[0] = '\0';
			break;
		}
		ch++;
	}
	return str;
}

/* read a line frome the file descriptor and clean the 
 * line removing comments and trimming the string
 */
static char *read_clean_line(FILE *fp, char *buf, int n) {
	if (fgets(buf, n, fp)) {
		buf[n-1] = '\0';
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
static int parse_config_general (FILE *config, struct cpufreqd_conf *configuration) {
	char buf[MAX_STRING_LEN];
	char *clean;
	char *name;
	char *value;
	char *token;
	struct group *grp = NULL;
	long int gid = 0;
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
				configuration->poll_interval = atoi (value);
			}
			/* validate */
			if (configuration->poll_interval < 1) {
				clog(LOG_WARNING, "WARNING! poll_interval has invalid value, "
						"using default.\n");
				configuration->poll_interval = DEFAULT_POLL;
			}
			continue;
		}

		if (strcmp(name,"verbosity") == 0) {
			if (configuration->log_level_overridden) {
				clog(LOG_DEBUG, "skipping \"verbosity\", "
						"overridden in the command line.\n");
				continue;
			}

			if (value != NULL) {
				configuration->log_level = atoi (value);
				/* validate */
				if (configuration->log_level < 0 || configuration->log_level > 7) {
					clog(LOG_WARNING, "WARNING! verbosity has invalid value, "
							"using default (%d).\n", DEFAULT_VERBOSITY);
					configuration->log_level = DEFAULT_VERBOSITY;
				}
			} else {
				configuration->log_level = DEFAULT_VERBOSITY;
				clog(LOG_WARNING, "WARNING! verbosity has empty value, "
						"using default (%d).\n", DEFAULT_VERBOSITY);
			}
			continue;
		}

		if (strcmp(name,"enable_plugins") == 0) {
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
				list_append(&(configuration->plugins), n_plugin);
				clog(LOG_DEBUG, "read plugin: %s\n", token);

			} while ((token = strtok(NULL,",")) != NULL);
			continue;

		}

		if (strcmp(name,"pidfile") == 0) {
			if (value != NULL) {
				strncpy(configuration->pidfile, value, MAX_PATH_LEN);
			} else {
				clog(LOG_WARNING, "empty \"pidfile\", using default %s.\n",
						CPUFREQD_PIDFILE);
				strncpy(configuration->pidfile, CPUFREQD_PIDFILE, MAX_PATH_LEN);
			}
			configuration->pidfile[MAX_PATH_LEN-1] = '\0';
			continue;
		}

		if (strcmp(name,"double_check") == 0) {
			if (value != NULL) {
				configuration->double_check = atoi (value);
				clog(LOG_WARNING, "double check profiles %s.\n", 
						configuration->double_check ? "enabled" : "disabled");
			}
			continue;
		}
		if (strcmp(name,"enable_remote") == 0) {
			if (value != NULL) {
				configuration->enable_remote = atoi (value);
				clog(LOG_WARNING, "Remote control %s.\n", 
						configuration->enable_remote ? "enabled" : "disabled");
			}
			continue;
		}
		if (strcmp(name,"remote_group") == 0) {
			if (value != NULL) {
				gid = strtol(value, &token, 10);
				
				/* in case value doesn't hold a number or that
				 * number is not acceptable try to consider it a
				 * groupname otherwise validate the given gid.
				 * If it still fails, shout!
				 * Note: the group_id limit is pretty arbitrary here...
				 */
				if ((*token == '\0' && gid > 0 && gid < USHRT_MAX
						&& (grp = getgrgid((gid_t)gid)) != NULL)
						|| (grp = getgrnam(value)) != NULL) {

					configuration->remote_gid = grp->gr_gid;
					clog(LOG_WARNING, "Remote controls will be r/w from group %s (%d).\n",
							grp->gr_name, grp->gr_gid);
				} else {
					configuration->remote_gid = 0;
					clog(LOG_WARNING, "remote_group contains an invalid value "
							"(%s), r/w group permissions will "
							"remain unchanged.\n", value);
				}
			}
			continue;
		}

		clog(LOG_WARNING, "WARNING! skipping config option \"%s\"\n", name);
	} /* end while */

	return 0;
}

/*
 * parse a [Profile] section
 *
 * Returns -1 if required properties are missing, 0 otherwise
 */
#define HAS_NAME    (1<<0)
#define HAS_MIN     (1<<1)
#define HAS_MAX     (1<<2)
#define HAS_POLICY  (1<<3)
#define HAS_CPU     (1<<4)
static int parse_config_profile (FILE *config, struct profile *p, struct LIST *plugins,
		struct cpufreq_limits *limits, struct cpufreq_available_frequencies *freq) {
	int state = 0, min_is_percent = 0, max_is_percent = 0, tmp_freq = 0;
	struct NODE *dir = NULL;
	void *obj = NULL; /* to hold the value provided by a plugin */
	struct cpufreqd_keyword *ckw = NULL;
	struct cpufreqd_plugin *plugin = NULL;
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
			continue;
		}
		
		if (strcmp(name,"minfreq")==0) {
			if (sscanf(value, "%d", &tmp_freq) != 1) {
				clog(LOG_ERR, "unable to parse MIN value %s.\n", value);
				return -1;
			}
			if (strstr(value, "%") != NULL) {
				min_is_percent=1;
			}
			if (tmp_freq < 0) {
				clog(LOG_NOTICE, "MIN freq below 0, resetting\n");
				p->policy.min = 0;
			} else {
				p->policy.min = tmp_freq;
			}
			state |= HAS_MIN;
			continue;
		}
		
		if (strcmp(name,"maxfreq") == 0) {
			if (sscanf(value, "%d", &tmp_freq) != 1) {
				clog(LOG_ERR, "unable to parse MAX value %s.\n", value);
				return -1;
			}
			if (strstr(value, "%") != NULL) {
				max_is_percent=1;
			}
			if (tmp_freq < 0) {
				clog(LOG_NOTICE, "MAX freq below 0, resetting\n");
				p->policy.max = 0;
			} else {
				p->policy.max = tmp_freq;
			}
			state |= HAS_MAX;
			continue;
		}
		
		if (strcmp(name,"cpu") == 0) {
			if (sscanf(value, "%u", &(p->cpu)) != 1) {
				clog(LOG_ERR, "unable to parse CPU value %s.\n", value);
				return -1;
			}
			state |= HAS_CPU;
			continue;
		}
		
		if (strcmp(name,"policy") == 0) {
			strncpy(p->policy.governor, value, MAX_STRING_LEN);
			p->policy.governor[MAX_STRING_LEN-1] = 0;
			state |= HAS_POLICY;
			continue;

		}
		
		/* it's plugin time to tell if they like the directive */
		ckw = plugin_handle_keyword(plugins, name, value, &obj, &plugin);
		/* if no plugin found read next line */
		if (ckw != NULL) {
			dir = node_new(NULL, sizeof(struct directive));
			if (dir == NULL) {
				free_keyword_object(ckw, obj);
				clog(LOG_ERR, "cannot make enough room for a new directive (%s).\n",
						strerror(errno));
				return -1;
			}
			((struct directive *)dir->content)->keyword = ckw;
			((struct directive *)dir->content)->obj = obj;
			((struct directive *)dir->content)->plugin = plugin;
			/* ok, append the rule entry */
			list_append(&(p->directives), dir);
			p->directives_count++;
			continue;
		}

		clog(LOG_WARNING, "WARNING! skipping config option \"%s\"\n", name);
	} /* end while */

	if (!(state & HAS_NAME)) {
		clog(LOG_ERR, "missing required property \"name\".\n");
		return -1;
	}
	if (!(state & HAS_MIN)) {
		clog(LOG_ERR, "\"%s\" missing required property \"minfreq\".\n", p->name);
		return -1;
	}
	if (!(state & HAS_MAX)) {
		clog(LOG_ERR, "\"%s\" missing required property \"maxfreq\".\n", p->name);
		return -1;
	}
	if (!(state & HAS_POLICY)) {
		clog(LOG_ERR, "\"%s\" missing required property \"policy\".\n", p->name);
		return -1;
	}

	/* TODO: check if the selected governor is available */

	/* validate and normalize frequencies */
	if (limits!=NULL) {
		/* calculate actual frequncies if percent where given frequencies */
		if (state & HAS_CPU) {
			if (min_is_percent)
				p->policy.min = percent_to_absolute(limits[p->cpu].max, p->policy.min);
			if (max_is_percent)
				p->policy.max = percent_to_absolute(limits[p->cpu].max, p->policy.max);
		} else {
			if (min_is_percent)
				p->policy.min = percent_to_absolute(limits[0].max, p->policy.min);
			if (max_is_percent)
				p->policy.max = percent_to_absolute(limits[0].max, p->policy.max);
		}
		/* normalize frequencies if such informations are available */
		p->policy.max = normalize_frequency(limits, freq, p->policy.max);
		p->policy.min = normalize_frequency(limits, freq, p->policy.min);
	} else {
		if (min_is_percent || max_is_percent) {
			clog(LOG_WARNING, "Unable to calculate absolute values for profile \"%s\".\n",
					p->name);
		}
		clog(LOG_WARNING, "Unable to normalize frequencies for profile \"%s\".\n", p->name);
	}

	clog(LOG_DEBUG, "[Profile] \"%s\" MAX is %ld, MIN is %ld\n", p->name,
			p->policy.max, p->policy.min);

	if (p->policy.min > p->policy.max) {
		clog(LOG_WARNING, "[Profile] \"%s\" uh! MIN freq is higher than MAX freq??\n",
				p->name);
	}

	return 0;
}

/*
 * parses a [Rule] section
 *
 * Returns -1 if required properties are missing, 0 otherwise
 */
#define HAS_NAME    (1<<0) 
#define HAS_PROFILE (1<<1)
static int parse_config_rule (FILE *config, struct rule *r, struct LIST *plugins) {
	int state = 0;
	char buf[MAX_STRING_LEN];
	char *clean = NULL, *name = NULL, *value = NULL;
	struct NODE *dir = NULL;
	void *obj = NULL; /* to hold the value provided by a plugin */
	struct cpufreqd_keyword *ckw = NULL;
	struct cpufreqd_plugin *plugin = NULL;

	/* reset profile ref */
	r->prof = 0;

	while (!feof(config)) {

		buf[0] = '\0';
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
			continue;
		}
		
		if (strcmp(name,"name") == 0) {
			strncpy(r->name, value, MAX_STRING_LEN);
			r->name[MAX_STRING_LEN-1] = '\0';
			state |= HAS_NAME;
			continue;
		}
		
		/* it's plugin time to tell if they like the directive */
		ckw = plugin_handle_keyword(plugins, name, value, &obj, &plugin);
		/* if plugin found append to the list */
		if (ckw != NULL) {
			dir = node_new(NULL, sizeof(struct directive));
			if (dir == NULL) {
				free_keyword_object(ckw, obj);
				clog(LOG_ERR, "cannot make enough room for a new directive (%s).\n",
						strerror(errno));
				return -1;
			}
			((struct directive *)dir->content)->keyword = ckw;
			((struct directive *)dir->content)->obj = obj;
			((struct directive *)dir->content)->plugin = plugin;
			/* ok, append the rule entry */
			list_append(&(r->directives), dir);
			r->directives_count++;
			continue;
		}

		clog(LOG_WARNING, "WARNING! skipping config option \"%s\"\n", name);
	} /* end while */

	if (!(state & HAS_NAME)) {
		clog(LOG_ERR, "missing required property \"name\".\n");
		return -1;
	}

	if (!(state & HAS_PROFILE)) {
		clog(LOG_ERR, "\"%s\" missing required property \"profile\".\n", 
				r->name);
		return -1;
	}

	return 0;
}

/* Handles the configuration section for a given plugin
 */
static void configure_plugin(FILE *config, struct plugin_obj *plugin) {
	char endtag[MAX_STRING_LEN];
	char buf[MAX_STRING_LEN];
	char *clean = NULL, *name = NULL, *value = NULL;

	snprintf(endtag, MAX_STRING_LEN, "[/%s]", plugin->plugin->plugin_name);

	while (!feof(config)) {
		
		clean = read_clean_line(config, buf, MAX_STRING_LEN);

		if (!clean[0]) /* returned an empty line */
			continue;

		if (strncasecmp(endtag, clean, MAX_STRING_LEN) == 0)
			break;
		
		name = strtok(clean, "=");
		value = strtok(NULL, "");

		if (plugin->plugin->plugin_conf(name, value) != 0) {
			clog(LOG_WARNING, "plugin \"%s\" can't handle %s.\n",
					plugin->plugin->plugin_name, name);
		}
	}
}

/* intialize the cpufreqd_conf object 
 * by reading the configuration file
 */
int init_configuration(struct cpufreqd_conf *configuration)
{
	FILE *fp_config = NULL;
	struct NODE *n = NULL;
	struct profile *tmp_profile = NULL;
	struct rule *tmp_rule = NULL;
	struct plugin_obj *plugin = NULL;
	char *clean = NULL;
	char buf[256];
	int plugins_post_confed = 0; /* did I already run post_conf for all? */

	/* configuration file */
	clog(LOG_INFO, "reading configuration file %s\n", configuration->config_file);
	fp_config = fopen(configuration->config_file, "r");
	if (!fp_config) {
		clog(LOG_ERR, "%s: %s\n", configuration->config_file, strerror(errno));
		return -1;
	}

	while (!feof(fp_config)) {
		
		buf[0] = '\0';
		clean = read_clean_line(fp_config, buf, 256);

		if (!clean[0]) /* returned an empty line */
			continue;

		/* if General scan general options */
		if (strstr(clean,"[General]")) {

			if (parse_config_general(fp_config, configuration) < 0) {
				fclose(fp_config);
				return -1;
			}
			/*
			 *  Load plugins
			 *  just after having read the General section
			 *  and before the rest in order to be able to hadle
			 *  options with them.
			 */
			load_plugin_list(&configuration->plugins);
			continue;
		}

		/* if Profile scan profile options */
		if (strstr(clean,"[Profile]")) {

			if (!plugins_post_confed) {
				plugins_post_conf(&configuration->plugins);
				plugins_post_confed = 1;
			}

			n = node_new(NULL, sizeof(struct profile));
			if (n == NULL) {
				clog(LOG_ERR, "cannot make enough room for a new Profile (%s)\n",
						strerror(errno));
				fclose(fp_config);
				return -1;
			}
			/* create governor string */
			tmp_profile = (struct profile *)n->content;
			if ((tmp_profile->policy.governor = malloc(MAX_STRING_LEN*sizeof(char))) ==NULL) {
				clog(LOG_ERR, "cannot make enough room for a new Profile governor (%s)\n",
						strerror(errno));
				node_free(n);
				fclose(fp_config);
				return -1;
			}

			if (parse_config_profile(fp_config, tmp_profile, &configuration->plugins, 
					configuration->limits, configuration->sys_info->frequencies) < 0) {
				clog(LOG_CRIT, "[Profile] error parsing %s, see logs for details.\n",
						configuration->config_file);
				node_free(n);
				fclose(fp_config);
				return -1;
			}
			/* checks duplicate names */
			LIST_FOREACH_NODE(node, &configuration->profiles) {
				struct profile *tmp = (struct profile *)node->content;
				if (strcmp(tmp->name, tmp_profile->name) != 0)
					continue;
				clog(LOG_CRIT, "[Profile] name \"%s\" already exists. Skipped\n", 
						tmp_profile->name);
				node_free(n);
			}
			list_append(&configuration->profiles, n);
			/* avoid continuing */
			continue;
		}

		/* if Rule scan rules options */
		if (strstr(clean,"[Rule]")) {

			if (!plugins_post_confed) {
				plugins_post_conf(&configuration->plugins);
				plugins_post_confed = 1;
			}

			n = node_new(NULL, sizeof(struct rule));
			if (n == NULL) {
				clog(LOG_ERR, "cannot make enough room for a new Rule (%s)\n",
						strerror(errno));
				fclose(fp_config);
				return -1;
			}
			tmp_rule = (struct rule *)n->content;
			if (parse_config_rule(fp_config, tmp_rule, &configuration->plugins) < 0) {
				clog(LOG_CRIT, "[Rule] error parsing %s, see logs for details.\n", 
						configuration->config_file);
				node_free(n);
				fclose(fp_config);
				return -1;
			}
				
			/* check duplicate names */
			LIST_FOREACH_NODE(node, &configuration->rules) {
				struct rule *tmp = (struct rule *)node->content;
				if (strcmp(tmp->name, tmp_rule->name) != 0)
					continue;
				
				clog(LOG_ERR, "[Rule] name \"%s\" already exists. Skipped\n", 
						tmp_rule->name);
				node_free(n);
			}
			/* check if there are options */
			if (tmp_rule->directives.first == NULL) {
				clog(LOG_CRIT, "[Rule] name \"%s\" has no options. Discarded.\n",
						tmp_rule->name);
				node_free(n);
				continue;
			}
			list_append(&configuration->rules, n);
			/* avoid continuing */
			continue;
		}

		/* try match a plugin name (case insensitive) */
		if ((plugin = plugin_handle_section(clean, &configuration->plugins)) != NULL) {
			configure_plugin(fp_config, plugin);
			continue;
		}
		clog(LOG_WARNING, "Unknown %s: nobody handles it.\n", clean);
		
	} /* end while */
	fclose(fp_config);

	/* did I read something? 
	 * check if I read at least one rule, otherwise exit
	 */
	if (configuration->rules.first == NULL) {
		clog(LOG_ERR, "ERROR! No rules found!\n");
		return -1;
	}

	/*
	 * associate rules->profiles
	 * go through rules and associate to the proper profile
	 */
	LIST_FOREACH_NODE(node, &configuration->rules) {
		tmp_rule = (struct rule *)node->content;
		int profile_found = 0;

		LIST_FOREACH_NODE(node1, &configuration->profiles) {
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
			clog(LOG_CRIT, "ERROR! no Profile section found for Rule \"%s\" "
					"(requested Profile \"%s\")\n", 
					tmp_rule->name, tmp_rule->profile_name);
			return -1;
		}
	}

	LIST_FOREACH_NODE(node, &configuration->rules) {
		tmp_rule = (struct rule *)node->content;
		clog(LOG_INFO, "Rule \"%s\" has Profile \"%s\"\n", 
				tmp_rule->name, tmp_rule->prof->name);
	}
	return 0;
}

/* 
 * Frees the structures allocated.
 */
void free_configuration(struct cpufreqd_conf *configuration)
{
	struct rule *tmp_rule;
	struct profile *tmp_profile;
	struct directive *tmp_directive;
	struct plugin_obj *o_plugin;

	/* cleanup rule directives */
	clog(LOG_INFO, "freeing rules directives.\n");
	LIST_FOREACH_NODE(node, &configuration->rules) {
		tmp_rule = (struct rule *) node->content;

		LIST_FOREACH_NODE(node1, &tmp_rule->directives) {
			tmp_directive = (struct directive *) node1->content;
			free_keyword_object(tmp_directive->keyword, tmp_directive->obj);
		}
		list_free_sublist(&tmp_rule->directives, tmp_rule->directives.first);
	}

	/* cleanup config structs */
	clog(LOG_INFO, "freeing rules.\n"); 
	list_free_sublist(&(configuration->rules), configuration->rules.first);

	/* cleanup profile directives */
	clog(LOG_INFO, "freeing profiles directives.\n");
	LIST_FOREACH_NODE(node, &configuration->profiles) {
		tmp_profile = (struct profile *) node->content;
		free(tmp_profile->policy.governor);
		LIST_FOREACH_NODE(node1, &tmp_profile->directives) {
			tmp_directive = (struct directive *) node1->content;
			free_keyword_object(tmp_directive->keyword, tmp_directive->obj);
		}
		list_free_sublist(&tmp_profile->directives, tmp_profile->directives.first);
	}
	clog(LOG_INFO, "freeing profiles.\n"); 
	list_free_sublist(&(configuration->profiles), configuration->profiles.first);

	/* clean other values */
	configuration->poll_interval = DEFAULT_POLL;
	configuration->has_sysfs = 0;
	configuration->enable_remote = 0;
	configuration->cpu_min_freq = 0;
	configuration->cpu_max_freq = 0;

	if (!configuration->log_level_overridden)
		configuration->log_level = DEFAULT_VERBOSITY;

	/* finalize plugins!!!! */
	/*
	 *  Unload plugins
	 */
	clog(LOG_INFO, "freeing plugins.\n"); 
	LIST_FOREACH_NODE(node, &configuration->plugins) {
		o_plugin = (struct plugin_obj*) node->content;
		finalize_plugin(o_plugin);
		close_plugin(o_plugin);
	}
	list_free_sublist(&(configuration->plugins), configuration->plugins.first);
}
