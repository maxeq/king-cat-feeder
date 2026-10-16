#pragma once

void wifiOnFirstConnect(void (*fn)());  // e.g. start web and telnet, runs once
bool wifiBegin();                       // blocks up to 20 s, true when connected
void wifiPoll();                        // reconnects every 10 s while the link is down
