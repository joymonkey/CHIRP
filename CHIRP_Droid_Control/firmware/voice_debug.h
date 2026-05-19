#ifndef VOICE_DEBUG_H
#define VOICE_DEBUG_H

#include <Arduino.h>
#include "config.h"

// Convenience macros — compile away to nothing when disabled
#ifdef DEBUG_VOICE_ENABLED
  #define VSAY(...)    voiceDebugSay(__VA_ARGS__)
  #define VSAY_P(p, ...) voiceDebugSayPriority(p, __VA_ARGS__)
#else
  #define VSAY(...)    do {} while(0)
  #define VSAY_P(p, ...) do {} while(0)
#endif

void voiceDebugInit();
void voiceDebugUpdate();  // Call every loop()

// Queue a phrase (variable number of words, terminated by nullptr)
void voiceDebugSay(const char* word1, ...);

// Queue with explicit priority (0=highest)
void voiceDebugSayPriority(uint8_t priority, const char* word1, ...);

extern bool voiceAckReceived;

#endif
