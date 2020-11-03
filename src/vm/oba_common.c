#include "oba_common.h"
#include "oba_vm.h"

void* reallocate(ObaVM* vm, void* pointer, size_t oldSize, size_t newSize) {
  vm->bytesAllocated += newSize - oldSize;

#ifdef DEBUG_STRESS_GC
  if (newSize > oldSize) {
    obaCollectGarbage(vm);
  }
#else
  if (newSize > oldSize && vm->bytesAllocated > vm->nextGC) {
    obaCollectGarbage(vm);
  }
#endif

  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  void* result = realloc(pointer, newSize);

  // Fail fast if we can't get the requested memory.
  if (result == NULL) exit(1);
  return result;
}
