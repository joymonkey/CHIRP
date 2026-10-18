#pragma once

#include <Arduino.h>
#include "config.h"
#include "types.h"

void storageManagerInit();
void storageManagerSave();
void storageManagerUpdateSetting(uint8_t paramId, int16_t rawVal);

