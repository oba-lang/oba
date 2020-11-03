#ifndef oba_h
#define oba_h

#include <stdbool.h>

#define OBA_VERSION_STRING "0.0.1"

// Public APIs for the Oba language -------------------------------------------

typedef enum {
  OBA_RESULT_SUCCESS,
  OBA_RESULT_COMPILE_ERROR,
  OBA_RESULT_RUNTIME_ERROR
} ObaInterpretResult;

// Builtin represents a named C function that is callable from Oba source code.
typedef struct Builtin Builtin;

// A single virtual machine for execute Oba code.
typedef struct ObaVM ObaVM;

// Creates a new Oba Virtual Machine.
ObaVM* obaNewVM(Builtin*, int);

// Disposes of all resources in use by the vm, which was previously created by
// a call to [obaVM].
void obaFreeVM(ObaVM*);

// Runs [source], a string of Oba source code.
ObaInterpretResult obaInterpret(ObaVM* vm, const char* source);

// Triggers a garbage-collection in the VM.
void obaCollectGarbage(ObaVM* vm);

void obaErrorf(ObaVM* vm, const char* format, ...);
void obaArityError(ObaVM* vm, int want, int got);
void obaTypeError(ObaVM* vm, const char* expected);
bool obaHasError(ObaVM* vm);

#endif
