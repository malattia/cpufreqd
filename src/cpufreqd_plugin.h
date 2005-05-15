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

/*  ...Sorry... notes in italian.
 *  Note: gestione della riga di una regola?? Ogni plugin deve fornire
 *  le keyword associate ad esso. Deve inoltre fornire essere in grado
 *  di dire se il valore attuale letto e' confacente la regola che il
 *  core sta analizzando.
 *
 *  Il core deve essere in grado di accettare i tipi di valore
 *  cpufreqd_value forniti dal plugin in corrispondenza di ogni sua
 *  keyword e di confrontare questi valori con le regole configurate.
 *
 *  oppure
 *  
 *  Il core deve rappresentare le regole parserizzate dal plugin e
 *  mantenere un puntatore alla funzione in grado di valutare
 *  l'espressione. Il plugin e' responsabile della valutazione della
 *  regola e deve dire se tale regola e conforme allo stato attuale del
 *  sistema.
 */

#define MATCH       1
#define DONT_MATCH  0

struct cpufreqd_plugin;

/* 
 *  A cpufreqd keyword consists of the proper word to match at the
 *  beginning of the Rule line. The struct consists of two other function
 *  pointers, one will provide the function to be called if the keyword
 *  being considered matches, the sencond will provide a function to be
 *  called during the main loop and that will evaluate the current
 *  system state (as read by the same plugin) against it.
 */
struct cpufreqd_keyword {
  const char *word;
  /*int score;*/

  /* function pointer to the keyword parser. line is a config file line and obj
   * must point to a structure that will be used by the evaulate function 
   */
  int (*parse) (const char *line, void **obj);

  /* function pointer to the evaluator. ev is the structure provided by 
   * the parse function and that represent the system state that must eventually
   * be matched. If the system state matches the function must return MATCH (1) 
   * otherwise DONT_MATCH (0).
   */
  int (*evaluate) (const void *ev);
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

