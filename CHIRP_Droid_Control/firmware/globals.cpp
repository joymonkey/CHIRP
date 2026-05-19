#include "globals.h"

RobotState robotState;
AudioManifest manifest;
SyncState audioSyncState = SYNC_IDLE;

uint16_t rcChannels[17];

void initGlobals() {
  robotState.battery1 = 0.0;
  robotState.battery2 = 0.0;
  robotState.battery3 = 0.0;
  robotState.currentDraw = 0.0;
  robotState.peakCurrent = 0.0;
  
  robotState.rcLinkUp = false;
  robotState.isArmed = false;
  robotState.speedMode = 1;
  robotState.volume = 50;
  
  robotState.autodomeEnabled = false;
  robotState.autochirpEnabled = false;
  robotState.currentPage = 0; // A
  robotState.domeAngle = 0;

  for (int i = 0; i < 17; i++) {
    rcChannels[i] = 1500; // Center values
  }
}