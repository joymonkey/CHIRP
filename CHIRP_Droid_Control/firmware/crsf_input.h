#ifndef CRSF_INPUT_H
#define CRSF_INPUT_H

#include <Arduino.h>

void crsfInit();
void crsfUpdate();

// Callbacks for actions
void onPlayTrigger(uint8_t bankIndex);
void onStopAll();

#endif // CRSF_INPUT_H