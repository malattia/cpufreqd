
/*  Note: gestione della riga di una regola?? Ogni plugin deve fornire
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
   * be matched. If the system state matches the function must return 1 otherwise 0.
   */
  int (*evaluate) (const void *ev);
};

/*
 *  A cpufreqd plugin is a collection of function and setting able to
 *  monitor some kind of system resource/state and tell if the present
 *  state is conformant to the one configured.
 *  cpufreqd plugins must be decalared static to avoid symbol clashes.
 */
struct cpufreqd_plugin {
  
  /****************************************
   *  PLUGIN IDENTIFICATION AND SETTINGS  *
   ****************************************/
  /* plugin name, must be unique (see README.plugins) */
  const char *plugin_name;
  
  /* char array of keywords triggering the parse function */
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

  /* Update plugin data. Must return either
   * CPUFREQD_SYS_STATUS_UNCHANGED or CPUFREQD_SYS_STATUS_CHANGED. The
   * return value will eventually trigger a CPUFREQD_EVENT_SYS_CHANGE
   * event */
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
 *  core cpufred with the correct struct cpufreqd_plugin structure
 */
struct cpufreqd_plugin *create_plugin(void);

