#ifndef oba_core_modules_h
#define oba_core_modules_h

extern const char* systemModSource;
const char* obaSystemModSource() { return systemModSource; }

extern const char* timeModSource;
const char* obaTimeModSource() { return timeModSource; }

extern const char* optionModSource;
const char* obaOptionModSource() { return optionModSource; }

extern const char* __globals__ModSource;
const char* obaGlobalsModSource() { return __globals__ModSource; }

#endif
