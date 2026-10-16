#include "report.h"
#include <WiFi.h>
#include <errno.h>
#include <lwip/sockets.h>
#include "../clock.h"
#include "../config.h"
#include "../core/watchdog.h"

uint16_t boardFaults(const Status& s) {
  uint16_t f = s.faults;
  if (WiFi.status() != WL_CONNECTED) f = f | Fault::WifiDown;
  return f;
}

void bitNames(uint16_t bits, const char* (*name)(int), char* out, size_t cap) {
  size_t n = 0;
  out[0] = 0;
  for (int i = 0; i < 16 && n < cap; i++) {
    if (!(bits & (1u << i))) continue;
    const char* nm = name(i);
    int w = nm ? snprintf(out + n, cap - n, "%s%s", n ? "," : "", nm)
               : snprintf(out + n, cap - n, "%sbit%d", n ? "," : "", i);
    if (w < 0) break;
    n += (size_t)w;
  }
  if (!out[0]) strlcpy(out, "-", cap);
}

bool sockSend(int fd, const char* data, size_t len, uint32_t deadline) {
  while (len) {
    if (fd < 0 || due(millis(), deadline)) return false;
    ssize_t w = send(fd, data, len > TCP_MSS ? TCP_MSS : len, MSG_DONTWAIT);  // a segment at a time
    if (w > 0) {
      data += w;
      len -= (size_t)w;
      watchdogFeed();
      continue;
    }
    if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return false;
    fd_set set;  // the socket buffer is full: wait a little for the peer to take some
    FD_ZERO(&set);
    FD_SET(fd, &set);
    timeval tv = {0, (suseconds_t)(SOCK_WAIT_MS * 1000)};
    select(fd + 1, nullptr, &set, nullptr, &tv);
    watchdogFeed();
  }
  return true;
}
