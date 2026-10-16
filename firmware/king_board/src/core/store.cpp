#include "store.h"
#include <Preferences.h>

static Preferences prefs;

void storeBegin() { prefs.begin("king", false); }
int32_t storeGet(const char* key, int32_t fallback) { return prefs.getInt(key, fallback); }
bool storeHas(const char* key) { return prefs.isKey(key); }

void storePut(const char* key, int32_t value) {
  if (storeHas(key) && prefs.getInt(key) == value) return;
  prefs.putInt(key, value);
}
