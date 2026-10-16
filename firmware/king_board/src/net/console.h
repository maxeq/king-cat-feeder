#pragma once

// One-letter commands over serial or telnet (port 23). See king_board.ino for the list.
void consoleBegin();  // telnet server, once Wi-Fi is up
void consolePoll();
void heartbeat();  // status line into the web log every 2 s
