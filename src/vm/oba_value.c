#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oba_common.h"
#include "oba_value.h"
#include "oba_vm.h"

ObjString* formatFunction(ObaVM* vm, ObjFunction* function) {
  char buf[1024];
  int length = 0;

  if (function->name != NULL) {
    length = sprintf(buf, "<fn %s::%s>", function->module->name->chars,
                     function->name->chars);
    return copyString(vm, buf, length);
  } else {
    return copyString(vm, "<fn>", 4);
  }
}

void printFunction(ObjFunction* function) {
  if (function->name != NULL) {
    printf("<fn %s::%s>", function->module->name->chars, function->name->chars);
  } else {
    // The function is still being compiled and this value is being printed
    // while the function is on the stack, probably as a debugging message.
    printf("<fn>");
  }
}

const char* objectTypeName(Value value) {
  switch (OBJ_TYPE(value)) {
  case OBJ_STRING:
    return "string";
  case OBJ_CLOSURE:
  case OBJ_NATIVE:
  case OBJ_FUNCTION:
    return "function";
  case OBJ_UPVALUE:
    return "upvalue";
  case OBJ_MODULE:
    return "module";
  default:
    break; // unreachable.
  }
}

bool canAssignObjectType(Value oldValue, Value newValue) {
  if (!IS_OBJ(newValue))
    return false;

  switch (OBJ_TYPE(oldValue)) {
  case OBJ_CLOSURE:
  case OBJ_FUNCTION:
  case OBJ_NATIVE:
    switch (OBJ_TYPE(newValue)) {
    case OBJ_CLOSURE:
    case OBJ_FUNCTION:
    case OBJ_NATIVE:
      return true;
    }
    return false;
  default:
    return OBJ_TYPE(oldValue) == OBJ_TYPE(newValue);
  }
}

bool canAssignType(Value oldValue, Value newValue) {
  switch (oldValue.type) {
  case VAL_OBJ:
    return canAssignObjectType(oldValue, newValue);
  default:
    return oldValue.type == newValue.type;
  }
}

const char* valueTypeName(Value value) {
  switch (value.type) {
  case VAL_NIL:
    return "nil";
  case VAL_BOOL:
    return "bool";
  case VAL_NUMBER:
    return "number";
  case VAL_OBJ:
    return objectTypeName(value);
  default:
    break; // unreachable.
  }
}

ObjString* formatValue(ObaVM*, Value);
ObjString* formatObject(ObaVM* vm, Value value) {
  Obj* obj = AS_OBJ(value);

  char buf[1024];
  int length = 0;

  switch (obj->type) {
  case OBJ_CLOSURE:
    return formatFunction(vm, AS_CLOSURE(value)->function);
  case OBJ_FUNCTION:
    return formatFunction(vm, AS_FUNCTION(value));
  case OBJ_STRING:
    return AS_STRING(value);
  case OBJ_NATIVE:
    length = sprintf(buf, "<native fn>");
    return copyString(vm, buf, length);
  case OBJ_UPVALUE:
    return formatValue(vm, *(AS_UPVALUE(value)->location));
  case OBJ_MODULE: {
    ObjModule* module = (ObjModule*)obj;
    length = sprintf(buf, "<module %s>", module->name->chars);
    return copyString(vm, buf, length);
  }
  default:
    return NULL; // Unreachable
  }
}

void printObject(Value value) {
  Obj* obj = AS_OBJ(value);
  switch (obj->type) {
  case OBJ_CLOSURE:
    printFunction(AS_CLOSURE(value)->function);
    break;
  case OBJ_FUNCTION:
    printFunction(AS_FUNCTION(value));
    break;
  case OBJ_STRING:
    printf("%s", AS_CSTRING(value));
    break;
  case OBJ_NATIVE:
    printf("<native fn>");
    break;
  case OBJ_UPVALUE:
    printValue(*(AS_UPVALUE(value)->location));
    break;
  case OBJ_MODULE: {
    ObjModule* module = (ObjModule*)obj;
    printf("<module %s>", module->name->chars);
    break;
  }
  default:
    break; // Unreachable
  }
}

Obj* allocateObject(ObaVM* vm, size_t size, ObjType type) {
#ifdef DEBUG_TRACE_EXECUTION
  printf("allocate object type: %d size %d\n", type, size);
#endif
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;
  object->next = vm->objects;
  vm->objects = object;
  return object;
}

void freeObject(Obj* obj) {
#ifdef DEBUG_TRACE_EXECUTION
  printf("free object type: %d\n", obj->type);
  printObject(OBJ_VAL(obj));
  printf(" @ %p\n", obj);
#endif

  switch (obj->type) {
  case OBJ_STRING: {
    ObjString* string = (ObjString*)obj;
    FREE_ARRAY(char, string->chars, string->length + 1);
    FREE(ObjString, obj);
    break;
  }
  case OBJ_NATIVE:
    FREE(ObjNative, obj);
    break;
  case OBJ_FUNCTION: {
    ObjFunction* function = (ObjFunction*)obj;
    freeChunk(&function->chunk);
    FREE(ObjFunction, obj);
    break;
  }
  case OBJ_CLOSURE: {
    ObjClosure* closure = (ObjClosure*)obj;
    FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
    FREE(ObjClosure, obj);
    break;
  }
  case OBJ_UPVALUE:
    FREE(ObjUpvalue, obj);
    break;
  case OBJ_MODULE: {
    ObjModule* module = (ObjModule*)obj;
    freeTable(module->variables);
    FREE(ObjModule, obj);
    break;
  }
  }
}

void initValueArray(ValueArray* array) {
  array->capacity = 0;
  array->count = 0;
  array->values = NULL;
}

void freeValueArray(ValueArray* array) {
  FREE_ARRAY(Value, array->values, array->capacity);
  initValueArray(array);
}

void writeValueArray(ValueArray* array, Value value) {
  if (array->capacity <= array->count) {
    int oldCap = array->capacity;
    array->capacity = GROW_CAPACITY(oldCap);
    array->values = GROW_ARRAY(Value, array->values, oldCap, array->capacity);
  }

  array->values[array->count] = value;
  array->count++;
}

ObjString* allocateString(ObaVM* vm, char* chars, int length, uint32_t hash) {
  ObjString* string = ALLOCATE_OBJ(vm, ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;

  return string;
}

// FNV-1a hash function
// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function.
static uint32_t hashString(const char* key, int length) {
  uint32_t hash = 2166136261u;

  for (int i = 0; i < length; i++) {
    hash ^= key[i];
    hash *= 16777619;
  }

  return hash;
}

ObjString* copyString(ObaVM* vm, const char* chars, int length) {
  char* heapChars = ALLOCATE(char, length + 1);
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';
  uint32_t hash = hashString(heapChars, length);

  return allocateString(vm, heapChars, length, hash);
}

ObjString* takeString(ObaVM* vm, char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  return allocateString(vm, chars, length, hash);
}

ObjNative* newNative(ObaVM* vm, NativeFn function) {
  ObjNative* native = ALLOCATE_OBJ(vm, ObjNative, OBJ_NATIVE);
  native->function = function;
  return native;
}

ObjModule* newModule(ObaVM* vm, ObjString* name) {
  ObjModule* module = ALLOCATE_OBJ(vm, ObjModule, OBJ_MODULE);
  module->name = name;
  module->variables = (Table*)reallocate(NULL, 0, sizeof(Table));
  initTable(module->variables);
  return module;
}

bool objectsEqual(Value ao, Value bo) {
  if (OBJ_TYPE(ao) != OBJ_TYPE(bo))
    return false;

  switch (OBJ_TYPE(ao)) {
  case OBJ_STRING: {
    ObjString* a = AS_STRING(ao);
    ObjString* b = AS_STRING(bo);
    return a->length == b->length && strcmp(a->chars, b->chars) == 0;
  }
  case OBJ_FUNCTION: {
    ObjFunction* a = AS_FUNCTION(ao);
    ObjFunction* b = AS_FUNCTION(bo);
    return a == b;
  }
  case OBJ_NATIVE: {
    NativeFn a = AS_NATIVE(ao);
    NativeFn b = AS_NATIVE(bo);
    return a == b;
  }
  default:
    return false; // Unreachable.
  }
}

bool valuesEqual(Value a, Value b) {
  if (a.type != b.type)
    return false;

  switch (a.type) {
  case VAL_BOOL:
    return AS_BOOL(a) == AS_BOOL(b);
  case VAL_NUMBER:
    return AS_NUMBER(a) == AS_NUMBER(b);
  case VAL_OBJ:
    return objectsEqual(a, b);
  default:
    return false; // Unreachable.
  }
}

ObjString* formatValue(ObaVM* vm, Value value) {
  char buf[1024];
  int length = 0;

  switch (value.type) {
  case VAL_NUMBER:
    length = sprintf(buf, "%g", AS_NUMBER(value));
    buf[length] = '\0';
    return copyString(vm, buf, length);
  case VAL_BOOL:
    length = sprintf(buf, AS_BOOL(value) ? "true" : "false");
    return copyString(vm, buf, length);
  case VAL_OBJ:
    return formatObject(vm, value);
  case VAL_NIL:
    return copyString(vm, "nil", 3);
  default:
    return NULL; // Unreachable
  }
}

void printValue(Value value) {
  switch (value.type) {
  case VAL_NUMBER:
    printf("%g", AS_NUMBER(value));
    break;
  case VAL_BOOL:
    printf(AS_BOOL(value) ? "true" : "false");
    break;
  case VAL_OBJ:
    printObject(value);
    break;
  case VAL_NIL:
    printf("nil");
    break;
  default:
    return; // Unreachable
  }
}

void initTable(Table* table) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
}

void freeTable(Table* table) {
  FREE_ARRAY(Entry, table->entries, table->capacity);
  initTable(table);
}

Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
  uint32_t index = key->hash % capacity;
  for (;;) {
    Entry* entry = &entries[index];

    if (entry->key == NULL || entry->key->hash == key->hash) {
      return entry;
    }

    index = (index + 1) % capacity;
  }
}

void adjustCapacity(Table* table, int capacity) {
  Entry* entries = ALLOCATE(Entry, capacity);
  for (int i = 0; i < capacity; i++) {
    entries[i].key = NULL;
    entries[i].value = NIL_VAL;
  }

  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if (entry->key == NULL)
      continue;

    Entry* dest = findEntry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
  }

  FREE_ARRAY(Entry, table->entries, table->capacity);
  table->entries = entries;
  table->capacity = capacity;
}

bool tableGet(Table* table, ObjString* key, Value* value) {
  if (table->count == 0)
    return false;

  Entry* entry = findEntry(table->entries, table->capacity, key);
  if (entry->key == NULL) {
    return false;
  }

  *value = entry->value;
  return true;
}

bool tableSet(Table* table, ObjString* key, Value value) {
  if (table->count <= table->capacity * TABLE_MAX_LOAD) {
    int capacity = GROW_CAPACITY(table->capacity);
    adjustCapacity(table, capacity);
  }

  Entry* entry = findEntry(table->entries, table->capacity, key);

  bool isNewKey = entry->key == NULL;
  if (isNewKey)
    table->count++;

  entry->key = key;
  entry->value = value;
  return isNewKey;
}
