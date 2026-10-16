#pragma once
#include <stdarg.h>
#include <stdio.h>

// A few log lines produced by one logic step. Fixed size, no heap.
struct Messages {
  static const int MAX = 8, LEN = 112;
  char line[MAX][LEN];
  int count = 0;

  void add(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (count >= MAX) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line[count++], LEN, fmt, ap);
    va_end(ap);
  }
  void append(const Messages& other) {
    for (int i = 0; i < other.count; i++)
      add("%s", other.line[i]);
  }
};
