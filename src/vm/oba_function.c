#include "oba_function.h"
#include "oba_common.h"
#include "oba_value.h"
#include "oba_vm.h"

ObjFunction* newFunction(ObaVM* vm, ObjModule* module) {
  obaPushRoot(vm, (Obj*)module);

  ObjFunction* function = ALLOCATE_OBJ(vm, ObjFunction, OBJ_FUNCTION);
  function->module = module;
  initChunk(&function->chunk);
  function->arity = 0;
  function->upvalueCount = 0;

  obaPopRoot(vm);
  return function;
}

ObjClosure* newClosure(ObaVM* vm, ObjFunction* function) {
  obaPushRoot(vm, (Obj*)function);

  ObjClosure* closure = ALLOCATE_OBJ(vm, ObjClosure, OBJ_CLOSURE);
  closure->function = function;
  closure->upvalueCount = function->upvalueCount;
  closure->upvalues = NULL;

  obaPushRoot(vm, (Obj*)closure);

  ObjUpvalue** upvalues = ALLOCATE(vm, ObjUpvalue*, function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = NULL;
  }
  closure->upvalues = upvalues;

  obaPopRoot(vm); // closure.
  obaPopRoot(vm); // function.
  return closure;
}

ObjUpvalue* newUpvalue(ObaVM* vm, Value* slot) {
  ObjUpvalue* upvalue = ALLOCATE_OBJ(vm, ObjUpvalue, OBJ_UPVALUE);
  upvalue->location = slot;
  return upvalue;
}
