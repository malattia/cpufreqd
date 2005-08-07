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

#ifndef __CPUFREQD_PLUGIN_H__
#define __CPUFREQD_PLUGIN_H__

#include <cpufreq.h>

#define MATCH       1
#define DONT_MATCH  0

struct cpufreqd_plugin;

/* 
 *  A cpufreqd keyword consists of the proper word to match at the
 *  beginning of the Rule line. The struct consists of two other function
 *  pointers, one will provide the function to be called if the keyword
 *  being considered matches with *word, the sencond will provide a function to
 *  be called during the main loop that will evaluate the current
 *  system state (as read by the same plugin) against it.
 *
 *  At least one out of evaluate, pre_change, post_change MUST be defined by the
 *  plugin.
 */
struct cpufreqd_keyword {
  const char *word;

  /* function pointer to the keyword parser. line is a config file line and obj
   * must point to a structure that will be used by the evaulate function 
   */
  int (*parse) (const char *line, void **obj);

  /* function pointer to the evaluator. ev is the structure provided by 
   * the parse function and that represent the system state that must eventually
   * be matched. If the system state matches the function must return MATCH (1) 
   * otherwise DONT_MATCH (0).
   *
   * Can be NULL
   */
  int (*evaluate) (const void *ev);

  /* function pointer to the pre_change event. ev is the structure previously
   * provided by the parse function, old and new are the old and new policy
   * pointer respctively.
   * The function is called prior to the call to set_policy() when a new Rule
   * applies the current system state. Note however that set_policy() will not
   * be called if the Profile doesn't change (you can tell that by comparing the
   * old and new policy pointers, if they are the same then set_policy() won't
   * be called).
   *
   * Can be NULL
   */
  void (*pre_change) (const void *ev, const struct cpufreq_policy *old,
		  const struct cpufreq_policy *new);

  /* function pointer to the post_change event. The same as pre_change applies
   * except for the fact that everything is referred tto _after_ set_policy()
   * has been called.
   *
   * Can be NULL
   */
  void (*post_change) (const void *ev, const struct cpufreq_policy *old,
		  const struct cpufreq_policy *new);

  /* Allows the owner to define a specific function to be called when freeing
   * malloced during the 'parse' call. Not required, if missing a libc call to
   * 'free' is performed with the same obj argument.
   *
   * Can be NULL
   */
  void (*free) (void *obj);
};

/*
 *  A cpufreqd plugin is a collection of functions and settings able to
 *  monitor some kind of system resource/state and tell if the present
 *  state is conformant to the one configured.
 *  cpufreqd plugins must be decalared static to avoid symbol clashes.
 */
struct cpufreqd_plugin {
  
  /****************************************
   *  PLUGIN IDENTIFICATION AND SETTINGS  *
   ****************************************/
  /* plugin name, must be unique (see README.plugins?) */
  const char *plugin_name;
  
  /* array of keywords handled by this plugin */
  struct cpufreqd_keyword *keywords;

  /* Interval between each poll (ms) */
  unsigned long poll_interval;

  /* NOT NECESSARY?? see plugin_event.
   *
   * Under wich circustances the plugin will be used */
  /*int cpufreqd_event;*/
  

  /************************
   *  FUNCTION POINTERS   *
   ************************/
  /* Plugin intialization */
  int (*plugin_init) (void);
  
  /* Plugin cleanup */
  int (*plugin_exit) (void);

  /* Update plugin data */
  int (*plugin_update) (void);

  /* Pre rule-change */
#if 0
  void (*pre_rule_change) (void *obj, struct cpufreq_policy *new_policy,
		  struct cpufreq_policy *old_policy);
#endif
  /* Post rule-change */
  /* Pre policy-change */
  /* Post policy-change */

  /************************
   *  FUNCTION POINTERS   *
   ************************/
  /* core provided after create_plugin is called */

  /* function pointer to the main log function */
  void (*cfdprint) (const int prio, const char *fmt, ...);

};

/*
 *  A cpufreqd plugin MUST define the following function to provide the
 *  core cpufreqd with the correct struct cpufreqd_plugin structure
 */
struct cpufreqd_plugin *create_plugin(void);

#endif
