#pragma once

#include <Arduino.h>
#include "config.h"
#include "types.h"

void domeLightingInit();
void domeLightingSendCommand(const char* marcduinoCmd);
void domeLightingUpdate();

