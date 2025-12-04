#ifndef VMEMORY_H
#define VMEMORY_H

typedef struct VMemory {
  int physical_memory;
  double physical_memory_used;

} VMemory;

void vmemory_init(VMemory* vmemory);

#endif