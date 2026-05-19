#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>
#include "config.h"

// Category Bitmasks
#define DBG_GENERAL  0x01
#define DBG_CRSF     0x02
#define DBG_AUDIO    0x04
#define DBG_DRIVE    0x08
#define DBG_DOME     0x10
#define DBG_TELEM    0x20
#define DBG_ALL      0xFF

#ifdef DEBUG_ENABLED

void debugInit();
void debugPrint(uint8_t category, const char* fmt, ...);
void debugPrintThrottled(uint8_t category, uint32_t* lastTime, uint32_t intervalMs, const char* fmt, ...);
void debugPrintStatus();
void debugPrintChannels();

#define DBG(cat, fmt, ...) debugPrint(cat, fmt, ##__VA_ARGS__)
#define DBG_EVENT(cat, fmt, ...) debugPrint(cat, fmt, ##__VA_ARGS__)

#define DBG_THROTTLE(cat, ms, fmt, ...) \
  do { \
    static uint32_t lastPrintTime = 0; \
    if (ms == 0) { \
        debugPrintThrottled(cat, &lastPrintTime, 0, fmt, ##__VA_ARGS__); \
    } else { \
        debugPrintThrottled(cat, &lastPrintTime, ms, fmt, ##__VA_ARGS__); \
    } \
  } while (0)

#else

#define debugInit() do {} while(0)
#define debugPrintStatus() do {} while(0)
#define debugPrintChannels() do {} while(0)

#define DBG(cat, fmt, ...) do {} while(0)
#define DBG_EVENT(cat, fmt, ...) do {} while(0)
#define DBG_THROTTLE(cat, ms, fmt, ...) do {} while(0)

#endif // DEBUG_ENABLED

#endif // DEBUG_H