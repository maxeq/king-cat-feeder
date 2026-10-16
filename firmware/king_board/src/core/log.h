#pragma once
#include <Arduino.h>

// Log lines go to serial, an optional mirror (telnet) and a ring buffer for the web page.
// say() is safe from any task: lines from background workers are queued and printed
// by the loop task in logDrain(), so serial and telnet are only ever touched from one place.
void logBegin();  // call early in setup(); the loop task becomes the one that prints
void say(const String& m);
void logQuiet(const String& m);  // ring buffer only, loop task only
void logDrain();                 // loop task: print lines queued by workers
String logText();
void logMirror(void (*sink)(const String& m));
