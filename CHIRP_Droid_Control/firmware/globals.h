#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include "config.h"

extern RobotState robotState;
extern AudioManifest manifest;
extern SyncState audioSyncState;

extern uint16_t rcChannels[17];

// UARTs
// Use RP2040 predefined Serial1 and Serial2
#define SerialCRSF Serial1
#define SerialAudio Serial2

// Initialize globals
void initGlobals();

#endif // GLOBALS_H