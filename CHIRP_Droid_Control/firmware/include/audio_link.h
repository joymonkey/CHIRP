#pragma once

#include <Arduino.h>
#include "config.h"
#include "types.h"

extern AudioManifest  g_audioManifest;
extern AudioSyncState g_audioSyncState;

void audioLinkInit();
void audioLinkUpdate();

// Direct CHIRP_Audio Commands
void audioRequestManifestSync();
void audioPlaySound(uint8_t bank, char pageChar, uint8_t track, uint8_t volume);
void audioStopAll();
void audioSetVolume(uint8_t volume);
void audioSetBank1Page(char pageChar);
void audioSetMaxStreams(uint8_t maxStreams);
void audioSendRawCommand(const char* cmd);

// Manifest Lookup Helpers (1-indexed bank 1..4, 0-indexed page 0..2, 1-indexed track 1..24)
uint8_t     audioGetSoundCount(uint8_t bank, uint8_t pageIdx);
const char* audioGetPageName(uint8_t bank, uint8_t pageIdx);
const char* audioGetSoundName(uint8_t bank, uint8_t pageIdx, uint8_t track);

// Vocal Status Feedback (PVOICE)
void audioSayPriority(uint8_t priority, const char* word1, ...);
void audioDispatchVoiceTag(uint8_t priority, const char* tag);

