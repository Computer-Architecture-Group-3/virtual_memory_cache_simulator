#ifndef CACHE_H
#define CACHE_H

typedef enum ReplacementPolicy { RR, RND } ReplacementPolicy;

typedef struct Cache {
  // given values
  int cache_size;
  int block_size;
  int associativity;
  ReplacementPolicy policy;

  // calculated values

} Cache;

void cache_init(Cache* cache);

#endif