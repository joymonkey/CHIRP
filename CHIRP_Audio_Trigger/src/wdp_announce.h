#ifndef WDP_ANNOUNCE_H
#define WDP_ANNOUNCE_H

// ===================================
// WDP Device Announce (WCB discovery)
// ===================================
// A Wireless Communication Board (WCB) running 6.2 or later records a device that
// announces itself on one of its serial ports with a line of the form
//   @WDP1 {"type":"...","fw":"..."}
// sent every 25-30 seconds (see docs/WDP_DEVICE_ANNOUNCE.md in
// greghulette/Wireless_Communication_Board-WCB). Off unless CHIRP.INI sets
// #WDP_ANNOUNCE 1.
//
// This header has no Arduino dependencies so the scheduling can be tested on a host.
// The includer must define VERSION_STRING first.

#include <stdint.h>

#define WDP_ANNOUNCE_TYPE "CHIRP Audio Trigger"
#define WDP_ANNOUNCE_LINE \
    "@WDP1 {\"type\":\"" WDP_ANNOUNCE_TYPE "\",\"fw\":\"" VERSION_STRING \
    "\",\"caps\":[\"audio.trigger\",\"audio.play\"]}"

// A WCB whose port is configured for an MP3 Trigger reads that port byte-by-byte:
// X (track finished), x (cancelled), E (error), = (status string) and M (trigger report)
// are events there. The announce line must contain none of them.
constexpr bool wdpIsMp3Safe(const char* s) {
    for (; *s; s++) {
        if (*s == 'X' || *s == 'x' || *s == 'E' || *s == '=' || *s == 'M') return false;
    }
    return true;
}

static_assert(sizeof(WDP_ANNOUNCE_TYPE) - 1 <= 24, "WDP type is limited to 24 chars");
static_assert(sizeof(VERSION_STRING) - 1 <= 27, "WDP fw is limited to 27 chars");
static_assert(wdpIsMp3Safe(WDP_ANNOUNCE_LINE), "announce line contains an MP3 Trigger event byte");

struct WdpScheduler {
    uint32_t nextMs;
    bool armed;
};

// Returns true when the caller should queue the announce line now. The first announce
// is 1.5-3 s after the first call, then every 25-30 s. It only fires when the Serial2
// queue is empty, so an announce never displaces a queued reply; a due announce just
// waits for the queue to drain. rnd() is called only when (re)scheduling.
inline bool wdpShouldAnnounce(WdpScheduler& s, bool enabled, uint32_t nowMs, int queued, uint32_t (*rnd)()) {
    if (!enabled) return false;
    if (!s.armed) {
        s.nextMs = nowMs + 1500 + rnd() % 1501;
        s.armed = true;
        return false;
    }
    if ((int32_t)(nowMs - s.nextMs) < 0) return false;  // not due yet (rollover-safe)
    if (queued > 0) return false;
    s.nextMs = nowMs + 25000 + rnd() % 5001;
    return true;
}

#endif
