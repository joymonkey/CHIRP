#include "debug.h"
#include "globals.h"

#ifdef DEBUG_ENABLED

const char* getCategoryName(uint8_t category) {
  if (category & DBG_GENERAL) return "GENERAL";
  if (category & DBG_CRSF) return "CRSF";
  if (category & DBG_AUDIO) return "AUDIO";
  if (category & DBG_DRIVE) return "DRIVE";
  if (category & DBG_DOME) return "DOME";
  if (category & DBG_TELEM) return "TELEM";
  return "UNKNOWN";
}

void debugInit() {
  Serial.begin(DEBUG_BAUD);
  uint32_t startWait = millis();
  while (!Serial && millis() - startWait < 2000) {
    // Wait up to 2 seconds for USB serial to connect
  }
  
  Serial.println("\n[0ms] [GENERAL] === CHIRP2 v1.0 ===");
  Serial.flush();
  Serial.println("[0ms] [GENERAL] Board: RP2350 (Pico 2 W)");
  Serial.flush();
  Serial.println("[0ms] [GENERAL] Debug: Categories enabled");
  Serial.flush();
}

void debugPrint(uint8_t category, const char* fmt, ...) {
  if (!(DEBUG_CATEGORIES & category)) return;

  char buf[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  Serial.printf("[%lu ms] [%s] %s\n", millis(), getCategoryName(category), buf);
}

void debugPrintThrottled(uint8_t category, uint32_t* lastTime, uint32_t intervalMs, const char* fmt, ...) {
  if (!(DEBUG_CATEGORIES & category)) return;

  uint32_t now = millis();
  if (now - *lastTime >= intervalMs) {
    *lastTime = now;
    
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    Serial.printf("[%lu ms] [%s] %s\n", now, getCategoryName(category), buf);
  }
}

void debugPrintStatus() {
  if (!(DEBUG_CATEGORIES & DBG_GENERAL)) return;
  
  const char* syncStateStr = "UNKNOWN";
  if (audioSyncState == SYNC_IDLE) syncStateStr = "IDLE";
  else if (audioSyncState == SYNC_REQUESTING) syncStateStr = "REQUESTING";
  else if (audioSyncState == SYNC_WAITING) syncStateStr = "WAITING";
  else if (audioSyncState == SYNC_COMPLETE) syncStateStr = "COMPLETE";
  
  Serial.printf("[%lu ms] [STATUS] LINK:%s ARM:%c SPD:%d VOL:%d B1:%.1fv B2:%.1fv DOME:%d PG:%c ADome:%d AChirp:%d SYNC:%s\n",
                millis(),
                robotState.rcLinkUp ? "UP" : "DOWN",
                robotState.isArmed ? 'Y' : 'N',
                robotState.speedMode,
                robotState.volume,
                robotState.battery1,
                robotState.battery2,
                robotState.domeAngle,
                'A' + robotState.currentPage,
                robotState.autodomeEnabled ? 1 : 0,
                robotState.autochirpEnabled ? 1 : 0,
                syncStateStr);
}

void debugPrintChannels() {
  if (!(DEBUG_CATEGORIES & DBG_CRSF)) return;

  Serial.printf("[%lu ms] [CRSF] CH 1-8:  %d %d %d %d %d %d %d %d\n",
                millis(),
                rcChannels[1], rcChannels[2], rcChannels[3], rcChannels[4],
                rcChannels[5], rcChannels[6], rcChannels[7], rcChannels[8]);
  Serial.printf("[%lu ms] [CRSF] CH 9-16: %d %d %d %d %d %d %d %d\n",
                millis(),
                rcChannels[9], rcChannels[10], rcChannels[11], rcChannels[12],
                rcChannels[13], rcChannels[14], rcChannels[15], rcChannels[16]);
}

#endif // DEBUG_ENABLED