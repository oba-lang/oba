#ifndef oba_builtin_h
#define oba_builtin_h

#include <time.h>
#include <unistd.h>

#include "oba.h"
#include "oba_core_modules.h"
#include "oba_value.h"
#include "oba_vm.h"

// Native Functions -----------------------------------------------------------

#define ASSERT_ARITY(vm, argc, arity)                                          \
  do {                                                                         \
    if ((argc) != (arity)) {                                                   \
      obaArityError(vm, arity, argc);                                          \
      return NIL_VAL;                                                          \
    }                                                                          \
  } while (0)

Value __native_panic(ObaVM* vm, int argc, Value* argv) {
  switch (argc) {
  case 0:
    obaErrorf(vm, "panic");
    break;
  case 1:
    vm->error = argv[0];
    break;
  default:
    obaErrorf(vm, "expected 0 or 1 arguments but got %d", argc);
    break;
  }
  return NIL_VAL;
}

Value __native_sleep(ObaVM* vm, int argc, Value* argv) {
  ASSERT_ARITY(vm, argc, 1);
  Value seconds = argv[0];
  unsigned int remaining = (unsigned int)AS_NUMBER(seconds);
  while (remaining > 0) remaining = sleep(remaining);
  return OBA_NUMBER(0);
}

Value __native_now(ObaVM* vm, int argc, Value* argv) {
  ASSERT_ARITY(vm, argc, 0);
  return OBA_NUMBER((double)clock() / CLOCKS_PER_SEC);
}

Value __native_read_byte(ObaVM* vm, int argc, Value* argv) {
  ASSERT_ARITY(vm, argc, 0);
  int c;

  if ((c = getchar()) == EOF) {
    return NIL_VAL;
  }
  const char byte = (const char)c;
  return OBJ_VAL(copyString(vm, &byte, 1));
}

Value __native_read_line(ObaVM* vm, int argc, Value* argv) {
  ASSERT_ARITY(vm, argc, 0);
  char* line = NULL;
  size_t length;

  if (getline(&line, &length, stdin) == -1) {
    // TODO(kendal): produce some sort of error for the caller to handle.
    if (!feof(stdin)) {
      vm->error = OBJ_VAL(copyString(vm, "read", 4));
    }
    return NIL_VAL;
  }

  Value value = OBJ_VAL(copyString(vm, line, length));
  free(line);
  return value;
}

Value __native_print(ObaVM* vm, int argc, Value* argv) {
  ASSERT_ARITY(vm, argc, 1);
  printValue(argv[0]);
  return NIL_VAL;
}

Value __native_println(ObaVM* vm, int argc, Value* argv) {
  ASSERT_ARITY(vm, argc, 1);
  printValue(argv[0]);
  printf("\n");
  return NIL_VAL;
}

Value __native_str(ObaVM* vm, int argc, Value* argv) {
  ASSERT_ARITY(vm, argc, 1);
  char buf[FORMAT_VALUE_MAX];
  int length = formatValue(vm, buf, argv[0]);
  return OBJ_VAL(copyString(vm, buf, length));
}

// Exports a symbol from a module as a VM global, available to all modules.
// This can only be called by the builtin module which is packaged with the VM.
Value __native_global(ObaVM* vm, int argc, Value* argv) {
  if (vm->allowGlobals) {
    ASSERT_ARITY(vm, argc, 2);
    tableSet(vm->globals, AS_STRING(argv[0]), argv[1]);
  } else {
    obaErrorf(vm, "illegal global definition");
  }
  return NIL_VAL;
}

Value __native_is_nil(ObaVM* vm, int argc, Value* argv) {
  ASSERT_ARITY(vm, argc, 1);
  return OBA_BOOL(valuesEqual(argv[0], NIL_VAL));
}

Builtin __builtins__[] = {
    {"__native_sleep", &__native_sleep},
    {"__native_now", &__native_now},
    {"__native_read_byte", &__native_read_byte},
    {"__native_read_line", &__native_read_line},
    {"__native_print", &__native_print},
    {"__native_println", &__native_println},
    {"__native_global", &__native_global},
    {"__native_is_nil", &__native_is_nil},
    {"str", &__native_str},
    {"panic", &__native_panic},
    {NULL, NULL}, // Sentinel to mark the end of the array.
};

// Core Modules ----------------------------------------------------------------

typedef const char* (*SourceLoader)();

typedef struct {
  const char* name;
  SourceLoader source;
} CoreModule;

extern const char* systemModSource;
extern const char* timeModSource;

// Keep these sorted alphabetically.
CoreModule __core_modules__[] = {
    {"option", obaOptionModSource},
    {"system", obaSystemModSource},
    {"time", obaTimeModSource},
    {"list", obaListModSource},
    {NULL, NULL},
};

#endif
