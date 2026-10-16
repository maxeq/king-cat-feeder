// Grove Vision AI V2 over I2C. The library handles inference; the raw
// transport below is for snapshots and debugging.
//
// After groveBegin() the bus belongs to the "i2c" worker on core 0: every exchange
// runs as a job there, one at a time. Callers post requests and read the latest
// result, so a multi-second camera exchange never blocks dosing, the door or the web.
#include "grove.h"
#include "../config.h"
#include "../core/log.h"
#include "../core/watchdog.h"
#include "../core/worker.h"
#include <Wire.h>
#include <Seeed_Arduino_SSCMA.h>
#include <atomic>
#include "../core/lock.h"

static SSCMA AI;
static const uint8_t GROVE = 0x62;
enum { SS_READ = 0x01, SS_WRITE = 0x02, SS_AVAIL = 0x03 };

static Worker i2c("i2c", 0, 12 * 1024);
static Mutex resultLock;
static AiResult latest;      // guarded by resultLock
static bool online = false;  // worker-owned after groveBegin()
static std::atomic<int> queued{0};

AiResult aiLatest() {
  Locked lock(resultLock);
  return latest;
}

AiStatus aiStatus() {
  Locked lock(resultLock);
  return {latest.at, latest.online};
}

bool aiBusy() { return queued > 0; }

static void publish(AiResult&& r) {  // a whole new result: frame and detections together
  Locked lock(resultLock);
  latest = std::move(r);
}

static void setOnline(bool on) {  // failed request: keep the last good frame, update the status only
  Locked lock(resultLock);
  latest.online = on;
}

static void busBegin() {
  Wire.setBufferSize(1024);  // SSCMA packets are 250 B
  Wire.begin(PIN_AI_SDA, PIN_AI_SCL, 50000);
  Wire.setTimeOut(200);  // Grove stretches the clock
}

void groveBegin() {
  // no external pull-ups on the bus, hence 50 kHz
  pinMode(PIN_AI_SDA, INPUT_PULLUP);
  pinMode(PIN_AI_SCL, INPUT_PULLUP);
  busBegin();
  online = AI.begin(&Wire, -1, GROVE, 2, 50000);
  Serial.println(online ? "Grove Vision AI: online (I2C 0x62)" : "Grove Vision AI: not responding");
  AiResult r;
  r.online = online;
  publish(std::move(r));
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
  return (Wire.read() << 8) | Wire.read();
}

// raw AT over the SSCMA transport, bypassing the library (debug)
static String rawAT(const char* q, uint32_t ms) {
  sscmaCmd(SS_WRITE, strlen(q), (const uint8_t*)q, 3);
  String r;
  unsigned long t0 = millis();
  while (millis() - t0 < ms && r.length() < 900) {
    watchdogFeed();
    int avail = sscmaAvail(3);
    if (avail <= 0 || avail > 20000) {
      delay(30);
      continue;
    }
    int n = min(avail, 200);
    sscmaCmd(SS_READ, n, NULL, 3);
    delay(3);
    Wire.requestFrom(GROVE, (uint8_t)n);
    while (Wire.available()) {
      char c = Wire.read();
      if (c >= 32 || c == '\n') r += c;
    }
  }
  return r.length() ? r : "(silence)";
}

// unstick the bus: 16 SCL clocks + STOP
static void aiRecover() {
  Wire.end();
  pinMode(PIN_AI_SDA, INPUT_PULLUP);
  pinMode(PIN_AI_SCL, INPUT_PULLUP);
  delay(1);
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
  delay(1);
  say("AI: bus recovery (before: SDA=" + String(sda) + " SCL=" + String(scl) +
      ", after: SDA=" + String(digitalRead(PIN_AI_SDA)) + ")");
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

// AT+SAMPLE=1 returns one base64 jpeg, ~15 KB.
// Takes seconds at 50 kHz, so preview only.
static String aiSample(uint32_t ms) {
  const char* q = "AT+SAMPLE=1\r\n";
  sscmaCmd(SS_WRITE, strlen(q), (const uint8_t*)q, 2);
  String r;
  r.reserve(40000);
  unsigned long t0 = millis();
  while (millis() - t0 < ms && r.length() < 60000) {
    watchdogFeed();
    int avail = sscmaAvail(2);
    if (avail <= 0 || avail > 60000) {
      if (r.indexOf("\"image\"") > 0 && r.endsWith("\n")) break;
      delay(15);
      continue;
    }
    while (avail > 0) {
      watchdogFeed();
      int n = min(avail, 250);
      sscmaCmd(SS_READ, n, NULL, 2);
      delay(2);
      Wire.requestFrom(GROVE, (uint8_t)n);
      while (Wire.available())
        r += (char)Wire.read();
      avail -= n;
    }
  }
  int k = r.indexOf("\"image\"");
  if (k < 0) return "";
  int s = r.indexOf('"', r.indexOf(':', k) + 1) + 1, e = r.indexOf('"', s);
  return e > s ? r.substring(s, e) : "";
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
    online = AI.begin(&Wire, -1, GROVE, 2, 50000);
    if (!online) {
      r.online = false;
      say("AI: not responding");
      return false;
    }
  }
  r.online = true;
  unsigned long t0 = millis();
  // invoke(n, filter, show): filter=true gives results only.
  // With the jpeg attached the reply is too big and times out.
  int rc = AI.invoke(1, true, false);
  if (rc != CMD_OK) rc = AI.invoke(1, true, false);
  if (rc != CMD_OK) {
    say("AI: invoke err " + String(rc) +
        " | raw reply: " + rawAT("AT+INVOKE=1,1,0\r\n", 3000).substring(0, 250));
    return false;
  }
  String v = "";
  r.count = AI.boxes().size();
  for (auto& b : AI.boxes())
    r.best = max(r.best, (int)b.score);
  for (auto& b : AI.boxes())
    v += "box[cls " + String(b.target) + " " + String(b.score) + "% @" + String(b.x) + "," + String(b.y) +
         "] ";
  for (auto& c : AI.classes())
    v += "class[cls " + String(c.target) + " " + String(c.score) + "%] ";
  if (!v.length()) v = "empty (nothing detected)";
  r.verdict = v;
  say("AI (" + String(millis() - t0) + "ms): " + v);
  return true;
}

// --- jobs: run on the i2c worker ---

// publish only when the frame and its detections come from the same exchange
static void finish(AiResult& r, String&& img, bool ok, unsigned long t0) {
  if (!ok || !img.length()) {
    setOnline(r.online);
    return;
  }
  r.image = std::move(img);
  r.ms = millis() - t0;
  r.at = millis();
  publish(std::move(r));
}

static void frameJob(int) {  // snapshot + inference, for the camera views
  unsigned long t0 = millis();
  AiResult r;
  String img;
  bool ok = false;
  if (aiPresent()) {
    img = aiSample(4000);
    ok = invoke(r);
  } else {
    r.online = online = false;
  }
  finish(r, std::move(img), ok, t0);
  queued--;
}

static void invokeJob(int) {  // console 'v': inference, plus its frame if Grove attached one
  unsigned long t0 = millis();
  AiResult r;
  bool ok = invoke(r);
  finish(r, AI.last_image(), ok, t0);
  queued--;
}

static void snapshotJob(int) {
  bool ok = aiPresent();
  String im = ok ? aiSample(4000) : "";
  say("AI: snapshot " + String(ok ? "" : "(not on bus) ") + String(im.length()) + " chars");
  queued--;
}

static void scanJob(int);

static void request(Worker::Job job, const char* what) {
  queued++;
  if (i2c.post(job)) return;
  queued--;
  say(String("AI: queue full, ") + what + " dropped");
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
    Wire.begin(sda, scl, 100000);
    delay(10);
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
      watchdogFeed();  // each probe can take the full 200 ms timeout on a stuck bus
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) {
        say("  SDA=" + String(sda) + " SCL=" + String(scl) + ": 0x" + String(a, HEX) +
            (a == GROVE ? "  <-- GROVE!" : ""));
        found++;
      }
    }
    if (!found) say("  SDA=" + String(sda) + " SCL=" + String(scl) + ": empty");
  }
  say("scan done");
  Wire.end();
  busBegin();

  const char* q = "AT+ID?\r\n";
  int e1 = sscmaCmd(SS_WRITE, strlen(q), (const uint8_t*)q, 3);
  delay(150);
  int e2 = sscmaCmd(SS_AVAIL, 0, NULL, 3);
  delay(3);
  Wire.requestFrom(GROVE, (uint8_t)2);
  int hi = Wire.read(), lo = Wire.read();
  int avail = (hi << 8) | lo;
  say("raw: write=" + String(e1) + " availreq=" + String(e2) + " avail=" + String(avail));
  if (avail > 0 && avail < 1000) {
    int n = min(avail, 240);
    sscmaCmd(SS_READ, n, NULL, 3);
    delay(3);
    Wire.requestFrom(GROVE, (uint8_t)n);
    String r;
    while (Wire.available()) {
      char c = Wire.read();
      if (c >= 32 || c == '\n') r += c;
    }
    say("raw reply: " + r.substring(0, 200));
  }
  queued--;
}
