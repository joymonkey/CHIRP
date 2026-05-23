#include "config_manager.h"
#include <LittleFS.h>
#include "debug.h"
#include "audio_link.h"

RobotConfig userConfig;

const char* CONFIG_FILE = "/config.bin";

void applyDefaults() {
  userConfig.bank1Page = 'A';
  userConfig.domeOffset = 0;
  userConfig.domeInvert = false;
  userConfig.voiceDebug = true;
  userConfig.leftMotorInvert = false;
  userConfig.rightMotorInvert = false;
  userConfig.speedSlow = 30;
  userConfig.speedMed = 70;
  userConfig.speedFast = 100;
}

void initConfigManager() {
  applyDefaults();
  
  if (!LittleFS.begin()) {
    DBG_EVENT(DBG_GENERAL, "LittleFS Mount Failed. Formatting...");
    LittleFS.format();
    if (!LittleFS.begin()) {
      DBG_EVENT(DBG_GENERAL, "LittleFS Format Failed!");
      return;
    }
  }
  
  if (LittleFS.exists(CONFIG_FILE)) {
    File file = LittleFS.open(CONFIG_FILE, "r");
    if (file) {
      if (file.readBytes((char*)&userConfig, sizeof(RobotConfig)) == sizeof(RobotConfig)) {
        DBG_EVENT(DBG_GENERAL, "Config loaded from LittleFS.");
      } else {
        DBG_EVENT(DBG_GENERAL, "Config size mismatch, using defaults.");
        applyDefaults();
      }
      file.close();
    }
  } else {
    DBG_EVENT(DBG_GENERAL, "Config file not found, creating default.");
    saveConfig();
  }
  
  // Apply initial config changes
  char bpageCmd[16];
  snprintf(bpageCmd, sizeof(bpageCmd), "BPAGE:%c", userConfig.bank1Page);
  audioSendCommand(bpageCmd);
}

void saveConfig() {
  File file = LittleFS.open(CONFIG_FILE, "w");
  if (file) {
    file.write((const uint8_t*)&userConfig, sizeof(RobotConfig));
    file.close();
    DBG_EVENT(DBG_GENERAL, "Config saved to LittleFS.");
  } else {
    DBG_EVENT(DBG_GENERAL, "Failed to save config!");
  }
}
