/*
 *  Copyright (C) 2002-2005  Mattia Dongili <malattia@gmail.com>
 *                           George Staikos <staikos@0wned.org>
 *  2004.10.12
 *  - reworked to support new plugin architecture
 *    by Mattia Dongili
 *  
 *  2004.08.22
 *  - added percentage/absolute frequency translation based on a patch submitted
 *    by Hervé Eychenne
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
				cpufreqd_log(LOG_WARNING, 
						"WARNING: [General] poll_interval has invalid value, "
						"using default.\n");
				configuration->poll_interval = DEFAULT_POLL;
			}
			continue;
		}

		if (strcmp(name,"verbosity") == 0) {
			if (configuration->log_level_overridden) {
				cpufreqd_log(LOG_DEBUG, 
						"parse_config_general(): skipping \"verbosity\", "
						"overridden in the command line.\n");
				continue;
			}

			if (value != NULL) {
				configuration->log_level = atoi (value);
				/* validate */
				if (configuration->log_level < 0 || configuration->log_level > 7) {
					cpufreqd_log(LOG_WARNING, 
							"WARNING: [General] verbosity has invalid value, "
							"using default (%d).\n", DEFAULT_VERBOSITY);
					configuration->log_level = DEFAULT_VERBOSITY;
				}
			} else {
				configuration->log_level = DEFAULT_VERBOSITY;
				cpufreqd_log(LOG_WARNING, 
						"WARNING: [General] verbosity has empty value, "
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
				cpufreqd_log(LOG_DEBUG, "parse_config_general(): read plugin: %s\n", token);

			} while ((token = strtok(NULL,",")) != NULL);
			continue;

		}

		if (strcmp(name,"pidfile") == 0) {
			if (value != NULL) {
				strncpy(configuration->pidfile, value, MAX_PATH_LEN);
			} else {
				cpufreqd_log(LOG_WARNING, 
						"parse_config_general(): empty \"pidfile\", "
						"using default %s.\n", CPUFREQD_PIDFILE);
				strncpy(configuration->pidfile, CPUFREQD_PIDFILE, MAX_PATH_LEN);
			}
			configuration->pidfile[MAX_PATH_LEN-1] = '\0';
			continue;
		}

		if (strcmp(name,"double_check") == 0) {
			if (value != NULL) {
				configuration->double_check = atoi (value);
				cpufreqd_log(LOG_WARNING, "parse_config_general(): "
						"double check profiles %s.\n", 
						configuration->double_check ? "enabled" : "disabled");
			}
			continue;
		}
		if (strcmp(name,"enable_remote") == 0) {
			if (value != NULL) {
				configuration->enable_remote = atoi (value);
				cpufreqd_log(LOG_WARNING, "parse_config_general(): "
						"Remote control %s.\n", 
						configuration->enable_remote ? "enabled" : "disabled");
			}
			continue;
		}

		cpufreqd_log(LOG_WARNING, "WARNING: [General] skipping config option \"%s\"\n", name);
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
				cpufreqd_log(LOG_ERR, "parse_config_profile(): "
						"unable to parse MIN value %s.\n", value);
				return -1;
			}
			if (strstr(value, "%") != NULL) {
				min_is_percent=1;
			}
			if (tmp_freq < 0) {
				cpufreqd_log(LOG_NOTICE, "parse_config_profile(): "
						"MIN freq below 0, resetting\n");
				p->policy.min = 0;
			} else {
				p->policy.min = tmp_freq;
			}
			state |= HAS_MIN;
			continue;
		}
		
		if (strcmp(name,"maxfreq") == 0) {
			if (sscanf(value, "%d", &tmp_freq) != 1) {
				cpufreqd_log(LOG_ERR, "parse_config_profile(): "
						"unable to parse MAX value %s.\n", value);
				return -1;
			}
			if (strstr(value, "%") != NULL) {
				max_is_percent=1;
			}
			if (tmp_freq < 0) {
				cpufreqd_log(LOG_NOTICE, "parse_config_profile(): "
						"MAX freq below 0, resetting\n");
				p->policy.max = 0;
			} else {
				p->policy.max = tmp_freq;
			}
			state |= HAS_MAX;
			continue;
		}
		
		if (strcmp(name,"cpu") == 0) {
			if (sscanf(value, "%u", &(p->cpu)) != 1) {
				cpufreqd_log(LOG_ERR, "parse_config_profile(): "
						"unable to parse CPU value %s.\n", value);
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
				cpufreqd_log(LOG_ERR, "parse_config_profile(): [Profile] cannot "
						"make enough room for a new entry (%s).\n",
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

		cpufreqd_log(LOG_WARNING, "WARNING: [Profile] "
				"skipping config option \"%s\"\n", name);
	} /* end while */

	if (!(state & HAS_NAME)) {
		cpufreqd_log(LOG_ERR, "parse_config_profile(): [Profile] "
				"missing required property \"name\".\n");
		return -1;
	}
	if (!(state & HAS_MIN)) {
		cpufreqd_log(LOG_ERR, 
				"parse_config_profile(): [Profile] \"%s\" "
				"missing required property \"minfreq\".\n", p->name);
		return -1;
	}
	if (!(state & HAS_MAX)) {
		cpufreqd_log(LOG_ERR, 
				"parse_config_profile(): [Profile] \"%s\" "
				"missing required property \"maxfreq\".\n", p->name);
		return -1;
	}
	if (!(state & HAS_POLICY)) {
		cpufreqd_log(LOG_ERR, 
				"parse_config_profile(): [Profile] \"%s\" "
				"missing required property \"policy\".\n", p->name);
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
		/* normalize frequencies if such informations are available 
		 *
		 * TODO: move this to init_configuration() ?
		 */
		p->policy.max = normalize_frequency(limits, freq, p->policy.max);
		p->policy.min = normalize_frequency(limits, freq, p->policy.min);
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
				cpufreqd_log(LOG_ERR, "parse_config_rule(): [Rule] cannot "
						"make enough room for a new entry (%s).\n",
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

		cpufreqd_log(LOG_WARNING, "WARNING: [Rule] "
				"skipping config option \"%s\"\n", name);
	} /* end while */

	if (!(state & HAS_NAME)) {
		cpufreqd_log(LOG_ERR, "parse_config_rule(): [Rule] "
				"missing required property \"name\".\n");
		return -1;
	}

	if (!(state & HAS_PROFILE)) {
		cpufreqd_log(LOG_ERR, "parse_config_rule(): [Rule] \"%s\" "
				"missing required property \"profile\".\n", 
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
			cpufreqd_log(LOG_WARNING, "plugin \"%s\" can't handle %s.\n",
					plugin->plugin->plugin_name, name);
		}
	}
}

void deconfigure_plugin(struct cpufreqd_conf *configuration, struct plugin_obj *plugin);
void deconfigure_plugin(struct cpufreqd_conf *configuration, struct plugin_obj *plugin) {
	struct rule *tmp_rule = NULL;
	struct profile *tmp_profile = NULL;
	struct directive *d = NULL;
	struct NODE *node1 = NULL;
	
	/* discard plugin related rule directives */
	LIST_FOREACH_NODE(node, &configuration->rules) {
		tmp_rule = (struct rule *)node->content;
		node1 = tmp_rule->directives.first;
		while (node1 != NULL) {
			d = (struct directive *)node1->content;
			if (d->plugin == plugin->plugin) {
				cpufreqd_log(LOG_DEBUG, "%s: removing %s Rule directive %s\n",
						__func__, tmp_rule->name,
						d->keyword->word);
				free_keyword_object(d->keyword, d->obj);
				tmp_rule->directives_count--;
				node1 = list_remove_node(&tmp_rule->directives, node1);
			} else
				node1 = node1->next;
		}
	}

	/* same for profiles */
	LIST_FOREACH_NODE(node, &configuration->profiles) {
		tmp_profile = (struct profile *)node->content;
		node1 = tmp_profile->directives.first;
		while (node1 != NULL) {
			d = (struct directive *)node1->content;
			if (d->plugin == plugin->plugin) {
				cpufreqd_log(LOG_DEBUG, "%s: removing %s Profile directive %s\n",
						__func__, tmp_profile->name,
						d->keyword->word);
				free_keyword_object(d->keyword, d->obj);
				tmp_profile->directives_count--;
				node1 = list_remove_node(&tmp_profile->directives, node1);
			} else
				node1 = node1->next;
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

	/* configuration file */
	cpufreqd_log(LOG_INFO, "init_configuration(): reading configuration file %s\n",
			configuration->config_file);
	fp_config = fopen(configuration->config_file, "r");
	if (!fp_config) {
		cpufreqd_log(LOG_ERR, "init_configuration(): %s: %s\n",
				configuration->config_file, strerror(errno));
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

			if (parse_config_profile(fp_config, tmp_profile, &configuration->plugins, 
					configuration->limits, configuration->sys_info->frequencies) < 0) {
				cpufreqd_log(LOG_CRIT, 
						"init_configuration(): [Profile] "
						"error parsing %s, see logs for details.\n",
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
				cpufreqd_log(LOG_CRIT, 
						"init_configuration(): [Profile] "
						"name \"%s\" already exists.\n", 
						tmp_profile->name);
				node_free(n);
			}
			list_append(&configuration->profiles, n);
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
			if (parse_config_rule(fp_config, tmp_rule, &configuration->plugins) < 0) {
				cpufreqd_log(LOG_CRIT, 
						"init_configuration(): [Rule] "
						"error parsing %s, see logs for details.\n", 
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
			list_append(&configuration->rules, n);
			/* avoid continuing */
			continue;
		}

		/* try match a plugin name (case insensitive) */
		if ((plugin = plugin_handle_section(clean, &configuration->plugins)) != NULL) {
			configure_plugin(fp_config, plugin);
			continue;
		}
		cpufreqd_log(LOG_WARNING, "Unknown %s: nobody handles it.\n", clean);
		
	} /* end while */
	fclose(fp_config);

	/* did I read something? 
	 * check if I read at least one rule, otherwise exit
	 */
	if (configuration->rules.first == NULL) {
		cpufreqd_log(LOG_ERR, "init_configuration(): No rules found!\n");
		return -1;
	}
	
	/* plugin POST CONFIGURATION */
	LIST_FOREACH_NODE(node, &configuration->plugins) {
		plugin = (struct plugin_obj *) node->content;
		/* try to post-configure the plugin */
		if (plugin->plugin->plugin_post_conf != NULL &&
				plugin->plugin->plugin_post_conf() != 0) {
			cpufreqd_log(LOG_ERR, "Unable to configure plugin %s, removing\n",
					plugin->plugin->plugin_name);

			deconfigure_plugin(configuration, plugin);
			/* mark unused, will be removed later */
			plugin->used = 0;
		}
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
			cpufreqd_log(LOG_CRIT, "init_configuration()-Syntax error: "
					"no Profile section found for Rule \"%s\" "
					"(requested Profile \"%s\")\n", 
					tmp_rule->name, tmp_rule->profile_name);
			return -1;
		}
	}

	LIST_FOREACH_NODE(node, &configuration->rules) {
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
void free_configuration(struct cpufreqd_conf *configuration)
{
	struct rule *tmp_rule;
	struct profile *tmp_profile;
	struct directive *tmp_directive;
	struct plugin_obj *o_plugin;

	/* cleanup rule directives */
	cpufreqd_log(LOG_INFO, "free_config(): freeing rules directives.\n");
	LIST_FOREACH_NODE(node, &configuration->rules) {
		tmp_rule = (struct rule *) node->content;

		LIST_FOREACH_NODE(node1, &tmp_rule->directives) {
			tmp_directive = (struct directive *) node1->content;
			free_keyword_object(tmp_directive->keyword, tmp_directive->obj);
		}
		list_free_sublist(&tmp_rule->directives, tmp_rule->directives.first);
	}

	/* cleanup config structs */
	cpufreqd_log(LOG_INFO, "free_config(): freeing rules.\n"); 
	list_free_sublist(&(configuration->rules), configuration->rules.first);

	/* cleanup profile directives */
	cpufreqd_log(LOG_INFO, "free_config(): freeing profiles directives.\n");
	LIST_FOREACH_NODE(node, &configuration->profiles) {
		tmp_profile = (struct profile *) node->content;
		free(tmp_profile->policy.governor);
		LIST_FOREACH_NODE(node1, &tmp_profile->directives) {
			tmp_directive = (struct directive *) node1->content;
			free_keyword_object(tmp_directive->keyword, tmp_directive->obj);
		}
		list_free_sublist(&tmp_profile->directives, tmp_profile->directives.first);
	}
	cpufreqd_log(LOG_INFO, "free_config(): freeing profiles.\n"); 
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
	cpufreqd_log(LOG_INFO, "free_config(): freeing plugins.\n"); 
	LIST_FOREACH_NODE(node, &configuration->plugins) {
		o_plugin = (struct plugin_obj*) node->content;
		finalize_plugin(o_plugin);
		close_plugin(o_plugin);
	}
	list_free_sublist(&(configuration->plugins), configuration->plugins.first);
}
