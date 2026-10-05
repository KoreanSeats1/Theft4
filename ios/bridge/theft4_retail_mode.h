#pragma once
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
// Process-owned mode. The launcher sets it before runtime construction; a
// settings change takes effect after reopening, never midway through a frame.
#ifdef __cplusplus
inline bool theft4_retail_mode() noexcept {
  static const bool enabled=[] {const char* v=getenv("THEFT4_RETAIL_MODE");return v&&strcmp(v,"1")==0;}();
  return enabled;
}
#else
static inline bool theft4_retail_mode(void) {
  const char* v=getenv("THEFT4_RETAIL_MODE");return v&&strcmp(v,"1")==0;
}
#endif
