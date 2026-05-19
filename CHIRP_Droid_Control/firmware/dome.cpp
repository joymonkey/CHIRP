#include "dome.h"
#include "globals.h"
#include "debug.h"
#include <Servo.h>
#include <SerialPIO.h>

Servo domeServo;
SerialPIO SerialRAD(RAD_TX_PIN, RAD_RX_PIN);

unsigned long lastAutodomeTime = 0;
unsigned long autodomeInterval = AUTODOME_MAX_INTERVAL_MS;

void domeInit() {
  domeServo.attach(DOME_PWM_PIN, SERVO_MIN_US, SERVO_MAX_US);
  domeServo.writeMicroseconds(SERVO_STOP_US);

  SerialRAD.begin(RAD_BAUD);
  // Request position reporting every 100ms
  SerialRAD.println("#DPREPORT100");
}

void domeUpdate() {
  unsigned long now = millis();

  // Read manual control
  int yawStick = rcChannels[CH_YAW];
  static bool isManualOverride = false;

  if (abs(yawStick - 1500) > DOME_DEADZONE) {
    if (!isManualOverride) {
      DBG_EVENT(DBG_DOME, "Manual Override Started");
    }
    isManualOverride = true;
    domeServo.writeMicroseconds(yawStick);
    lastAutodomeTime = now; // Delay autodome while manually controlling
  } else {
    if (isManualOverride) {
      DBG_EVENT(DBG_DOME, "Manual Override Ended");
    }
    isManualOverride = false;
    domeServo.writeMicroseconds(SERVO_STOP_US);
  }

  // Parse Roam-A-Dome feedback
  while (SerialRAD.available() > 0) {
    String line = SerialRAD.readStringUntil('\n');
    line.trim();
    if (line.startsWith("#DP@") || line.startsWith("#DP!") || line.startsWith("#DP$") || line.startsWith("#DP%")) {
      // Extract position (e.g. #DP@360)
      robotState.domeAngle = line.substring(4).toInt();
      DBG_THROTTLE(DBG_DOME, 1000, "RAD Position: %d", robotState.domeAngle);
    }
  }

  // Autodome logic
  if (robotState.autodomeEnabled && !isManualOverride) {
    // Map CH_AUTODOME_FREQ (1000-2000) to interval (disable if lowest)
    int freqStick = rcChannels[CH_AUTODOME_FREQ];
    
    if (freqStick > 1050) {
      // Map 1050 -> 30000ms, 2000 -> 5000ms
      autodomeInterval = map(freqStick, 1050, 2000, AUTODOME_MAX_INTERVAL_MS, AUTODOME_MIN_INTERVAL_MS);
      
      if (now - lastAutodomeTime >= autodomeInterval) {
        lastAutodomeTime = now;
        
        // Random relative rotation
        int randomAngle = random(-90, 90);
        DBG_EVENT(DBG_DOME, "Autodome Move: %d° (Int: %lu ms)", randomAngle, autodomeInterval);
        SerialRAD.print(":DPDR");
        SerialRAD.println(randomAngle);
      }
    }
  } else {
    lastAutodomeTime = now; // Keep timer reset while disabled or in manual override
  }
}