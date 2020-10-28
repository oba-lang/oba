#ifndef oba_builtin_h
#define oba_builtin_h

#include <time.h>
#include <unistd.h>

#include "oba.h"
#include "oba_core_modules.h"
#include "oba_value.h"

// TODO(kendal): Support error reporting in natives.
Value sleepNative(ObaVM* vm, int argc, Value* argv) {
  // Assume argc == 1 and argv != NULL
  Value seconds = argv[0];
  unsigned int remaining = (unsigned int)AS_NUMBER(seconds);
  while (remaining > 0)
    remaining = sleep(remaining);
  return OBA_NUMBER(0);
}

Value nowNative(ObaVM* vm, int argc, Value* argv) {
  return OBA_NUMBER((double)clock() / CLOCKS_PER_SEC);
}

Value readByteNative(ObaVM* vm, int argc, Value* argv) {
  int c;
  if ((c = getchar()) == EOF) {
    return NIL_VAL;
  }
  const char byte = (const char)c;
  return OBJ_VAL(copyString(vm, &byte, 1));
}

Value readLineNative(ObaVM* vm, int argc, Value* argv) {
  char* line = NULL;
  size_t length;
  if (getline(&line, &length, stdin) == -1) {
    return NIL_VAL;
    // TODO: if (feof(stdin)) { /* nil */ }  else { /* error */ }
  }
  Value value = OBJ_VAL(copyString(vm, line, length));
  free(line);
  return value;
}

Value formatNative(ObaVM* vm, int argc, Value* argv) {
  switch (argc) {
  case 0:
    return OBJ_VAL(takeString(vm, "", 0));
  case 1:
  default:
    // This should error if we have more than one arg but we don't have error
    // reporting yet.
    return OBJ_VAL(formatValue(vm, argv[0]));
  }
}

Value printNative(ObaVM* vm, int argc, Value* argv) {
  if (argc > 0) {
    printValue(argv[0]);
  }
  printf("\n");
  return NIL_VAL;
}

Value isNilNative(ObaVM* vm, int argc, Value* argv) {
  if (argc != 1) {
    return OBA_BOOL(false);
  }
  return OBA_BOOL(argv[0].type == VAL_NIL);
}

Builtin __builtins__[] = {
    {"__native_sleep", &sleepNative},
    {"__native_now", &nowNative},
    {"__native_read_byte", &readByteNative},
    {"__native_read_line", &readLineNative},
    {"__native_print", &printNative},
    {"isNil", &isNilNative},
    {"format", &formatNative},
    {NULL, NULL}, // Sentinel to mark the end of the array.
};

typedef const char* (*SourceLoader)();

typedef struct {
  const char* name;
  SourceLoader source;
} CoreModule;

extern const char* systemModSource;
extern const char* timeModSource;

CoreModule __core_modules__[] = {
    {"system", obaSystemModSource},
    {"time", obaTimeModSource},
    {NULL, NULL},
};

#endif
