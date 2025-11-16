#include "vmemory.h"

void vmemory_init(VMemory* vmemory) {
  if (!vmemory) {
    // TODO message
    return;
  }

  vmemory->physical_memory = 0;
  vmemory->physical_memory_used = 0;
}