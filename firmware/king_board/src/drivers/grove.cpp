// Grove Vision AI V2 over I2C. The library handles inference; the raw
// transport below is for snapshots and debugging.
//
// After groveBegin() the bus belongs to the "i2c" worker on core 0: every exchange
// runs as a job there, one at a time. Callers post requests and read the latest
// result, so a multi-second camera exchange never blocks dosing, the door or the web.
#include "grove.h"
#include <Wire.h>
#include <atomic>
#include <esp_heap_caps.h>
#include "../config.h"
#include "../core/lock.h"
#include "../core/log.h"
#include "../core/watchdog.h"
#include "../core/worker.h"
#include "sscma/Seeed_Arduino_SSCMA.h"  // patched copy: fixed buffers, no spin

static SSCMA AI;
static const uint8_t GROVE = 0x62;
static const uint32_t I2C_HZ = 50000;        // no external pull-ups on the bus
static const uint16_t I2C_TIMEOUT_MS = 200;  // per transfer; Grove stretches the clock
enum { SS_READ = 0x01, SS_WRITE = 0x02, SS_AVAIL = 0x03 };
// transport timing, ms: the gap Grove needs between packets. The debug paths (raw AT, scan)
// keep the slower 3 ms they were tuned with; frames and the library use 2 ms.
static const int LIB_WAIT_MS = 2;     // passed to the library's begin()
static const int FRAME_WAIT_MS = 2;   // between packets while reading a frame
static const int DEBUG_WAIT_MS = 3;   // between packets in the debug paths
static const int FRAME_POLL_MS = 15;  // nothing available yet: look again after this
static const int DEBUG_POLL_MS = 30;
static const int ID_REPLY_MS = 150;    // AT+ID? answers within this
static const int SCAN_SETTLE_MS = 10;  // after re-pinning the bus for a scan
static const int BUS_SETTLE_MS = 1;    // after switching the bus pins by hand

static Worker i2c("i2c", 0, 12 * 1024);
static bool online = false;  // worker-owned after groveBegin()
static std::atomic<int> queued{0};

// frames: see grove.h
static const size_t IMG_CAP = 60000;
static const int BUFS = 3;
static char* buf[BUFS];      // allocated once in groveBegin(), nullptr if PSRAM is missing
static size_t bufLen[BUFS];  // guarded by resultLock
static Mutex resultLock;
static AiResult latest;  // guarded by resultLock
static int front = -1;   // the buffer with the latest frame, guarded by resultLock
static int pinned = -1;  // the buffer a reader is streaming, guarded by resultLock
static std::atomic<uint32_t> latestAt{0};
static std::atomic<bool> latestOnline{false};

AiFrame aiPin() {
  AiFrame f;
  Locked lock(resultLock);
  if (!lock) return f;
  f.result = latest;
  if (front >= 0) {
    pinned = front;
    f.image = buf[front];
    f.len = bufLen[front];
  }
  return f;
}

void aiRelease() {
  Locked lock(resultLock);
  if (lock) pinned = -1;  // on a timeout the worker just has one buffer fewer to pick from
}

AiStatus aiStatus() { return {latestAt, latestOnline}; }

bool aiBusy() { return queued > 0; }

uint32_t aiStackFree() { return i2c.stackFree(); }

static int freeBuffer() {  // a buffer neither published nor being streamed, -1 if none
  Locked lock(resultLock);
  if (!lock) return -1;
  for (int i = 0; i < BUFS; i++)
    if (buf[i] && i != front && i != pinned) return i;
  return -1;
}

static void publish(const AiResult& r, int b, size_t len) {  // a frame and its detections together
  Locked lock(resultLock);
  if (!lock) return;
  latest = r;
  if (b >= 0) {
    bufLen[b] = len;
    front = b;
  }
  latestAt = r.at;
  latestOnline = r.online;
}

static void setOnline(bool on) {  // failed request: keep the last good frame, update the status only
  Locked lock(resultLock);
  if (!lock) return;
  latest.online = on;
  latestOnline = on;
}

static void busBegin() {
  Wire.setBufferSize(1024);  // SSCMA packets are 250 B
  Wire.begin(PIN_AI_SDA, PIN_AI_SCL, I2C_HZ);
  Wire.setTimeOut(I2C_TIMEOUT_MS);  // each bus transfer
  Wire.setTimeout(I2C_TIMEOUT_MS);  // and each readBytes() (the Stream timeout, 1 s by default)
}

void groveBegin() {
  pinMode(PIN_AI_SDA, INPUT_PULLUP);
  pinMode(PIN_AI_SCL, INPUT_PULLUP);
  busBegin();
  online = AI.begin(&Wire, -1, GROVE, LIB_WAIT_MS, I2C_HZ);
  if (online)
    say("Grove Vision AI: online (I2C 0x62)");
  else
    warnf("Grove Vision AI: not responding");
  for (int i = 0; i < BUFS; i++)
    buf[i] = (char*)heap_caps_malloc(IMG_CAP + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf[BUFS - 1]) warnf("Grove: no PSRAM for frames, camera views show results only");
  AiResult r;
  r.online = online;
  publish(r, -1, 0);
  i2c.begin();
}

// one transport packet: 0x10, cmd, length, payload, two zero bytes
static int sscmaCmd(uint8_t cmd, uint16_t len, const uint8_t* data, int wait) {
  delay(wait);
  Wire.beginTransmission(GROVE);
  Wire.write(0x10);
  Wire.write(cmd);
  Wire.write(len >> 8);
  Wire.write(len & 0xFF);
  if (data) Wire.write(data, len);
  Wire.write(0);
  Wire.write(0);
  return Wire.endTransmission();
}

static int sscmaAvail(int wait) {
  sscmaCmd(SS_AVAIL, 0, NULL, wait);
  delay(wait);
  Wire.requestFrom(GROVE, (uint8_t)2);
  int hi = Wire.read();  // two statements: the order of reads inside one expression is unspecified
  int lo = Wire.read();
  return (hi << 8) | lo;
}

// raw AT over the SSCMA transport, bypassing the library (debug): printable reply into out
static void rawAT(const char* q, uint32_t ms, char* out, size_t cap) {
  sscmaCmd(SS_WRITE, (uint16_t)strlen(q), (const uint8_t*)q, DEBUG_WAIT_MS);
  size_t n = 0;
  for (uint32_t t0 = millis(); millis() - t0 < ms && n + 1 < cap;) {
    watchdogFeed();
    int avail = sscmaAvail(DEBUG_WAIT_MS);
    if (avail <= 0 || avail > 20000) {
      delay(DEBUG_POLL_MS);
      continue;
    }
    int chunk = min(avail, 200);
    sscmaCmd(SS_READ, (uint16_t)chunk, NULL, DEBUG_WAIT_MS);
    delay(DEBUG_WAIT_MS);
    Wire.requestFrom(GROVE, (uint8_t)chunk);
    while (Wire.available()) {
      char c = (char)Wire.read();
      if ((c >= 32 || c == '\n') && n + 1 < cap) out[n++] = c;
    }
  }
  if (!n)
    strlcpy(out, "(silence)", cap);
  else
    out[n] = 0;
}

// unstick the bus: 16 SCL clocks + STOP
static void aiRecover() {
  Wire.end();
  pinMode(PIN_AI_SDA, INPUT_PULLUP);
  pinMode(PIN_AI_SCL, INPUT_PULLUP);
  delay(BUS_SETTLE_MS);
  int sda = digitalRead(PIN_AI_SDA), scl = digitalRead(PIN_AI_SCL);
  pinMode(PIN_AI_SCL, OUTPUT_OPEN_DRAIN);
  for (int i = 0; i < 16; i++) {
    digitalWrite(PIN_AI_SCL, LOW);
    delayMicroseconds(20);
    digitalWrite(PIN_AI_SCL, HIGH);
    delayMicroseconds(20);
  }
  pinMode(PIN_AI_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_AI_SDA, LOW);
  delayMicroseconds(20);
  digitalWrite(PIN_AI_SCL, HIGH);
  delayMicroseconds(20);
  digitalWrite(PIN_AI_SDA, HIGH);
  delayMicroseconds(20);
  pinMode(PIN_AI_SDA, INPUT_PULLUP);
  delay(BUS_SETTLE_MS);
  sayf("AI: bus recovery (before: SDA=%d SCL=%d, after: SDA=%d)", sda, scl, digitalRead(PIN_AI_SDA));
  busBegin();
}

// a missing Grove would otherwise stall web and OTA on timeouts
static bool aiPresent() {
  for (int k = 0; k < 2; k++) {
    Wire.beginTransmission(GROVE);
    if (Wire.endTransmission() == 0) return true;
    if (k == 0) aiRecover();
  }
  return false;
}

// AT+SAMPLE=1 returns one base64 jpeg, ~15 KB, inside a JSON reply. Reads the reply into
// buf[b], then moves the image to the start of it. Takes seconds at 50 kHz: preview only.
static size_t aiSample(int b, uint32_t ms) {
  char* out = buf[b];
  const char* q = "AT+SAMPLE=1\r\n";
  sscmaCmd(SS_WRITE, (uint16_t)strlen(q), (const uint8_t*)q, FRAME_WAIT_MS);
  size_t n = 0;
  out[0] = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < ms && n < IMG_CAP) {
    watchdogFeed();
    int avail = sscmaAvail(FRAME_WAIT_MS);
    if (avail <= 0 || avail > (int)IMG_CAP) {
      out[n] = 0;
      if (n && out[n - 1] == '\n' && strstr(out, "\"image\"")) break;  // the whole reply is in
      delay(FRAME_POLL_MS);
      continue;
    }
    while (avail > 0 && n < IMG_CAP && millis() - t0 < ms) {  // the deadline holds mid-burst too
      watchdogFeed();
      int chunk = min(avail, (int)MAX_PL_LEN);
      sscmaCmd(SS_READ, (uint16_t)chunk, NULL, FRAME_WAIT_MS);
      delay(FRAME_WAIT_MS);
      Wire.requestFrom(GROVE, (uint8_t)chunk);
      while (Wire.available()) {
        char c = (char)Wire.read();
        if (n < IMG_CAP) out[n++] = c;
      }
      avail -= chunk;
    }
  }
  out[n] = 0;
  const char* key = strstr(out, "\"image\"");
  const char* colon = key ? strchr(key, ':') : nullptr;
  const char* s = colon ? strchr(colon + 1, '"') : nullptr;
  const char* e = s ? strchr(s + 1, '"') : nullptr;
  if (!e) return 0;
  size_t len = (size_t)(e - s - 1);
  memmove(out, s + 1, len);
  out[len] = 0;
  return len;
}

// inference on the current frame; fills r, logs, false if Grove didn't answer
static bool invoke(AiResult& r) {
  r.count = 0;
  r.best = 0;
  if (!aiPresent()) {
    r.online = online = false;
    say("AI: not on bus");
    return false;
  }
  if (!online) {
    online = AI.begin(&Wire, -1, GROVE, LIB_WAIT_MS, I2C_HZ);
    if (!online) {
      r.online = false;
      say("AI: not responding");
      return false;
    }
  }
  r.online = true;
  uint32_t t0 = millis();
  // invoke(n, filter, show): filter=true gives results only.
  // With the jpeg attached the reply is too big and times out.
  // each library call is bounded (two waits of ~1 s plus one capped read pass each), and the
  // watchdog is fed between them, so a sick bus can't stretch the job past it
  int rc = AI.invoke(1, true, false);
  watchdogFeed();
  if (rc != CMD_OK) rc = AI.invoke(1, true, false);
  watchdogFeed();
  if (rc != CMD_OK) {
    char raw[256];
    rawAT("AT+INVOKE=1,1,0\r\n", AI_RAW_TIMEOUT_MS, raw, sizeof(raw));
    warnf("AI: invoke err %d | raw reply: %s", rc, raw);
    return false;
  }
  size_t n = 0;
  r.verdict[0] = 0;
  r.count = (int)AI.boxes().size();
  for (auto& b : AI.boxes()) {
    r.best = max(r.best, (int)b.score);
    if (n < sizeof(r.verdict))
      n += (size_t)snprintf(r.verdict + n, sizeof(r.verdict) - n, "box[cls %d %d%% @%d,%d] ", b.target,
                            b.score, b.x, b.y);
  }
  for (auto& c : AI.classes())
    if (n < sizeof(r.verdict))
      n += (size_t)snprintf(r.verdict + n, sizeof(r.verdict) - n, "class[cls %d %d%%] ", c.target, c.score);
  if (!r.verdict[0]) strlcpy(r.verdict, "empty (nothing detected)", sizeof(r.verdict));
  sayf("AI (%lums): %s", (unsigned long)(millis() - t0), r.verdict);
  return true;
}

// --- jobs: run on the i2c worker ---

// publish only when the frame and its detections come from the same exchange
static void finish(AiResult& r, int b, size_t len, bool ok, uint32_t t0) {
  bool images = buf[BUFS - 1] != nullptr;  // without PSRAM the views show results only
  if (!ok || (images && !len)) {
    setOnline(r.online);
    return;
  }
  r.ms = millis() - t0;
  r.at = millis() | 1;  // 0 means "never"
  publish(r, len ? b : -1, len);
}

static void frameJob(int) {  // snapshot + inference, for the camera views
  uint32_t t0 = millis();
  AiResult r;
  int b = freeBuffer();
  size_t len = 0;
  bool ok = false;
  if (aiPresent()) {
    if (b >= 0) len = aiSample(b, AI_FRAME_TIMEOUT_MS);
    ok = invoke(r);
  } else {
    r.online = online = false;
  }
  finish(r, b, len, ok, t0);
  queued--;
}

static void invokeJob(int) {  // console 'v': inference only, the result goes to the log
  AiResult r;
  invoke(r);
  setOnline(r.online);
  queued--;
}

static void snapshotJob(int) {
  bool ok = aiPresent();
  int b = freeBuffer();
  size_t len = ok && b >= 0 ? aiSample(b, AI_FRAME_TIMEOUT_MS) : 0;
  sayf("AI: snapshot %s%u chars", ok ? "" : "(not on bus) ", (unsigned)len);
  queued--;
}

static void scanJob(int);

static void request(Worker::Job job, const char* what) {
  queued++;
  if (i2c.post(job)) return;
  queued--;
  warnf("AI: queue full, %s dropped", what);
}

void aiRequestFrame() {
  if (!aiBusy()) request(frameJob, "frame");
}
void aiRequestInvoke() { request(invokeJob, "inference"); }
void aiRequestSnapshot() { request(snapshotJob, "snapshot"); }
void aiRequestScan() { request(scanJob, "scan"); }

// scan both SDA/SCL orders, then ping Grove with AT+ID?
static void scanJob(int) {
  for (int m = 0; m < 2; m++) {
    int sda = m ? PIN_AI_SCL : PIN_AI_SDA, scl = m ? PIN_AI_SDA : PIN_AI_SCL;
    Wire.end();
    pinMode(sda, INPUT_PULLUP);
    pinMode(scl, INPUT_PULLUP);
    Wire.begin(sda, scl, AI_SCAN_HZ);
    delay(SCAN_SETTLE_MS);
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
      watchdogFeed();  // each probe can take the full I2C timeout on a stuck bus
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) {
        sayf("  SDA=%d SCL=%d: 0x%02X%s", sda, scl, a, a == GROVE ? "  <-- GROVE!" : "");
        found++;
      }
    }
    if (!found) sayf("  SDA=%d SCL=%d: empty", sda, scl);
  }
  say("scan done");
  Wire.end();
  busBegin();

  const char* q = "AT+ID?\r\n";
  int e1 = sscmaCmd(SS_WRITE, (uint16_t)strlen(q), (const uint8_t*)q, DEBUG_WAIT_MS);
  delay(ID_REPLY_MS);
  int e2 = sscmaCmd(SS_AVAIL, 0, NULL, DEBUG_WAIT_MS);
  delay(DEBUG_WAIT_MS);
  Wire.requestFrom(GROVE, (uint8_t)2);
  int hi = Wire.read(), lo = Wire.read();
  int avail = (hi << 8) | lo;
  sayf("raw: write=%d availreq=%d avail=%d", e1, e2, avail);
  if (avail > 0 && avail < 1000) {
    int n = min(avail, 240);
    sscmaCmd(SS_READ, (uint16_t)n, NULL, DEBUG_WAIT_MS);
    delay(DEBUG_WAIT_MS);
    Wire.requestFrom(GROVE, (uint8_t)n);
    char r[241];
    int k = 0;
    while (Wire.available()) {
      char c = (char)Wire.read();
      if ((c >= 32 || c == '\n') && k < 240) r[k++] = c;
    }
    r[k] = 0;
    sayf("raw reply: %s", r);
  }
  queued--;
}
