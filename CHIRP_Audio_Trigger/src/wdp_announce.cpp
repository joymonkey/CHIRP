#include "config.h"
#include "wdp_announce.h"

// ===================================
// WDP Device Announce
// ===================================
// Queues WDP_ANNOUNCE_LINE for Serial2 when due. See wdp_announce.h.

static_assert(sizeof(WDP_ANNOUNCE_LINE) <= SERIAL2_MSG_MAX_LENGTH, "announce line would be truncated by the Serial2 queue");

static WdpScheduler wdpScheduler = {};

static uint32_t wdpRandom() {
    return rp2040.hwrand32();
}

void wdpAnnounceTick() {
    if (wdpShouldAnnounce(wdpScheduler, wdpAnnounce, millis(), getQueuedMessageCount(), wdpRandom)) {
        queueSerial2Message(WDP_ANNOUNCE_LINE);
    }
}
