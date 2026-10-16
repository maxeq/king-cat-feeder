#pragma once

// Task watchdog. Every long-running task subscribes itself and feeds it; if any
// of them stops, the board reboots instead of hanging with the rotor or door in
// an unknown state. The reset reason is logged on the next boot.
void watchdogBegin(unsigned seconds);  // once, from setup()
void watchdogSubscribe();              // the calling task
void watchdogUnsubscribe();            // the calling task (OTA writes flash in one long call)
void watchdogFeed();                   // the calling task; no-op if it isn't subscribed
const char* resetReason();
