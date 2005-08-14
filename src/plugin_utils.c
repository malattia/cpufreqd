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
#include <string.h>
#include <dlfcn.h>
#include "plugin_utils.h"
#include "cpufreqd_log.h"
#include "cpufreqd.h"

/*
 *  Load plugins from a list of plugin_obj's. Also cleanup the
 *  list if a plugin fails to load
 */
void load_plugin_list(struct LIST *plugins) {
	struct plugin_obj *o_plugin = NULL;
	struct NODE *n = NULL;
	
	n = plugins->first;
	while (n != NULL) {
		o_plugin = (struct plugin_obj*)n->content;
		/* take care!! if statement badly indented!! */
		if (load_plugin(o_plugin) == 0 &&
				get_cpufreqd_object(o_plugin) == 0 &&
				initialize_plugin(o_plugin) == 0) { 
			cpufreqd_log(LOG_INFO, "plugin loaded: %s\n", o_plugin->plugin->plugin_name);
			n=n->next;

		} else {
			cpufreqd_log(LOG_INFO, "plugin failed to load: %s\n", o_plugin->name);
			/* remove the list item and assing n the next node (returned from list_remove_node) */
			cpufreqd_log(LOG_NOTICE, "discarded plugin %s\n", o_plugin->name);
			n = list_remove_node(plugins, n);
		} /* end else */
	} /* end while */
}

/* Validate plugins after parsing the configuration, an unused
 * plugin is unloaded and removed from the list.
 *
 * Returns the number of remaining plugins.
 */
int validate_plugins(struct LIST *plugins) {
	struct plugin_obj *o_plugin = NULL;
	struct NODE *n = NULL;
	int used_plugins = 0;

	n = plugins->first;
	while (n != NULL) {
		o_plugin = (struct plugin_obj*)n->content;
		if (o_plugin->used != 0) {
			used_plugins++;
			n = n->next;
		} else {
			finalize_plugin((struct plugin_obj*)n->content);
			close_plugin((struct plugin_obj*)n->content);
			n = list_remove_node(plugins, n);
		}
	}
	return used_plugins;
}
	
/*  int load_plugin(struct plugin_obj *cp)
 *  Open shared libraries
 */
int load_plugin(struct plugin_obj *cp) {
	char libname[512];

	snprintf(libname, 512, CPUFREQD_LIBDIR"cpufreqd_%s.so", cp->name);

	cpufreqd_log(LOG_INFO, "Loading \"%s\" for plugin \"%s\".\n", libname, cp->name);
	cp->library = dlopen(libname, RTLD_LAZY);
	if (!cp->library) {
		cpufreqd_log(LOG_ERR, "load_plugin(): %s\n", dlerror());
		return -1;
	}

	return 0;
}

/*  void close_plugin(struct plugin_obj *cp)
 *  Close shared libraries
 */
void close_plugin(struct plugin_obj *cp) {
	/* close library */
	if (dlclose(cp->library) != 0) {
		cpufreqd_log(LOG_ERR, "Error unloading plugin %s: %s\n", cp->name, dlerror());
	}
}

/*  int get_cpufreqd_object(struct plugin_obj *cp)
 *  Calls the create_plugin routine.
 */
int get_cpufreqd_object(struct plugin_obj *cp) {

	/* pointer to an error message, if any */
	const char* error;    
	/* plugin ptr */
	struct cpufreqd_plugin *(*create)(void);

	cpufreqd_log(LOG_INFO, "Getting plugin object for \"%s\".\n", cp->name);
	/* create plugin */
	create = (struct cpufreqd_plugin * (*) (void))dlsym(cp->library, "create_plugin");
	/* uh! the following makes gcc-3.4 happy with -pedantic... */
	/* *(void **) (&create) = dlsym(cp->library, "create_plugin"); */
	error = dlerror();
	if (error) {
		cpufreqd_log(LOG_ERR, "get_cpufreqd_object(): %s\n", error);
		return -1;
	}
	cp->plugin = create();

	return 0;
}

/*  int initialize_plugin(struct plugin_obj *cp)
 *  Call plugin_init()
 */
int initialize_plugin(struct plugin_obj *cp) {
	int ret = 0;
	cpufreqd_log(LOG_INFO, "Initializing plugin \"%s-%s\".\n",
			cp->name, cp->plugin->plugin_name);
	/* call init function */
	if (cp->plugin->plugin_init != NULL) {
		ret = cp->plugin->plugin_init();
	}
	return ret;
}

/*  int finalize_plugin(struct plugin_obj *cp)
 *  Call plugin_exit()
 */
int finalize_plugin(struct plugin_obj *cp) {
	if (cp != NULL && cp->plugin->plugin_exit != NULL) {
		cpufreqd_log(LOG_INFO, "Finalizing plugin \"%s-%s\".\n",
				cp->name, cp->plugin->plugin_name);
		/* call exit function */
		cp->plugin->plugin_exit();
		return -1;
	}
	return 0;
}

/* void update_plugin_states(struct LIST *plugins)
 * calls plugin_update() for every plugin in the list
 */
void update_plugin_states(struct LIST *plugins) {
	struct plugin_obj *o_plugin;

	/* update plugin states */
	LIST_FOREACH_NODE(node, plugins) {
		o_plugin = (struct plugin_obj*)node->content;
		if (o_plugin != NULL && o_plugin->used > 0 && 
				o_plugin->plugin->plugin_update != NULL) {
			o_plugin->plugin->plugin_update();
		}
	}
}

/* 
 * Looks for a plugin handling the key keyword, calls its parse function
 * and assigns the obj as returned by the plugin. Returns the struct
 * cpufreqd_keyword handling the keyword or NULL if no plugin handles the
 * keyword or if an error occurs parsing the value.
 * NOTE: the value of obj is significant only if the function returns non-NULL.
 */
struct cpufreqd_keyword *plugin_handle_keyword(struct LIST *plugins,
		const char *key, const char *value, void **obj) {
	struct plugin_obj *o_plugin = NULL;
	struct cpufreqd_keyword *ckw = NULL;
	
	/* foreach plugin */
	LIST_FOREACH_NODE(node, plugins) {
		o_plugin = (struct plugin_obj*)node->content;
		if (o_plugin==NULL || o_plugin->plugin==NULL || o_plugin->plugin->keywords==NULL)
			continue;

		/* foreach keyword */
		for(ckw = o_plugin->plugin->keywords; ckw->word != NULL; ckw++) {

			/* if keyword corresponds
			 * (TODO: use strncmp and bound check the string?)
			 */
			if (strcmp(ckw->word, key) != 0)
				continue;

			cpufreqd_log(LOG_DEBUG, "Plugin %s handles keyword %s (value=%s)\n",
					o_plugin->plugin->plugin_name, key, value);

			if (ckw->parse(value, obj) != 0) {
				cpufreqd_log(LOG_ERR, 
						"%s: %s is unable to parse this value \"%s\". Discarded\n",
						__func__, o_plugin->plugin->plugin_name, value);
				return NULL;
			}
			/* increase plugin use count */
			o_plugin->used++;
			return ckw;
		}
	}
	cpufreqd_log(LOG_NOTICE, "%s: unandled keyword \"%s\". Discarded\n", __func__, key);
	return NULL;
}

/*
 * Tries to free the object using the plugin provided free function.
 * Falls back to the libc function.
 */
void free_keyword_object(struct cpufreqd_keyword *k, void *obj) {
	if (k->free != NULL)
		k->free(obj);
	else 
		free(obj);
}
