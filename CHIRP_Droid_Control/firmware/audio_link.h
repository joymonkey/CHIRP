#ifndef AUDIO_LINK_H
#define AUDIO_LINK_H

#include <Arduino.h>

void audioInit();
void audioUpdate();
void audioPlaySound(uint8_t bankIndex, uint8_t pageIndex, uint8_t soundIndex, uint8_t volume);
void audioSetVolume(uint8_t volume);
void audioStopAll();
void audioSendCommand(const char* command);

// Returns the name of the sound, or a placeholder
const char* getSoundName(uint8_t bank, uint8_t page, uint8_t index);
const char* getPageName(uint8_t bank, uint8_t page);

#endif // AUDIO_LINK_H