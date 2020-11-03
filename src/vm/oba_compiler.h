#ifndef oba_compiler_h
#define oba_compiler_h

#include <stdbool.h>

#include "oba.h"
#include "oba_function.h"
#include "oba_vm.h"

// Compiles [source], a string of Oba source code.
// Code is always compiled into a function pointer. Returns NULL iff an error
// occurred while compiling. Code should not be executed if so.
ObjFunction* obaCompile(ObaVM* vm, ObjModule* module, const char* source);

typedef struct Compiler Compiler;

void markCompilerRoots(ObaVM*, Compiler*);

#endif
