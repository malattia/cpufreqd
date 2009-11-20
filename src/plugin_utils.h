/*
 *  Copyright (C) 2002-2005  Mattia Dongili <malattia@linux.it>
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

#include "cpufreqd_plugin.h"
#include "list.h"

struct plugin_obj {
	char name[256];
	void *library;
	struct cpufreqd_plugin *plugin;
	unsigned int used;		/* track if the plugin is used
					eg. if a Rule/Profile contains
					any of its directives */
	unsigned int configured;	/* track if the plugin has
					already been configured */
};

void	discover_plugins	(struct LIST *plugins);
void	load_plugin_list	(struct LIST *plugins);
int	validate_plugins	(struct LIST *plugins);
int     load_plugin		(struct plugin_obj *cp);
void    close_plugin		(struct plugin_obj *cp);
int     get_cpufreqd_object	(struct plugin_obj *cp);
int     initialize_plugin	(struct plugin_obj *cp);
int     finalize_plugin		(struct plugin_obj *cp);
void	update_plugin_states	(struct LIST *plugins);
void	plugins_post_conf	(struct LIST *plugins);

struct plugin_obj *plugin_handle_section
	(const char *name, struct LIST *plugins);

struct cpufreqd_keyword *plugin_handle_keyword
	(struct LIST *plugins, const char *key, const char *value, void **obj,
	 struct cpufreqd_plugin **plugin);

void	free_keyword_object	(struct cpufreqd_keyword *k, void *obj);

