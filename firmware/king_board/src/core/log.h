#pragma once
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Log lines go to serial, an optional mirror (telnet) and a ring buffer for the web page.
// say() is safe from any task and never allocates: lines from other tasks are copied into
// a queue and printed by the loop task in logDrain(), so serial and telnet are only ever
// touched from one place. A line that doesn't fit in the queue is counted, never lost silently.
// Every line goes through here: nothing else prints to Serial (check.sh enforces it), so what
// the board says reaches /log and telnet too. warnf() and errorf() mark a line ("! ", "!! ")
// and count it, so /health shows that something went wrong without reading the log.
const size_t LOG_LINE_LEN = 200;

void logBegin();  // call early in setup(); the loop task becomes the one that prints
void say(const char* m);
void sayf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));    // printf into a stack buffer
void warnf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));   // degraded, but working
void errorf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));  // something failed
uint32_t logWarnings();
uint32_t logErrors();
void logQuiet(const char* m);           // ring buffer only, loop task only
void logDrain();                        // loop task: print lines queued by other tasks
size_t logText(char* out, size_t cap);  // loop task: the ring, oldest first, one line each
uint32_t logDropped();                  // lines lost because the queue was full
void logMirror(void (*sink)(const char* m));
