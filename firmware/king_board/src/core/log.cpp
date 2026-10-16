#include "log.h"
#include <atomic>
#include <stdarg.h>
#include "watchdog.h"

static const int LOG_LINES = 24;
static const size_t STAMP_LEN = 16;                     // "4294967s  "
static char ring[LOG_LINES][STAMP_LEN + LOG_LINE_LEN];  // loop task only
static int head = 0;
static void (*mirror)(const char* m) = nullptr;
static TaskHandle_t owner = nullptr;  // the loop task
static QueueHandle_t inbox = nullptr;
static std::atomic<uint32_t> dropped{0}, warnings{0}, errors{0};
static uint32_t droppedShown = 0;

void logBegin() {
  owner = xTaskGetCurrentTaskHandle();
  inbox = xQueueCreate(32, LOG_LINE_LEN);
}

void logQuiet(const char* m) {
  snprintf(ring[head], sizeof(ring[head]), "%lus  %s", (unsigned long)(millis() / 1000), m);
  head = (head + 1) % LOG_LINES;
}

static void print(const char* m) {
  Serial.println(m);
  if (mirror) mirror(m);
  logQuiet(m);
}

void say(const char* m) {
  if (owner && xTaskGetCurrentTaskHandle() != owner) {
    char line[LOG_LINE_LEN];
    strlcpy(line, m, sizeof(line));
    if (xQueueSend(inbox, line, 0) != pdTRUE) dropped++;  // the loop is far behind
    return;
  }
  print(m);
}

static void vsay(const char* mark, const char* fmt, va_list ap) {
  char line[LOG_LINE_LEN];
  size_t n = strlcpy(line, mark, sizeof(line));
  vsnprintf(line + n, sizeof(line) - n, fmt, ap);
  say(line);
}

void sayf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsay("", fmt, ap);
  va_end(ap);
}

void warnf(const char* fmt, ...) {
  warnings++;
  va_list ap;
  va_start(ap, fmt);
  vsay("! ", fmt, ap);
  va_end(ap);
}

void errorf(const char* fmt, ...) {
  errors++;
  va_list ap;
  va_start(ap, fmt);
  vsay("!! ", fmt, ap);
  va_end(ap);
}

uint32_t logWarnings() { return warnings; }
uint32_t logErrors() { return errors; }

void logDrain() {
  char line[LOG_LINE_LEN];
  while (inbox && xQueueReceive(inbox, line, 0) == pdTRUE) {
    print(line);
    watchdogFeed();  // a stalled telnet peer can hold one write for a while
  }
  uint32_t lost = dropped;
  if (lost != droppedShown) {
    snprintf(line, sizeof(line), "! log: %lu lines dropped (queue full)",
             (unsigned long)(lost - droppedShown));
    warnings++;
    droppedShown = lost;
    print(line);
  }
}

size_t logText(char* out, size_t cap) {
  size_t n = 0;
  if (cap) out[0] = 0;
  for (int i = 0; i < LOG_LINES; i++) {
    const char* l = ring[(head + i) % LOG_LINES];
    if (!l[0]) continue;
    int w = snprintf(out + n, cap - n, "%s\n", l);
    if (w < 0 || (size_t)w >= cap - n) break;  // full: keep what fits
    n += (size_t)w;
  }
  return n;
}

uint32_t logDropped() { return dropped; }

void logMirror(void (*sink)(const char* m)) { mirror = sink; }
