#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oba_common.h"
#include "oba_value.h"
#include "oba_vm.h"

int formatFunction(ObaVM* vm, char* buf, ObjFunction* function) {
  if (function->name != NULL) {
    return sprintf(buf, "<fn %s::%s>", function->module->name->chars,
                   function->name->chars);
  } else {
    return sprintf(buf, "<fn>");
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
  if (!IS_OBJ(newValue)) return false;

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

int formatValue(ObaVM*, char*, Value);
int formatObject(ObaVM* vm, char* buf, Value value) {
  Obj* obj = AS_OBJ(value);

  switch (obj->type) {
  case OBJ_CLOSURE:
    return formatFunction(vm, buf, AS_CLOSURE(value)->function);
  case OBJ_FUNCTION:
    return formatFunction(vm, buf, AS_FUNCTION(value));
  case OBJ_STRING:
    return sprintf(buf, AS_CSTRING(value));
  case OBJ_NATIVE:
    return sprintf(buf, "<native fn>");
  case OBJ_UPVALUE:
    return formatValue(vm, buf, *(AS_UPVALUE(value)->location));
  case OBJ_MODULE: {
    ObjModule* module = (ObjModule*)obj;
    return sprintf(buf, "<module %s>", module->name->chars);
  }
  case OBJ_INSTANCE: {
    ObjInstance* instance = (ObjInstance*)obj;

    int length = sprintf(buf, "(");
    length += sprintf(buf + length, instance->ctor->family->chars);
    length += sprintf(buf + length, "::");
    length += sprintf(buf + length, instance->ctor->name->chars);

    int fields = instance->ctor->arity;
    if (fields > 0) {
      length += sprintf(buf + length, ",");
      for (int i = 0; i < fields; i++) {
        length += formatValue(vm, buf + length, instance->fields[i]);
        if (i + 1 < fields) {
          length += sprintf(buf + length, ",");
        }
      }
    }
    return length + sprintf(buf + length, ")");
  }
  default:
    return 0; // Unreachable
  }
}

void printCtor(ObjCtor* ctor) {
  printf("%s::%s", ctor->family->chars, ctor->name->chars);
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
  case OBJ_CTOR: {
    printCtor(AS_CTOR(value));
    break;
  }
  case OBJ_INSTANCE: {
    ObjInstance* instance = (ObjInstance*)obj;
    printf("(");
    printCtor(instance->ctor);

    int fields = instance->ctor->arity;
    if (fields > 0) {
      printf(",");
      for (int i = 0; i < fields; i++) {
        printValue(instance->fields[i]);
        if (i + 1 < fields) {
          printf(",");
        }
      }
    }
    printf(")");
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
    free(module->variables);
    FREE(ObjModule, obj);
    break;
  }
  case OBJ_CTOR:
    FREE(ObjCtor, obj);
    break;
  case OBJ_INSTANCE: {
    ObjInstance* instance = (ObjInstance*)obj;
    FREE_ARRAY(Value, instance->fields, instance->ctor->arity);
    FREE(ObjInstance, obj);
    break;
  }
  }
}

DEFINE_BUFFER(Byte, uint8_t)
DEFINE_BUFFER(Value, Value)

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

ObjCtor* newCtor(ObaVM* vm, ObjString* family, ObjString* name, int arity) {
  ObjCtor* ctor = ALLOCATE_OBJ(vm, ObjCtor, OBJ_CTOR);
  ctor->family = family;
  ctor->name = name;
  ctor->arity = arity;
  return ctor;
}

ObjInstance* newInstance(ObaVM* vm, ObjCtor* ctor) {
  ObjInstance* instance = ALLOCATE_OBJ(vm, ObjInstance, OBJ_INSTANCE);
  instance->ctor = ctor;
  instance->fields = ALLOCATE(Value, ctor->arity);
  return instance;
}

bool objectsEqual(Value ao, Value bo) {
  if (OBJ_TYPE(ao) != OBJ_TYPE(bo)) return false;

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
  case OBJ_CTOR:
    return AS_CTOR(ao) == AS_CTOR(bo);
  case OBJ_INSTANCE: {
    ObjInstance* a = AS_INSTANCE(ao);
    ObjInstance* b = AS_INSTANCE(bo);
    if (a->ctor != b->ctor) return false;
    for (int i = 0; i < a->ctor->arity; i++) {
      if (!valuesEqual(a->fields[i], b->fields[i])) {
        return false;
      }
    }
    return true;
  }
  default:
    return false; // Unreachable.
  }
}

bool valuesEqual(Value a, Value b) {
  if (a.type != b.type) return false;

  switch (a.type) {
  case VAL_BOOL:
    return AS_BOOL(a) == AS_BOOL(b);
  case VAL_NUMBER:
    return AS_NUMBER(a) == AS_NUMBER(b);
  case VAL_OBJ:
    return objectsEqual(a, b);
  case VAL_NIL:
    return b.type == VAL_NIL;
  default:
    return false; // Unreachable.
  }
}

int formatValue(ObaVM* vm, char* buf, Value value) {
  switch (value.type) {
  case VAL_NUMBER:
    return sprintf(buf, "%g", AS_NUMBER(value));
  case VAL_BOOL:
    return sprintf(buf, AS_BOOL(value) ? "true" : "false");
  case VAL_NIL:
    return sprintf(buf, "nil");
  case VAL_OBJ:
    return formatObject(vm, buf, value);
  default:
    return 0; // Unreachable
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

// Frees the memory held by all table entries. This does not free the table
// pointer itself.
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
    if (entry->key == NULL) continue;

    Entry* dest = findEntry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
  }

  FREE_ARRAY(Entry, table->entries, table->capacity);
  table->entries = entries;
  table->capacity = capacity;
}

bool tableGet(Table* table, ObjString* key, Value* value) {
  if (table->count == 0) return false;

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
  if (isNewKey) table->count++;

  entry->key = key;
  entry->value = value;
  return isNewKey;
}
