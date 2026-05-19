#include "drive.h"
#include "globals.h"
#include "debug.h"
#include <Servo.h>

Servo leftFoot;
Servo rightFoot;

void driveInit() {
  leftFoot.attach(LEFT_FOOT_PWM_PIN, SERVO_MIN_US, SERVO_MAX_US);
  rightFoot.attach(RIGHT_FOOT_PWM_PIN, SERVO_MIN_US, SERVO_MAX_US);
  leftFoot.writeMicroseconds(SERVO_STOP_US);
  rightFoot.writeMicroseconds(SERVO_STOP_US);
}

void mixBHD(int stickX, int stickY, int &outLeft, int &outRight) {
  // Map sticks (1000-2000) to (-100 to 100)
  int x = map(stickX, RC_MIN_US, RC_MAX_US, -100, 100);
  int y = map(stickY, RC_MIN_US, RC_MAX_US, 100, -100); // Inverse Y so forward is positive

  // Deadzone
  if (abs(x) < DRIVE_DEADZONE / 10) x = 0;
  if (abs(y) < DRIVE_DEADZONE / 10) y = 0;

  // Simple Arcade Drive Mixing
  int l = y + x;
  int r = y - x;

  // Constrain to -100 to 100
  l = constrain(l, -100, 100);
  r = constrain(r, -100, 100);

  // Apply speed mode factor
  float factor = SPEED_FACTOR_SLOW;
  if (robotState.speedMode == 2) factor = SPEED_FACTOR_MED;
  else if (robotState.speedMode == 3) factor = SPEED_FACTOR_FAST;

  l = (int)(l * factor);
  r = (int)(r * factor);

  // Map back to servo microseconds
  outLeft = map(l, -100, 100, SERVO_MIN_US, SERVO_MAX_US);
  outRight = map(r, -100, 100, SERVO_MIN_US, SERVO_MAX_US);
}

void driveUpdate() {
  if (!robotState.isArmed) {
    leftFoot.writeMicroseconds(SERVO_STOP_US);
    rightFoot.writeMicroseconds(SERVO_STOP_US);
    return;
  }

  int outLeft, outRight;
  mixBHD(rcChannels[CH_ROLL], rcChannels[CH_PITCH], outLeft, outRight);

  DBG_THROTTLE(DBG_DRIVE, 500, "Motors L:%d R:%d (spd:%d)", outLeft, outRight, robotState.speedMode);

  leftFoot.writeMicroseconds(outLeft);
  rightFoot.writeMicroseconds(outRight);
}