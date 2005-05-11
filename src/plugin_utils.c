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
#include <dlfcn.h>
#include "plugin_utils.h"
#include "main.h"
#include "cpufreqd_log.h"
#include "cpufreqd.h"

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
  dlclose(cp->library);
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
  /* uh! make gcc-3.4 happy with -pedantic... */
  /**(void **) (&create) = dlsym(cp->library, "create_plugin");*/
  error = dlerror();
  if (error) {
    cpufreqd_log(LOG_ERR, "get_cpufreqd_object(): %s\n", error);
    return -1;
  }
  cp->plugin = (*create)();

  return 0;
}

/*  int initialize_plugin(struct plugin_obj *cp)
 *  Call plugin_init()
 */
int initialize_plugin(struct plugin_obj *cp) {
  int ret = 0;
  cpufreqd_log(LOG_INFO, "Initializing plugin \"%s-%s\".\n", cp->name, cp->plugin->plugin_name);
  /* set logger function for this plugin */
  cp->plugin->cfdprint = &cpufreqd_log;
  /* call init function */
  if (cp->plugin->plugin_init != NULL) {
     ret = (*(cp->plugin->plugin_init))();
  }
  return ret;
}

/*  int finalize_plugin(struct plugin_obj *cp)
 *  Call plugin_exit()
 */
int finalize_plugin(struct plugin_obj *cp) {
  if (cp != NULL) {
    cpufreqd_log(LOG_INFO, "Finalizing plugin \"%s-%s\".\n", cp->name, cp->plugin->plugin_name);
    /* call exit function */
    (*(cp->plugin->plugin_exit))();
    return -1;
  }
  return 0;
}


