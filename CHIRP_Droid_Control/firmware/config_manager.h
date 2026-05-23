#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>

struct RobotConfig {
  char bank1Page;
  int16_t domeOffset;
  bool domeInvert;
  bool voiceDebug;
  bool leftMotorInvert;
  bool rightMotorInvert;
  uint8_t speedSlow;
  uint8_t speedMed;
  uint8_t speedFast;
};

extern RobotConfig userConfig;

void initConfigManager();
void saveConfig();

#endif // CONFIG_MANAGER_H
