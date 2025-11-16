#include "config.h"

#include "cache.h"
#include "vmemory.h"

void Config_init(Config* config) {
  if (!config) {
    // TODO add message
    return;
  }

  cache_init(&config->cache);
  vmemory_init(&config->vmemory);
}
