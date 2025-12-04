#include "cache.h"

// initialize everything to 0
void cache_init(Cache* cache) {
  if (!cache) {
    // TODO add message
    return;
  }

  cache->cache_size = 0;
  cache->block_size = 0;
  cache->associativity = 0;
  cache->policy = 0;
}
