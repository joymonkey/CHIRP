#include "telemetry.h"
#include "globals.h"
#include "debug.h"
#include "audio_link.h"
#include <AlfredoCRSF.h>

extern AlfredoCRSF crsf;

unsigned long lastTelemetryTime = 0;
uint8_t telemetrySlot = 0;
uint8_t batteryCycle = 0;

uint8_t currentSyncBank = 1;
uint8_t currentSyncPage = 0;
uint8_t currentSyncSound = 0;

void sendBatteryTelem() {
  crsf_sensor_battery_t crsfBatt = { 0 };
  
  float voltageToSend = 0.0;
  if (batteryCycle == 0) {
    voltageToSend = robotState.battery1;
  } else if (batteryCycle == 1) {
    voltageToSend = robotState.battery2 + 100.0; // Lua expects: >=1000 => (Battery 2 * 10) + 1000 => meaning voltage + 100
  } else {
    voltageToSend = robotState.battery3 + 200.0;
  }
  
  crsfBatt.voltage = htobe16((uint16_t)(voltageToSend * 10.0));
  crsfBatt.current = htobe16((uint16_t)(robotState.currentDraw * 10.0));
  crsfBatt.capacity = htobe16((uint16_t)(robotState.peakCurrent * 10.0)) << 8; // Repurposed for peak current
  crsfBatt.remaining = 100;

  DBG_THROTTLE(DBG_TELEM, 2000, "TX Batt Telem (V:%.1f)", voltageToSend);
  crsf.queuePacket(CRSF_SYNC_BYTE, CRSF_FRAMETYPE_BATTERY_SENSOR, &crsfBatt, sizeof(crsfBatt));
  
  batteryCycle = (batteryCycle + 1) % 3;
}

void sendGpsTelem() {
  crsf_sensor_gps_t crsfGps = { 0 };
  
  // Volume as groundspeed
  crsfGps.groundspeed = htobe16((uint16_t)(robotState.volume * 10.0));
  
  // Pack status flags into satellites byte:
  //   Bits 0-1: speed mode (0=disarmed, 1=slow, 2=med, 3=fast)
  //   Bit 2: autodome enabled
  //   Bit 3: autochirp enabled
  uint8_t satPayload = robotState.isArmed ? robotState.speedMode : 0;
  if (robotState.autodomeEnabled) satPayload |= 0x04;
  if (robotState.autochirpEnabled) satPayload |= 0x08;
  crsfGps.satellites = satPayload;
  
  // Sound counts: Banks 1-2 in heading
  uint16_t count1 = manifest.banks[0].pages[robotState.currentPage].soundCount;
  uint16_t count2 = manifest.banks[1].pages[robotState.currentPage].soundCount;
  uint16_t headingPayload = (count1 & 0x1F) | ((count2 & 0x1F) << 5);
  crsfGps.heading = htobe16(headingPayload); // / 100.0 in EdgeTX, so raw int
  
  // Banks 3-4 in altitude
  uint16_t count3 = manifest.banks[2].pages[robotState.currentPage].soundCount;
  uint16_t count4 = manifest.banks[3].pages[robotState.currentPage].soundCount;
  uint16_t altPayload = (count3 & 0x1F) | ((count4 & 0x1F) << 5);
  crsfGps.altitude = htobe16(altPayload + 1000); // Core sends payload + 1000

  DBG_THROTTLE(DBG_TELEM, 2000, "TX GPS Telem (Vol:%d Spd:%d Hdg:%d Alt:%d)", robotState.volume, crsfGps.satellites, headingPayload, altPayload);
  crsf.queuePacket(CRSF_SYNC_BYTE, CRSF_FRAMETYPE_GPS, &crsfGps, sizeof(crsfGps));
}

void sendFlightModeTelem() {
  // Format: "1AaName"
  char fmString[20] = {0};
  
  char pageChar = 'A' + currentSyncPage;
  char soundChar;
  if (currentSyncSound == 0) soundChar = '0';
  else if (currentSyncSound <= 9) soundChar = '0' + currentSyncSound;
  else soundChar = 'a' + (currentSyncSound - 10);
  
  const char* name = (currentSyncSound == 0) ? 
                     getPageName(currentSyncBank, currentSyncPage) : 
                     getSoundName(currentSyncBank, currentSyncPage, currentSyncSound);
                     
  snprintf(fmString, sizeof(fmString), "%d%c%c%.11s", currentSyncBank, pageChar, soundChar, name);
  
  // Send via AlfredoCRSF queuePacket
  uint8_t len = strlen(fmString) + 1; // +1 for null terminator
  DBG_THROTTLE(DBG_TELEM, 2000, "TX FM Telem: %s", fmString);
  crsf.queuePacket(CRSF_SYNC_BYTE, CRSF_FRAMETYPE_FLIGHT_MODE, fmString, len);
  
  // Cycle to next sound
  currentSyncSound++;
  if (currentSyncSound > manifest.banks[currentSyncBank-1].pages[currentSyncPage].soundCount) {
    currentSyncSound = 0;
    currentSyncPage++;
    if (currentSyncPage >= MAX_PAGES) {
      currentSyncPage = 0;
      currentSyncBank++;
      if (currentSyncBank > MAX_BANKS) {
        currentSyncBank = 1;
      }
    }
  }
}

void telemetryInit() {
  lastTelemetryTime = millis();
}

void telemetryUpdate() {
  if (millis() - lastTelemetryTime >= TELEMETRY_RATE_MS) {
    lastTelemetryTime = millis();
    
    if (telemetrySlot == 0) {
      sendBatteryTelem();
    } else if (telemetrySlot == 1) {
      sendGpsTelem();
    } else if (telemetrySlot == 2) {
      sendFlightModeTelem();
    }
    
    telemetrySlot = (telemetrySlot + 1) % 3;
  }
}