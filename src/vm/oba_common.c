#include "oba_common.h"

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  void* result = realloc(pointer, newSize);
  // Fail fast if we can't get the requested memory.
  if (result == NULL) exit(1);
  return result;
}
