#ifndef CONFIG_H
#define CONFIG_H

#include "cache.h"
#include "vmemory.h"

typedef struct Config {
  Cache cache;
  VMemory vmemory;
  int instruction;
  int fileCount;
  char* filenames[3];

} Config;

void config_init(Config* config);

#endif