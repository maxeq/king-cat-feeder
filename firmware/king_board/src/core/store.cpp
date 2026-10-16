#include "store.h"
#include <Preferences.h>
#include <atomic>
#include "worker.h"

static const int N = (int)Key::Count;
static const char* const NAMES[N] = {"door", "tare", "light", "bootfail", "lightall", "dosing"};

static Preferences prefs;  // setup, then the nvs worker only
static std::atomic<int32_t> value[N];
static std::atomic<bool> present[N];
static std::atomic<bool> dirty[N];  // a write job is queued for this key
static bool running = false;        // set once in setup(), before the other tasks start
static Worker nvs("nvs", 0, 4 * 1024, N + 2);

void storeBegin() {
  prefs.begin("king", false);
  for (int k = 0; k < N; k++) {
    present[k] = prefs.isKey(NAMES[k]);
    value[k] = prefs.getInt(NAMES[k], 0);
    dirty[k] = false;
  }
}

static void write(int k) {
  dirty[k] = false;  // first, so a change made during the write queues another one
  prefs.putInt(NAMES[k], value[k]);
}

void storeRun() {
  nvs.begin();
  running = true;
}

int32_t storeGet(Key k, int32_t fallback) { return present[(int)k] ? value[(int)k].load() : fallback; }
bool storeHas(Key k) { return present[(int)k]; }

void storePut(Key k, int32_t v) {
  int i = (int)k;
  if (present[i] && value[i] == v) return;
  value[i] = v;
  present[i] = true;
  if (!running) {
    prefs.putInt(NAMES[i], v);
    return;
  }
  // one queued job per key at most, and the job writes the latest value, so the queue
  // (N + 2 deep) can't fill; if it ever did, the next change of this key retries
  if (!dirty[i].exchange(true) && !nvs.post(write, i)) dirty[i] = false;
}

uint32_t storeStackFree() { return nvs.stackFree(); }
