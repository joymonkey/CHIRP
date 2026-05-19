#include <Arduino.h>
#include "config.h"
#include "globals.h"
#include "debug.h"
#include "voice_debug.h"
#include "crsf_input.h"
#include "drive.h"
#include "dome.h"
#include "audio_link.h"
#include "telemetry.h"

unsigned long lastVbatTime = 0;

void setup() {
  debugInit();
  DBG(DBG_GENERAL, "CHIRP2 Booting...");

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH);

  initGlobals();
  DBG(DBG_GENERAL, "✓ Globals initialized");
  
  crsfInit();
  DBG(DBG_GENERAL, "✓ CRSF initialized (UART0 @ %d baud)", CRSF_BAUD);
  driveInit();
  DBG(DBG_GENERAL, "✓ Drive initialized (L:GP%d R:GP%d)", LEFT_FOOT_PWM_PIN, RIGHT_FOOT_PWM_PIN);
  domeInit();
  DBG(DBG_GENERAL, "✓ Dome initialized (PWM:GP%d RAD:GP%d/GP%d)", DOME_PWM_PIN, RAD_TX_PIN, RAD_RX_PIN);
  audioInit();
  DBG(DBG_GENERAL, "✓ Audio initialized (UART1 @ %d baud)", AUDIO_BAUD);
  voiceDebugInit();
  VSAY("chirp", "control", "ready", nullptr);
  telemetryInit();
  DBG(DBG_GENERAL, "✓ Telemetry initialized (%dms cycle)", TELEMETRY_RATE_MS);

  DBG(DBG_GENERAL, "=== CHIRP2 Ready ===");
  digitalWrite(STATUS_LED_PIN, LOW);
}

void loop() {
  crsfUpdate();
  driveUpdate();
  domeUpdate();
  audioUpdate();
  telemetryUpdate();
  voiceDebugUpdate();
  
  // Read VBAT occasionally
  if (millis() - lastVbatTime >= VBAT_READ_INTERVAL_MS) {
    lastVbatTime = millis();
    
    // Read ADCs
    int adc1 = analogRead(VBAT_PIN);
    int adc2 = analogRead(VBAT2_PIN);
    int adc3 = analogRead(VBAT3_PIN);
    
    // Dummy conversions for now
    robotState.battery1 = (adc1 / 4095.0) * 3.3 * 10.0;
    robotState.battery2 = (adc2 / 4095.0) * 3.3 * 10.0;
    robotState.battery3 = (adc3 / 4095.0) * 3.3 * 10.0;
    
    static bool batt1WarnSent = false;
    if (robotState.battery1 < 11.0 && robotState.battery1 > 1.0 && !batt1WarnSent) {
      VSAY_P(0, "warning", "primary", "battery", "low", nullptr);
      batt1WarnSent = true;
    }
    if (robotState.battery1 >= 11.5) batt1WarnSent = false; // Reset with hysteresis
  }

  // Periodic debug dumps (every DEBUG_STATUS_INTERVAL_MS)
  static uint32_t lastStatusTime = 0;
  if (millis() - lastStatusTime >= DEBUG_STATUS_INTERVAL_MS) {
    lastStatusTime = millis();
    debugPrintStatus();
    debugPrintChannels();
  }

  // Heartbeat LED (lub-dub pattern)
  static uint32_t hbTimer = 0;
  uint32_t hbPhase = (millis() - hbTimer) % 1200;
  if (hbPhase < 80 || (hbPhase >= 200 && hbPhase < 280)) {
    digitalWrite(LED_BUILTIN, HIGH);  // Two quick flashes
  } else {
    digitalWrite(LED_BUILTIN, LOW);   // Dark between and after
  }
}

// Callbacks from CRSF
void onPlayTrigger(uint8_t bankIndex) {
  DBG_EVENT(DBG_AUDIO, "Play Trigger: Bank %d", bankIndex);
  
  // Determine correct page based on current selection
  uint8_t pageIndex = robotState.currentPage;
  
  // Bank 1 is fixed to Page A according to spec
  if (bankIndex == 1) {
    pageIndex = 0; // Page A
  }
  
  // Send play command — use same floor-division as Lua: floor(norm * count) + 1
  uint16_t currentKnob = rcChannels[CH_SOUND_INDEX];
  uint8_t maxIdx = manifest.banks[bankIndex-1].pages[pageIndex].soundCount;
  
  if (maxIdx > 0) {
    long knobOffset = constrain((long)currentKnob - RC_MIN_US, 0, RC_MAX_US - RC_MIN_US);
    uint8_t sndIdx = (uint8_t)((knobOffset * maxIdx) / (RC_MAX_US - RC_MIN_US)) + 1;
    sndIdx = constrain(sndIdx, 1, maxIdx);
    audioPlaySound(bankIndex, pageIndex, sndIdx, robotState.volume);
  }
}

void onStopAll() {
  DBG_EVENT(DBG_AUDIO, "Stop All Triggered");
  audioStopAll();
}