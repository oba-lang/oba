#ifndef oba_builtin_h
#define oba_builtin_h

#include <time.h>
#include <unistd.h>

#include "oba.h"
#include "oba_core_modules.h"
#include "oba_value.h"
#include "oba_vm.h"

Value panicNative(ObaVM* vm, int argc, Value* argv) {
  if (argc > 0) {
    vm->error = argv[0];
  } else {
    vm->error = OBJ_VAL(copyString(vm, "panic", 5));
  }
  return NIL_VAL;
}

#define ASSERT_ARITY(vm, argc, arity)                                          \
  do {                                                                         \
    if ((argc) != (arity)) {                                                   \
      obaArityError(vm, arity, argc);                                          \
      return NIL_VAL;                                                          \
    }                                                                          \
  } while (0)

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

Value toStringNative(ObaVM* vm, int argc, Value* argv) {
  ASSERT_ARITY(vm, argc, 1);
  char buf[FORMAT_VALUE_MAX];
  int length = formatValue(vm, buf, argv[0]);
  return OBJ_VAL(copyString(vm, buf, length));
}

Builtin __builtins__[] = {
    {"__native_sleep", &sleepNative},
    {"__native_now", &nowNative},
    {"__native_read_byte", &readByteNative},
    {"__native_read_line", &readLineNative},
    {"__native_print", &printNative},
    {"isNil", &isNilNative},
    {"toString", &toStringNative},
    {"panic", &panicNative},
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
    {"option", obaOptionModSource},
    {NULL, NULL},
};

#endif
