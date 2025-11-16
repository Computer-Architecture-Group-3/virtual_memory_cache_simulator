#ifndef CONFIG_H
#define CONFIG_H

#include "cache.h"
#include "vmemory.h"

typedef struct Config {
  Cache cache;
  VMemory vmemory;

} Config;

void Config_init(Config* config);

#endif