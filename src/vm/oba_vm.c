#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oba.h"
#include "oba_builtins.h"
#include "oba_common.h"
#include "oba_compiler.h"
#include "oba_function.h"
#include "oba_vm.h"

#ifdef DEBUG_TRACE_EXECUTION
#include "oba_debug.h"
#endif

// The size of the buffer used to format error messages.
#define MAX_ERROR_SIZE 1024

// VM private helpers ---------------------------------------------------------

static void resetStack(ObaVM* vm) { vm->stackTop = vm->stack; }

static void ensureStack(ObaVM* vm, int needed) {
  if (vm->stackCapacity >= needed) return;

  int oldCapacity = vm->stackCapacity;
  while (vm->stackCapacity < needed) {
    vm->stackCapacity = GROW_CAPACITY(vm->stackCapacity);
  }

  Value* oldStack = vm->stack;
  vm->stack = GROW_ARRAY(vm, Value, vm->stack, oldCapacity, vm->stackCapacity);

  // If the reallocation moved the stack, we need to fix up any pointers into
  // the old stack to point at the new one.
  if (vm->stack != oldStack) {
    // Stack top.
    vm->stackTop = vm->stack + (int)(vm->stackTop - oldStack);

    // Call frame slots.
    int frameCount = vm->frame - vm->frames;
    for (int i = 0; i < frameCount; i++) {
      CallFrame* frame = &vm->frames[i];
      frame->slots = vm->stack + (int)(frame->slots - oldStack);
    }

    // Open upvalues.
    ObjUpvalue* upvalue = vm->openUpvalues;
    while (upvalue != NULL) {
      upvalue->location = vm->stack + (int)(upvalue->location - oldStack);
      upvalue = upvalue->next;
    }
  }

  if (oldCapacity == 0) {
    resetStack(vm);
  }
}

static Value peek(ObaVM* vm, int lookahead) {
  return *(vm->stackTop - lookahead);
}

static void push(ObaVM* vm, Value value) {
  if (vm->stackCapacity == 0) {
    if (IS_OBJ(value)) obaPushRoot(vm, AS_OBJ(value));
    ensureStack(vm, MIN_STACK_CAPACITY);
    if (IS_OBJ(value)) obaPopRoot(vm);
  } else {
    int count = (int)(vm->stackTop - vm->stack);
    if (count + 1 > vm->stackCapacity) {
      ensureStack(vm, count + 1);
    }
  }

  *vm->stackTop = value;
  vm->stackTop++;
}

static Value pop(ObaVM* vm) {
  vm->stackTop--;
  return *vm->stackTop;
}

static void defineNative(ObaVM* vm, const char* name, NativeFn function) {
  push(vm, OBJ_VAL(copyString(vm, name, (int)strlen(name))));
  push(vm, OBJ_VAL(newNative(vm, function)));
  tableSet(vm, vm->globals, AS_STRING(peek(vm, 2)), peek(vm, 1));
  pop(vm);
  pop(vm);
}

void runtimeError(ObaVM* vm) {
  char buf[MAX_ERROR_SIZE];
  int length = formatValue(vm, buf, vm->error);
  fprintf(stderr, "Runtime error: %s\n", buf);

#ifndef DISABLE_STACK_TRACES
  CallFrame* frame;
  for (frame = vm->frame; frame != vm->frames; frame--) {
    ObjFunction* function = frame->closure->function;
    size_t instruction = frame->ip - function->chunk.code - 1;
    int line = function->chunk.lines[instruction];
    fprintf(stderr, "[line %d] in ", line);
    if (function == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s::%s()\n", function->module->name->chars,
              function->name->chars);
    }
  }
#endif

  resetStack(vm);
}

static void registerBuiltins(ObaVM* vm, Builtin* builtins, int builtinsLength) {
  Builtin* builtin = __builtins__;

  // Original builtins.
  while (builtin->name != NULL) {
    defineNative(vm, builtin->name, builtin->function);
    builtin++;
  }

  // User builtins, registered last so they can override the originals.
  for (int i = 0; i < builtinsLength; i++) {
    defineNative(vm, builtins[i].name, builtins[i].function);
  }
}

static bool call(ObaVM* vm, ObjClosure* closure, int arity) {
  if (arity != closure->function->arity) {
    obaArityError(vm, closure->function->arity, arity);
    return false;
  }

  vm->frame++;
  if (vm->frame - vm->frames > FRAMES_MAX) {
    obaErrorf(vm, "Too many nested function calls");
    return false;
  }
  vm->frame->closure = closure;
  vm->frame->ip = closure->function->chunk.code;
  vm->frame->slots = vm->stackTop - arity;
  return true;
}

static bool callNative(ObaVM* vm, NativeFn native, int arity) {
  Value result = native(vm, arity, vm->stackTop - arity);
  for (int i = 0; i < arity; i++) pop(vm);
  pop(vm); // native.
  push(vm, result);
  return valuesEqual(vm->error, NIL_VAL);
}

static bool callCtor(ObaVM* vm, ObjCtor* ctor, int arity) {
  if (arity != ctor->arity) {
    obaArityError(vm, ctor->arity, arity);
    return false;
  }

  ObjInstance* instance = newInstance(vm, ctor);
  for (int i = 0; i < ctor->arity; i++) {
    instance->fields[ctor->arity - i - 1] = pop(vm);
  }

  pop(vm); // ctor.
  push(vm, OBJ_VAL(instance));
  return true;
}

static bool callValue(ObaVM* vm, Value value, int arity) {
  if (IS_OBJ(value)) {
    switch (OBJ_TYPE(value)) {
    case OBJ_CLOSURE:
      return call(vm, AS_CLOSURE(value), arity);
    case OBJ_NATIVE:
      return callNative(vm, AS_NATIVE(value), arity);
    case OBJ_CTOR:
      return callCtor(vm, AS_CTOR(value), arity);
    default:
      // Non-callable
      break;
    }
  }

  obaErrorf(vm, "Can only call functions");
  return false;
}

static bool match(ObaVM* vm, Value pattern, Value value) {
  if (IS_CTOR(pattern) && IS_INSTANCE(value)) {
    ObjCtor* ctor = AS_CTOR(pattern);
    ObjInstance* instance = AS_INSTANCE(value);
    return ctor == instance->ctor;
  } else {
    return valuesEqual(pattern, value);
  }
}

static void destructure(ObaVM* vm, Value pattern, Value value) {
  if (!IS_CTOR(pattern)) {
    // pattern and value are matching literals. there is nothing to destructure.
    return;
  }

  ObjCtor* ctor = AS_CTOR(pattern);
  ObjInstance* instance = AS_INSTANCE(value);
  for (int i = 0; i < instance->ctor->arity; i++) {
    push(vm, instance->fields[i]);
  }
}

// Captures the local value in an upvalue.
// If an existing upvalue already closes over the local, it is returned.
// Otherwise a new one is created.
static ObjUpvalue* captureUpvalue(ObaVM* vm, Value* local) {
  ObjUpvalue* prev = NULL;
  ObjUpvalue* upvalue = vm->openUpvalues;

  while (upvalue != NULL && upvalue->location > local) {
    prev = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  ObjUpvalue* createdUpvalue = newUpvalue(vm, local);
  createdUpvalue->next = upvalue;

  if (prev == NULL) {
    vm->openUpvalues = createdUpvalue;
  } else {
    prev->next = createdUpvalue;
  }

  return createdUpvalue;
}

static void closeUpvalue(ObaVM* vm, Value* last) {
  while (vm->openUpvalues != NULL && vm->openUpvalues->location >= last) {
    vm->openUpvalues->closed = *vm->openUpvalues->location;
    vm->openUpvalues->location = &vm->openUpvalues->closed;
    vm->openUpvalues = vm->openUpvalues->next;
  }
}

static Value resolveModule(ObaVM* vm, Value name) {
  // TODO(kendal): Allow the host application to resolve modules in its own way.
  return name;
}

ObjClosure* compileInModule(ObaVM* vm, Value value, const char* source) {
  ObjString* name = AS_STRING(value);
  obaPushRoot(vm, (Obj*)name);
  ObjModule* module = newModule(vm, name);
  obaPushRoot(vm, (Obj*)module);

  ObjFunction* function = obaCompile(vm, module, source);
  if (function == NULL) {
    return NULL;
  }
  obaPushRoot(vm, (Obj*)function);

  // Store the module as a global variable of the current module.
  tableSet(vm, vm->frame->closure->function->module->variables, module->name,
           OBJ_VAL(module));

  ObjClosure* closure = newClosure(vm, function);

  obaPopRoot(vm); // function.
  obaPopRoot(vm); // module.
  obaPopRoot(vm); // module name.
  return closure;
}

// TODO(kendal): If the module is already loaded, bail early.
// TODO(kendal): Handle circular imports.
static bool importModule(ObaVM* vm, Value name) {
  name = resolveModule(vm, name);

  const char* source = NULL;
  char* cname = AS_CSTRING(name);

  CoreModule* module = &__core_modules__[0];
  while (module->name != NULL) {
    if (strcmp(module->name, cname) == 0) {
      source = module->source();
      break;
    }
    module++;
  }

  if (source == NULL) return NULL;

  ObjClosure* moduleClosure = compileInModule(vm, name, source);
  if (moduleClosure == NULL) {
    return false;
  }

  push(vm, OBJ_VAL(moduleClosure));
  return callValue(vm, OBJ_VAL(moduleClosure), 0);
}

static void return_(ObaVM* vm) {
  Value value = pop(vm);
  closeUpvalue(vm, vm->frame->slots);

  vm->stackTop = vm->frame->slots - 1;
  push(vm, value);
  vm->frame->closure = NULL;
  vm->frame->ip = NULL;
  vm->frame->slots = NULL;
  vm->frame--;
}

static void concatenate(ObaVM* vm) {
  ObjString* b = AS_STRING(peek(vm, 1));
  ObjString* a = AS_STRING(peek(vm, 2));

  char* chars = ALLOCATE(vm, char, b->length + a->length + 1);
  int length = b->length + a->length;

  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = takeString(vm, chars, length);
  pop(vm);
  pop(vm);
  push(vm, OBJ_VAL(result));
}

static void freeObjects(ObaVM* vm) {
  Obj* obj = vm->objects;
  while (obj != NULL) {
    Obj* next = obj->next;
    freeObject(vm, obj);
    obj = next;
  }
}

static ObaInterpretResult run(ObaVM* vm) {

#define RUNTIME_ERROR()                                                        \
  do {                                                                         \
    runtimeError(vm);                                                          \
    return OBA_RESULT_RUNTIME_ERROR;                                           \
  } while (0)

#define READ_BYTE() (*vm->frame->ip++)

#define READ_SHORT()                                                           \
  (vm->frame->ip += 2, (uint16_t)((vm->frame->ip[-2] << 8) | vm->frame->ip[-1]))

#define READ_CONSTANT()                                                        \
  (vm->frame->closure->function->chunk.constants.values[READ_BYTE()])

#define READ_STRING() AS_STRING(READ_CONSTANT())

#define BINARY_OP(type, op)                                                    \
  do {                                                                         \
    if (IS_NUMBER(peek(vm, 1)) && IS_NUMBER(peek(vm, 2))) {                    \
      double b = AS_NUMBER(pop(vm));                                           \
      double a = AS_NUMBER(pop(vm));                                           \
      push(vm, type(a op b));                                                  \
    } else {                                                                   \
      obaErrorf(vm, "Expected numeric or string operands");                    \
      RUNTIME_ERROR();                                                         \
    }                                                                          \
  } while (0)

  // Debug output

#ifdef DEBUG_TRACE_EXECUTION

#define DEBUG_TRACE_INSTRUCTIONS()                                             \
  disassembleInstruction(                                                      \
      &vm->frame->closure->function->chunk,                                    \
      (int)(vm->frame->ip - vm->frame->closure->function->chunk.code));        \
  printf("          ");                                                        \
  for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {                 \
    printf("[ ");                                                              \
    printValue(*slot);                                                         \
    printf(" ]");                                                              \
  }                                                                            \
  printf("\n");

#else

#define DEBUG_TRACE_INSTRUCTIONS() ;

#endif

  // Optimizations

#ifdef OBA_COMPUTED_GOTO

#define DISPATCH()                                                             \
  do {                                                                         \
    DEBUG_TRACE_INSTRUCTIONS();                                                \
    if (obaHasError(vm)) RUNTIME_ERROR();                                      \
    goto* dispatchTable[READ_BYTE()];                                          \
  } while (0)

#define INTERPRET_LOOP DISPATCH();

  // Computed goto dispatch table.
  // eli.thegreenplace.net/2012/07/12/computed-goto-for-efficient-dispatch-tables
  static void* dispatchTable[] = {
#define OPCODE(name) &&op_##name,
#include "oba_opcodes.h"
#undef OPCODE
  };

#define CASE_OP(name) op_##name

#else

#define OPCODE(name) OP_##name
#define CASE_OP(name) case OPCODE(name)

#define INTERPRET_LOOP                                                         \
  loop:                                                                        \
  DEBUG_TRACE_INSTRUCTIONS();                                                  \
  if (obaHasError(vm)) RUNTIME_ERROR();                                        \
  switch ((OpCode)READ_BYTE())

#define DISPATCH() goto loop

#endif

  INTERPRET_LOOP {

    CASE_OP(CONSTANT) : {
      push(vm, READ_CONSTANT());
      DISPATCH();
    }

    CASE_OP(ERROR) : {
      obaErrorf(vm, AS_CSTRING(READ_CONSTANT()));
      RUNTIME_ERROR();
    }

    CASE_OP(ADD) : {
      if (IS_STRING(peek(vm, 1)) && IS_STRING(peek(vm, 2))) {
        concatenate(vm);
      } else {
        BINARY_OP(OBA_NUMBER, +);
      }
      DISPATCH();
    }

    CASE_OP(MINUS) : {
      BINARY_OP(OBA_NUMBER, -);
      DISPATCH();
    }

    CASE_OP(MULTIPLY) : {
      BINARY_OP(OBA_NUMBER, *);
      DISPATCH();
    }

    CASE_OP(DIVIDE) : {
      BINARY_OP(OBA_NUMBER, /);
      DISPATCH();
    }

    CASE_OP(MODULO) : {
      if (IS_NUMBER(peek(vm, 1)) && IS_NUMBER(peek(vm, 2))) {
        int b = AS_NUMBER(pop(vm));
        int a = AS_NUMBER(pop(vm));
        push(vm, OBA_NUMBER((double)(a % b)));
      } else {
        obaErrorf(vm, "Expected numeric or string operands");
        RUNTIME_ERROR();
      }

      DISPATCH();
    }

    CASE_OP(NOT) : {
      if (!IS_BOOL(peek(vm, 1))) {
        obaTypeError(vm, "boolean");
        RUNTIME_ERROR();
      }
      push(vm, OBA_BOOL(!AS_BOOL(pop(vm))));
      DISPATCH();
    }

    CASE_OP(GT) : {
      BINARY_OP(OBA_BOOL, >);
      DISPATCH();
    }

    CASE_OP(LT) : {
      BINARY_OP(OBA_BOOL, <);
      DISPATCH();
    }

    CASE_OP(GTE) : {
      BINARY_OP(OBA_BOOL, >=);
      DISPATCH();
    }

    CASE_OP(LTE) : {
      BINARY_OP(OBA_BOOL, <=);
      DISPATCH();
    }

    CASE_OP(EQ) : {
      Value b = pop(vm);
      Value a = pop(vm);
      push(vm, OBA_BOOL(valuesEqual(a, b)));
      DISPATCH();
    }

    CASE_OP(NEQ) : {
      Value b = pop(vm);
      Value a = pop(vm);
      push(vm, OBA_BOOL(!valuesEqual(a, b)));
      DISPATCH();
    }

    CASE_OP(TRUE) : {
      push(vm, OBA_BOOL(true));
      DISPATCH();
    }

    CASE_OP(FALSE) : {
      push(vm, OBA_BOOL(false));
      DISPATCH();
    }

    CASE_OP(JUMP) : {
      vm->frame->ip += READ_SHORT();
      DISPATCH();
    }

    CASE_OP(JUMP_IF_FALSE) : {
      Value condValue = pop(vm);
      if (!IS_BOOL(condValue)) {
        obaTypeError(vm, "boolean");
        RUNTIME_ERROR();
      }
      int jump = READ_SHORT();
      bool cond = AS_BOOL(condValue);
      if (!cond) vm->frame->ip += jump;
      DISPATCH();
    }

    CASE_OP(JUMP_IF_TRUE) : {
      Value condValue = pop(vm);
      if (!IS_BOOL(condValue)) {
        obaTypeError(vm, "boolean");
        RUNTIME_ERROR();
      }
      int jump = READ_SHORT();
      bool cond = AS_BOOL(condValue);
      if (cond) vm->frame->ip += jump;
      DISPATCH();
    }

    CASE_OP(JUMP_IF_NOT_MATCH) : {
      int jump = READ_SHORT();
      Value lambda = peek(vm, 1);
      Value pattern = peek(vm, 2);
      Value value = peek(vm, 3);

      if (!match(vm, pattern, value)) {
        vm->frame->ip += jump;
        pop(vm); // lambda.
        pop(vm); // pattern.
        DISPATCH();
      }
      pop(vm); // lambda.
      pop(vm); // pattern.
      pop(vm); // value.
      push(vm, lambda);
      destructure(vm, pattern, value);
      DISPATCH();
    }

    CASE_OP(LOOP) : {
      vm->frame->ip = vm->frame->closure->function->chunk.code + READ_SHORT();
      DISPATCH();
    }

    CASE_OP(DEFINE_GLOBAL) : {
      ObjString* name = READ_STRING();
      obaPushRoot(vm, (Obj*)name);
      tableSet(vm, vm->frame->closure->function->module->variables, name,
               peek(vm, 1));
      obaPopRoot(vm);
      pop(vm);
      DISPATCH();
    }

    CASE_OP(GET_GLOBAL) : {
      ObjString* name = READ_STRING();
      Value value;

      if (!tableGet(vm->frame->closure->function->module->variables, name,
                    &value)) {
        if (!tableGet(vm->globals, name, &value)) {
          obaErrorf(vm, "Undefined variable: %s", name->chars);
          RUNTIME_ERROR();
        }
      }
      push(vm, value);
      DISPATCH();
    }

    CASE_OP(SET_LOCAL) : {
      uint8_t slot = READ_BYTE();

      Value oldValue = vm->frame->slots[slot];
      Value newValue = peek(vm, 1);
      if (canAssignType(oldValue, newValue)) {
        vm->frame->slots[slot] = peek(vm, 1);
        DISPATCH();
      }

      const char* oldTypeName = valueTypeName(oldValue);
      const char* newTypeName = valueTypeName(newValue);
      obaErrorf(vm, "Cannot assign '%s' to variable of type '%s'", newTypeName,
                oldTypeName);
      RUNTIME_ERROR();
    }

    CASE_OP(GET_LOCAL) : {
      // Locals live on the top of the stack.
      uint8_t slot = READ_BYTE();
      push(vm, vm->frame->slots[slot]);
      DISPATCH();
    }

    CASE_OP(SET_UPVALUE) : {
      uint8_t slot = READ_BYTE();
      *vm->frame->closure->upvalues[slot]->location = peek(vm, 1);
      DISPATCH();
    }

    CASE_OP(GET_UPVALUE) : {
      uint8_t slot = READ_BYTE();
      ObjUpvalue* upvalue = vm->frame->closure->upvalues[slot];
      // The user can never get an upvalue directly. Push its captured value
      // onto the stack instead.
      push(vm, *upvalue->location);
      DISPATCH();
    }

    CASE_OP(CLOSE_UPVALUE) : {
      closeUpvalue(vm, vm->stackTop - 1);
      pop(vm);
      DISPATCH();
    }

    CASE_OP(GET_IMPORTED_VARIABLE) : {
      Value receiver = pop(vm);
      if (!IS_MODULE(receiver)) {
        obaTypeError(vm, "module");
        RUNTIME_ERROR();
      }

      ObjModule* module = AS_MODULE(receiver);
      ObjString* name = READ_STRING();
      Value value;
      if (!tableGet(module->variables, name, &value)) {
        obaErrorf(vm, "Variable '%s' not found in module '%s'", name->chars,
                  module->name);
        RUNTIME_ERROR();
      }
      push(vm, value);
      DISPATCH();
    }

    CASE_OP(STRING) : {
      char buf[FORMAT_VALUE_MAX];
      int length = formatValue(vm, buf, pop(vm));
      Value string = OBJ_VAL(copyString(vm, buf, length));
      push(vm, string);
      DISPATCH();
    }

    CASE_OP(CALL) : {
      uint8_t argCount = READ_BYTE();
      if (!callValue(vm, peek(vm, argCount + 1), argCount)) {
        RUNTIME_ERROR();
      }
      DISPATCH();
    }

    CASE_OP(CLOSURE) : {
      ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
      ObjClosure* closure = newClosure(vm, function);
      push(vm, OBJ_VAL(closure));

      for (int j = 0; j < closure->upvalueCount; j++) {
        uint8_t isLocal = READ_BYTE();
        uint8_t slot = READ_BYTE();
        if (isLocal) {
          closure->upvalues[j] = captureUpvalue(vm, vm->frame->slots + slot);
        } else {
          closure->upvalues[j] = vm->frame->closure->upvalues[slot];
        }
      }
      DISPATCH();
    }

    CASE_OP(RETURN) : {
      return_(vm);
      DISPATCH();
    }

    CASE_OP(POP) : {
      pop(vm);
      DISPATCH();
    }

    CASE_OP(DEBUG) : {
      Value value = pop(vm);
      printValue(value);
      printf("\n");
      DISPATCH();
    }

    CASE_OP(IMPORT_MODULE) : {
      Value name = READ_CONSTANT();
      if (!importModule(vm, name)) {
        obaErrorf(vm, "Could not import module '%s'", AS_CSTRING(name));
        RUNTIME_ERROR();
      }
      DISPATCH();
    }

    CASE_OP(END_MODULE) : {
      // Don't pop the root module or we'll never reach OP_EXIT.
      push(vm, NIL_VAL);
      if (vm->frame - vm->frames > 1) {
        return_(vm);
        pop(vm);
      }
      DISPATCH();
    }

    CASE_OP(EXIT) : {
      // Pop the root closure off the stack.
      return_(vm);
      pop(vm);
      return OBA_RESULT_SUCCESS;
    }
  }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
#undef CASE_OP
#undef DISPATCH
#undef INTERPRET_LOOP
#undef DEBUG_TRACE_INSTRUCTIONS
}

void obaPushRoot(ObaVM* vm, Obj* obj) {
  ASSERT(obj != NULL, "Can't push NULL GC root");
  ASSERT(vm->tempRootsCount + 1 < TEMP_ROOTS_MAX,
         "Too many temporary GC roots");
  vm->tempRoots[vm->tempRootsCount++] = obj;
}

void obaPopRoot(ObaVM* vm) {
  ASSERT(vm->tempRootsCount != 0, "No temp roots to release");
  vm->tempRootsCount--;
}

static void markRoots(ObaVM* vm) {
  for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
    obaGrayValue(vm, *slot);
  }

  for (int i = 0; i < vm->tempRootsCount; i++) {
    obaGrayObject(vm, vm->tempRoots[i]);
  }

  for (CallFrame* frame = vm->frames; frame <= vm->frame; frame++) {
    obaGrayObject(vm, (Obj*)frame->closure);
  }

  for (ObjUpvalue* uv = vm->openUpvalues; uv != NULL; uv = uv->next) {
    obaGrayObject(vm, (Obj*)uv);
  }

  obaGrayTable(vm, vm->globals);
  markCompilerRoots(vm, vm->compiler);
}

static void blackenRoots(ObaVM* vm) {
  while (vm->grayCount > 0) {
    Obj* obj = vm->grayStack[--vm->grayCount];
    blackenObject(vm, obj);
  }
}

static void sweepGarbage(ObaVM* vm) {
  Obj* previous = NULL;
  Obj* object = vm->objects;
  while (object != NULL) {
    if (object->isMarked) {
      object->isMarked = false;
      previous = object;
      object = object->next;
      continue;
    }

    Obj* unreached = object;
    object = object->next;
    if (previous != NULL) {
      previous->next = object;
    } else {
      vm->objects = object;
    }
    freeObject(vm, unreached);
  }
}

// VM public API implementation ------------------------------------------------

void obaCollectGarbage(ObaVM* vm) {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
  size_t before = vm->bytesAllocated;
#endif
  markRoots(vm);
  blackenRoots(vm);
  sweepGarbage(vm);
  vm->nextGC = vm->bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
  printf("   collected %ld bytes (from %ld to %ld) next at %ld\n",
         before - vm->bytesAllocated, before, vm->bytesAllocated, vm->nextGC);
#endif
}

void obaErrorf(ObaVM* vm, const char* format, ...) {
  char buf[MAX_ERROR_SIZE];

  va_list args;
  va_start(args, format);
  int length = vsprintf(buf, format, args);
  va_end(args);

  vm->error = OBJ_VAL(copyString(vm, buf, length));
}

void obaTypeError(ObaVM* vm, const char* expected) {
  obaErrorf(vm, "expected a %s value", expected);
}

void obaArityError(ObaVM* vm, int want, int got) {
  char* arguments = "argument";
  if (want != 1) arguments = "arguments";
  obaErrorf(vm, "expected %d %s but got %d", want, arguments, got);
}

bool obaHasError(ObaVM* vm) { return !valuesEqual(vm->error, NIL_VAL); }

ObaVM* obaNewVM(Builtin* builtins, int builtinsLength) {
  ObaVM* vm = (ObaVM*)realloc(NULL, sizeof(*vm));
  memset(vm, 0, sizeof(ObaVM));

  vm->compiler = NULL;
  vm->openUpvalues = NULL;

  vm->objects = NULL;
  vm->grayCount = 0;
  vm->grayCapacity = 0;
  vm->grayStack = NULL;
  vm->tempRootsCount = 0;
  vm->bytesAllocated = 0;
  vm->nextGC = 1024 * 1024;

  resetStack(vm);
  vm->stack = NULL;
  vm->stackCapacity = 0;

  vm->frame = vm->frames;
  vm->error = NIL_VAL;

  vm->globals = (Table*)realloc(NULL, sizeof(Table));
  initTable(vm->globals);

  registerBuiltins(vm, builtins, builtinsLength);
  return vm;
}

void obaFreeVM(ObaVM* vm) {
  // Any non-object values held in object fields will be freed by this.
  freeObjects(vm);
  freeTable(vm, vm->globals);
  FREE_ARRAY(vm, Value, vm->stack, vm->stackCapacity);
  free(vm->globals);
  free(vm->grayStack);
  free(vm);
}

ObaInterpretResult interpret(ObaVM* vm, const char* source) {
  ObjModule* module = newModule(vm, copyString(vm, "main", 4));
  obaPushRoot(vm, (Obj*)module);

  ObjFunction* function = obaCompile(vm, module, source);
  if (function == NULL) {
    return OBA_RESULT_COMPILE_ERROR;
  }
  if (function->chunk.code == NULL) {
    return OBA_RESULT_SUCCESS;
  }

  obaPopRoot(vm); // module.

  push(vm, OBJ_VAL(function));
  ObjClosure* closure = newClosure(vm, function);
  pop(vm); // function.
  push(vm, OBJ_VAL(closure));
  callValue(vm, OBJ_VAL(closure), 0);
  return run(vm);
}

ObaInterpretResult obaInterpret(ObaVM* vm, const char* source) {
  vm->allowGlobals = true;
  interpret(vm, obaGlobalsModSource());
  vm->allowGlobals = false;
  return interpret(vm, source);
}
