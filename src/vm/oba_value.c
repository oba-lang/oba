#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oba_common.h"
#include "oba_value.h"
#include "oba_vm.h"

ObjString* formatCtor(ObaVM* vm, ObjCtor* ctor) {
  int size = 3 + ctor->family->length + ctor->name->length;
  if (size > FORMAT_VALUE_MAX) {
    size = FORMAT_VALUE_MAX;
  }
  char buf[size];
  int length =
      snprintf(buf, size, "%s::%s", ctor->family->chars, ctor->name->chars);
  return copyString(vm, buf, length);
}

ObjString* formatFunction(ObaVM* vm, ObjFunction* function) {
  if (function->name != NULL) {
    int size = 7 + function->module->name->length + function->name->length;
    if (size > FORMAT_VALUE_MAX) {
      size = FORMAT_VALUE_MAX;
    }
    char buf[size];
    int length = snprintf(buf, size, "<fn %s::%s>",
                          function->module->name->chars, function->name->chars);
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

ObjString* formatValue(ObaVM*, Value);

ObjString* formatModule(ObaVM* vm, ObjModule* module) {
  int size = FORMAT_VALUE_MAX;
  if (module->name->length < size) {
    size = module->name->length;
  }
  char buf[size];
  int length = snprintf(buf, size, "<module %s>", module->name->chars);
  return copyString(vm, buf, size);
}

ObjString* formatInstance(ObaVM* vm, ObjInstance* instance) {
  StringBuffer buffer;
  initStringBuffer(&buffer);

  writeStringBuffer(vm, &buffer, copyString(vm, "(", 1));
  writeStringBuffer(vm, &buffer, formatCtor(vm, instance->ctor));
  writeStringBuffer(vm, &buffer, copyString(vm, ",", 1));

  int field = 0;
  while (field < instance->ctor->arity) {
    ObjString* fieldString = formatValue(vm, instance->fields[field]);
    writeStringBuffer(vm, &buffer, fieldString);
    if (field + 1 < instance->ctor->arity) {
      writeStringBuffer(vm, &buffer, copyString(vm, ",", 1));
    }
    field++;
  }
  writeStringBuffer(vm, &buffer, copyString(vm, ")", 1));

  // Concatenate the array into a single string.
  int fullsize = 0;
  for (int i = 0; i < buffer.count; i++) {
    fullsize += buffer.values[i]->length;
  }

  char* string = ALLOCATE(vm, char, fullsize + 1);
  string[0] = '\0';
  for (int i = 0; i < buffer.count; i++) {
    strcat(string, buffer.values[i]->chars);
  }

  freeStringBuffer(vm, &buffer);
  return takeString(vm, string, fullsize);
}

ObjString* formatObject(ObaVM* vm, Obj* obj) {
  switch (obj->type) {
  case OBJ_CLOSURE:
    return formatFunction(vm, ((ObjClosure*)obj)->function);
  case OBJ_FUNCTION:
    return formatFunction(vm, (ObjFunction*)obj);
  case OBJ_STRING:
    return (ObjString*)obj;
  case OBJ_NATIVE:
    return copyString(vm, "<native fn>", 11);
  case OBJ_UPVALUE:
    return formatValue(vm, *((ObjUpvalue*)obj)->location);
  case OBJ_MODULE:
    return formatModule(vm, (ObjModule*)obj);
  case OBJ_INSTANCE: {
    return formatInstance(vm, (ObjInstance*)obj);
  }
  default:
    return NULL; // Unreachable
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
  Obj* obj = (Obj*)reallocate(vm, NULL, 0, size);
  memset(obj, 0, size);

#ifdef DEBUG_LOG_GC
  printf("@%p allocate %ld  for object type: %d\n", (void*)obj, size, type);
#endif

  obj->type = type;
  obj->next = vm->objects;
  obj->isMarked = false;
  vm->objects = obj;
  return obj;
}

void freeObject(ObaVM* vm, Obj* obj) {
#ifdef DEBUG_LOG_GC
  printf("@%p free object type: %d\n", (void*)obj, obj->type);
  printObject(OBJ_VAL(obj));
  printf("\n");
#endif

  switch (obj->type) {
  case OBJ_STRING: {
    ObjString* string = (ObjString*)obj;
    FREE_ARRAY(vm, char, string->chars, string->length + 1);
    FREE(vm, ObjString, obj);
    break;
  }
  case OBJ_NATIVE:
    FREE(vm, ObjNative, obj);
    break;
  case OBJ_FUNCTION: {
    ObjFunction* function = (ObjFunction*)obj;
    freeChunk(vm, &function->chunk);
    FREE(vm, ObjFunction, obj);
    break;
  }
  case OBJ_CLOSURE: {
    ObjClosure* closure = (ObjClosure*)obj;
    FREE_ARRAY(vm, ObjUpvalue*, closure->upvalues, closure->upvalueCount);
    FREE(vm, ObjClosure, obj);
    break;
  }
  case OBJ_UPVALUE:
    FREE(vm, ObjUpvalue, obj);
    break;
  case OBJ_MODULE: {
    ObjModule* module = (ObjModule*)obj;
    freeTable(vm, module->variables);
    free(module->variables);
    FREE(vm, ObjModule, obj);
    break;
  }
  case OBJ_CTOR:
    FREE(vm, ObjCtor, obj);
    break;
  case OBJ_INSTANCE: {
    ObjInstance* instance = (ObjInstance*)obj;
    FREE_ARRAY(vm, Value, instance->fields, instance->ctor->arity);
    FREE(vm, ObjInstance, obj);
    break;
  }
  }
}

DEFINE_BUFFER(Byte, uint8_t)
DEFINE_BUFFER(Value, Value)
DEFINE_BUFFER(String, ObjString*)

ObjString* allocateString(ObaVM* vm, char* chars, int length, uint32_t hash) {
  ObjString* string = ALLOCATE_OBJ(vm, ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;

  obaPushRoot(vm, (Obj*)string);
  tableSet(vm, vm->strings, string, NIL_VAL);
  obaPopRoot(vm);

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

static ObjString* tableFindString(Table* table, const char* chars, int length,
                                  uint32_t hash);
ObjString* copyString(ObaVM* vm, const char* chars, int length) {
  uint32_t hash = hashString(chars, length);

  ObjString* interned = tableFindString(vm->strings, chars, length, hash);
  if (interned != NULL) {
    return interned;
  }

  char* heapChars = ALLOCATE(vm, char, length + 1);
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';

  return allocateString(vm, heapChars, length, hash);
}

ObjString* takeString(ObaVM* vm, char* chars, int length) {
  uint32_t hash = hashString(chars, length);

  ObjString* interned = tableFindString(vm->strings, chars, length, hash);
  if (interned != NULL) {
    FREE_ARRAY(vm, char, chars, length + 1);
    return interned;
  }

  return allocateString(vm, chars, length, hash);
}

ObjNative* newNative(ObaVM* vm, NativeFn function) {
  ObjNative* native = ALLOCATE_OBJ(vm, ObjNative, OBJ_NATIVE);
  native->function = function;
  return native;
}

ObjModule* newModule(ObaVM* vm, ObjString* name) {
  obaPushRoot(vm, (Obj*)name);

  ObjModule* module = ALLOCATE_OBJ(vm, ObjModule, OBJ_MODULE);
  module->name = name;

  obaPopRoot(vm);
  obaPushRoot(vm, (Obj*)module);

  module->variables = (Table*)reallocate(vm, NULL, 0, sizeof(Table));
  initTable(module->variables);

  obaPopRoot(vm);
  return module;
}

ObjCtor* newCtor(ObaVM* vm, ObjString* family, ObjString* name, int arity) {
  obaPushRoot(vm, (Obj*)family);
  obaPushRoot(vm, (Obj*)name);

  ObjCtor* ctor = ALLOCATE_OBJ(vm, ObjCtor, OBJ_CTOR);

  obaPopRoot(vm);
  obaPopRoot(vm);

  ctor->family = family;
  ctor->name = name;
  ctor->arity = arity;
  return ctor;
}

ObjInstance* newInstance(ObaVM* vm, ObjCtor* ctor) {
  obaPushRoot(vm, (Obj*)ctor);

  // Allocate the fields before the ObjInstance, in-case a GC is triggered and
  // we attempt to print the instance, which would iterate over a null pointer.
  Value* fields = ALLOCATE(vm, Value, ctor->arity);
  ObjInstance* instance = ALLOCATE_OBJ(vm, ObjInstance, OBJ_INSTANCE);
  instance->ctor = ctor;
  instance->fields = fields;

  obaPopRoot(vm); // ctor.
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

ObjString* formatNumber(ObaVM* vm, Value value) {
  char buf[FORMAT_VALUE_MAX];
  int length = snprintf(buf, FORMAT_VALUE_MAX, "%g", AS_NUMBER(value));
  return copyString(vm, buf, length);
}

ObjString* formatBool(ObaVM* vm, Value value) {
  return AS_BOOL(value) ? copyString(vm, "true", 4)
                        : copyString(vm, "false", 5);
}

ObjString* formatValue(ObaVM* vm, Value value) {
  switch (value.type) {
  case VAL_NUMBER:
    return formatNumber(vm, value);
  case VAL_BOOL:
    return formatBool(vm, value);
  case VAL_NIL:
    return copyString(vm, "nil", 3);
  case VAL_OBJ:
    return formatObject(vm, AS_OBJ(value));
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

void obaGrayStringBuffer(ObaVM* vm, StringBuffer* buf) {
  for (int i = 0; i < buf->count; i++) {
    obaGrayObject(vm, (Obj*)buf->values[i]);
  }
}

void obaGrayValueBuffer(ObaVM* vm, ValueBuffer* buf) {
  for (int i = 0; i < buf->count; i++) {
    obaGrayValue(vm, buf->values[i]);
  }
}

void obaGrayTable(ObaVM* vm, Table* table) {
  if (table == NULL) return;
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    obaGrayObject(vm, (Obj*)entry->key);
    obaGrayValue(vm, entry->value);
  }
}

void blackenObject(ObaVM* vm, Obj* obj) {
#ifdef DEBUG_LOG_GC
  printf("@%p blacken ", (void*)obj);
  printValue(OBJ_VAL(obj));
  printf("\n");
#endif

  switch (obj->type) {
  case OBJ_NATIVE:
  case OBJ_STRING:
    break;
  case OBJ_CLOSURE: {
    ObjClosure* closure = (ObjClosure*)obj;
    obaGrayObject(vm, (Obj*)closure->function);
    if (closure->upvalues != NULL) {
      for (int i = 0; i < closure->upvalueCount; i++) {
        obaGrayObject(vm, (Obj*)closure->upvalues[i]);
      }
    }
    break;
  }
  case OBJ_FUNCTION: {
    ObjFunction* function = (ObjFunction*)obj;
    obaGrayObject(vm, (Obj*)function->name);
    obaGrayObject(vm, (Obj*)function->module);
    obaGrayValueBuffer(vm, &function->chunk.constants);
    break;
  }
  case OBJ_UPVALUE: {
    ObjUpvalue* upvalue = (ObjUpvalue*)obj;
    obaGrayValue(vm, upvalue->closed);
    // No need to mark upvalue->location; It's on the stack, so it's marked as a
    // GC root by the VM.
    break;
  }
  case OBJ_MODULE: {
    ObjModule* module = (ObjModule*)obj;
    obaGrayTable(vm, module->variables);
    obaGrayObject(vm, (Obj*)module->name);
    break;
  }
  case OBJ_CTOR: {
    ObjCtor* ctor = (ObjCtor*)obj;
    obaGrayObject(vm, (Obj*)ctor->name);
    obaGrayObject(vm, (Obj*)ctor->family);
    break;
  }
  case OBJ_INSTANCE: {
    ObjInstance* instance = (ObjInstance*)obj;
    obaGrayObject(vm, (Obj*)instance->ctor);
    if (instance->fields != NULL) {
      for (int i = 0; i < instance->ctor->arity; i++) {
        obaGrayValue(vm, instance->fields[i]);
      }
    }
    break;
  }
  }
}

void obaGrayObject(ObaVM* vm, Obj* obj) {
  if (obj == NULL) return;
  if (obj->isMarked) return;
#ifdef DEBUG_LOG_GC
  printf("@%p mark ", (void*)obj);
  printValue(OBJ_VAL(obj));
  printf("\n");
#endif

  obj->isMarked = true;

  // TOOD(kendal): Why not use an ObjectBuffer (dynamic array) here?
  if (vm->grayCapacity < vm->grayCount + 1) {
    vm->grayCapacity = GROW_CAPACITY(vm->grayCapacity);
    // realloc directly to avoid triggering a recursive GC.
    vm->grayStack = realloc(vm->grayStack, sizeof(Obj*) * vm->grayCapacity);
    // If we can't mark objects for GC, just exit.
    if (vm->grayStack == NULL) {
#ifdef DEBUG_LOG_GC
      fprintf(stderr, "OOM during GC gray stack allocation\n");
#endif
      exit(1);
    }
  }

  vm->grayStack[vm->grayCount++] = obj;
}

void obaGrayValue(ObaVM* vm, Value value) {
  if (!IS_OBJ(value)) return;
  obaGrayObject(vm, AS_OBJ(value));
}

void initTable(Table* table) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
}

// Frees the memory held by all table entries. This does not free the table
// pointer itself.
void freeTable(ObaVM* vm, Table* table) {
  FREE_ARRAY(vm, Entry, table->entries, table->capacity);
  initTable(table);
}

Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
  uint32_t index = key->hash % capacity;
  Entry* tombstone = NULL;

  for (;;) {
    Entry* entry = &entries[index];

    if (entry->key == NULL) {
      if (IS_NIL(entry->value)) {
        // tombstone.
        return tombstone != NULL ? tombstone : entry;
      } else {
        if (tombstone == NULL) tombstone = entry;
      }
    } else if (entry->key->hash == key->hash) {
      return entry;
    }

    index = (index + 1) % capacity;
  }
}

void adjustCapacity(ObaVM* vm, Table* table, int capacity) {
  Entry* entries = ALLOCATE(vm, Entry, capacity + 1);
  for (int i = 0; i < capacity; i++) {
    entries[i].key = NULL;
    entries[i].value = NIL_VAL;
  }

  table->count = 0;
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if (entry->key == NULL) continue;

    Entry* dest = findEntry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
    table->count++;
  }

  FREE_ARRAY(vm, Entry, table->entries, table->capacity + 1);
  table->entries = entries;
  table->capacity = capacity;
}

static ObjString* tableFindString(Table* table, const char* chars, int length,
                                  uint32_t hash) {
  if (table->count == 0) return NULL;

  uint32_t index = hash % table->capacity;

  for (;;) {
    Entry* entry = &table->entries[index];
    if (entry->key == NULL) {
      if (IS_NIL(entry->value)) return NULL;
    } else if (entry->key->length == length && entry->key->hash == hash &&
               memcmp(entry->key->chars, chars, length) == 0) {
      return entry->key;
    }
    index = (index + 1) % table->capacity;
  }
}

bool tableGet(Table* table, ObjString* key, Value* value) {
  if (table->count == 0) return false;

  Entry* entry = findEntry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;

  *value = entry->value;
  return true;
}

bool tableSet(ObaVM* vm, Table* table, ObjString* key, Value value) {
  if (table->count >= table->capacity * TABLE_MAX_LOAD) {
    int capacity = GROW_CAPACITY(table->capacity);
    adjustCapacity(vm, table, capacity);
  }

  Entry* entry = findEntry(table->entries, table->capacity, key);

  bool isNewKey = entry->key == NULL;
  if (isNewKey && IS_NIL(entry->value)) table->count++;

  entry->key = key;
  entry->value = value;
  return isNewKey;
}

bool tableDelete(ObaVM* vm, Table* table, ObjString* key) {
  if (table->count == 0) return false;

  Entry* entry = findEntry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;

  entry->key = NULL;
  entry->value = OBA_BOOL(true);

  return true;
}
