#ifndef oba_common_h
#define oba_common_h

// Assertions add significant overhead, so are only enabled in debug builds.

#ifdef DEBUG_MODE

#define ASSERT(condition, message)                                             \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "[%s:%d] Assert failed in %s(): %s\n", __FILE__,         \
              __LINE__, __func__, message);                                    \
      abort();                                                                 \
    }                                                                          \
  } while (false)

#else

// clang-format off
#define ASSERT(condition, message) do {} while (false)
// clang-format on

#endif

#endif
