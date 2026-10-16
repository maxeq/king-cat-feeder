#pragma once

// App-level OTA rollback. The prebuilt bootloader can't roll back on its own, so the
// app counts boots that ended in a crash or a watchdog reset without ever running
// healthy. After 3 in a row it boots the other app slot, if that holds a valid image.
void rollbackCheck();  // early in setup(), after storeBegin()
void rollbackPoll();   // from loop(): marks this firmware healthy after a minute of uptime
bool bootCrashed();    // the last reset was a crash or a watchdog
