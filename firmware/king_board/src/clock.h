#pragma once
#include <stdint.h>

// millis() wraps every ~49.7 days. Compare deadlines by signed difference, never by
// `now > deadline`, so a deadline set just before the wrap still fires on time.
// A signed difference only reaches ~24.8 days, so a deadline that has fired must be
// cleared (0 = none) rather than left behind: an old one would read "not yet" again.
inline bool due(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }
