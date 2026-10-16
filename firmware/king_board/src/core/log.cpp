#include "log.h"
#include "watchdog.h"

static const int LOG_LINES = 24;
static const int LOG_LINE_LEN = 200;
static String ring[LOG_LINES];
static int head = 0;
static void (*mirror)(const String& m) = nullptr;
static TaskHandle_t owner = nullptr;  // the loop task
static QueueHandle_t inbox = nullptr;

void logBegin() {
  owner = xTaskGetCurrentTaskHandle();
  inbox = xQueueCreate(32, LOG_LINE_LEN);
}

void logQuiet(const String& m) {
  ring[head] = String(millis() / 1000) + "s  " + m;
  head = (head + 1) % LOG_LINES;
}

static void print(const String& m) {
  Serial.println(m);
  if (mirror) mirror(m);
  logQuiet(m);
}

void say(const String& m) {
  if (owner && xTaskGetCurrentTaskHandle() != owner) {
    char line[LOG_LINE_LEN];
    strlcpy(line, m.c_str(), sizeof(line));
    xQueueSend(inbox, line, 0);  // dropped if the loop is far behind
    return;
  }
  print(m);
}

void logDrain() {
  char line[LOG_LINE_LEN];
  while (inbox && xQueueReceive(inbox, line, 0) == pdTRUE) {
    print(line);
    watchdogFeed();  // a stalled telnet peer can hold one write for seconds
  }
}

String logText() {
  String out;
  for (int i = 0; i < LOG_LINES; i++) {
    const String& l = ring[(head + i) % LOG_LINES];
    if (l.length()) {
      out += l;
      out += "\n";
    }
  }
  return out;
}

void logMirror(void (*sink)(const String& m)) { mirror = sink; }
