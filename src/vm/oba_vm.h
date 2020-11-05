#ifndef oba_vm_h
#define oba_vm_h

#include "oba_compiler.h"
#include "oba_function.h"
#include "oba_token.h"
#include "oba_value.h"

// The maximum number of values that can be held on the stack at once.
#define MIN_STACK_CAPACITY 1024

// The maximum number of call-frames.
// TODO(kendal): Oba code will naturally have many recursive calls, the call
// frameStack should be dynamic.
#define FRAMES_MAX 1024 * 1024

// The maximum number of temporary GC roots at any given time. In practice there
// are < 10 of these at a time.
#define TEMP_ROOTS_MAX 64

#define GC_HEAP_GROW_FACTOR 2

struct ObaVM {
  CallFrame frames[FRAMES_MAX];
  CallFrame* frame;

  struct Compiler* compiler;

  int stackCapacity;
  Value* stack;
  Value* stackTop;

  // Global values available to all modules.
  //
  // Builtins are defined here. When searching for a global, the VM first checks
  // the current module, then this table.
  Table* globals;
  Table* strings;

  ObjUpvalue* openUpvalues;
  Obj* objects;

  // Set by Oba code when a panic occurs. When set, the VM prints the error, a
  // stacktrace, and exits on the next turn.
  Value error;

  // Whether the current module can define new global variables. This is used
  // internally and is automatically disabled for user code.
  bool allowGlobals;

  // Fields that keep of "gray" objects during GC.
  int grayCount;
  int grayCapacity;
  Obj** grayStack;
  size_t bytesAllocated;
  size_t nextGC;

  // Temporary GC roots. These are used to prevent heap objects from being GC'd
  // while they're being initialized - for example if they contain nested heap
  // objects that also require allocation and may trigger GC.
  Obj* tempRoots[TEMP_ROOTS_MAX];
  int tempRootsCount;
};

typedef enum {
#define OPCODE(name) OP_##name,
#include "oba_opcodes.h"
#undef OPCODE
} OpCode;

void obaPopRoot(ObaVM*);
void obaPushRoot(ObaVM*, Obj*);

#endif
