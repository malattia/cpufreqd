#include "cpufreqd_plugin.h"

struct plugin_obj {
  char name[256];
  void *library;
  struct cpufreqd_plugin *plugin;
  unsigned int used;
};


int     load_plugin           (struct plugin_obj *cp);
void    close_plugin          (struct plugin_obj *cp);
int     get_cpufreqd_object   (struct plugin_obj *cp);
int     initialize_plugin     (struct plugin_obj *cp);
int     finalize_plugin       (struct plugin_obj *cp);

