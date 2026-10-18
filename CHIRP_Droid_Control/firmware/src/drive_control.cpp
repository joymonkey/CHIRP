#include "drive_control.h"

extern RobotState  g_robotState;
extern RobotConfig g_robotConfig;
extern bool        lockState(TickType_t timeoutTicks);
extern void        unlockState();

// Native ESP32-S3 Hardware LEDC PWM Configuration (50Hz, 14-bit resolution = 16384 ticks per 20,000us)
constexpr uint8_t  LEDC_CH_FOOT_LEFT   = 0;
constexpr uint8_t  LEDC_CH_FOOT_RIGHT  = 1;
constexpr uint32_t LEDC_PWM_FREQ_HZ    = 50;
constexpr uint8_t  LEDC_PWM_RES_BITS   = 14;
constexpr uint32_t LEDC_PERIOD_US      = 20000;
constexpr uint32_t LEDC_MAX_DUTY       = (1UL << LEDC_PWM_RES_BITS); // 16384

static inline void writePwmUs(uint8_t ledcChannel, uint16_t pulseUs) {
    uint32_t clampedUs = constrain(pulseUs, PWM_MIN_US, PWM_MAX_US);
    uint32_t duty = (clampedUs * LEDC_MAX_DUTY) / LEDC_PERIOD_US;
    ledcWrite(ledcChannel, duty);
}

void driveControlInit() {
    // Configure native ESP32-S3 LEDC channels 0 and 1 at 50Hz, 14-bit resolution
    ledcSetup(LEDC_CH_FOOT_LEFT,  LEDC_PWM_FREQ_HZ, LEDC_PWM_RES_BITS);
    ledcSetup(LEDC_CH_FOOT_RIGHT, LEDC_PWM_FREQ_HZ, LEDC_PWM_RES_BITS);

    ledcAttachPin(PIN_PWM_FOOT_LEFT,  LEDC_CH_FOOT_LEFT);
    ledcAttachPin(PIN_PWM_FOOT_RIGHT, LEDC_CH_FOOT_RIGHT);

    // Boot strictly locked at 1500us neutral
    writePwmUs(LEDC_CH_FOOT_LEFT,  PWM_NEUTRAL_US);
    writePwmUs(LEDC_CH_FOOT_RIGHT, PWM_NEUTRAL_US);

    Serial.printf("[DRV] Foot Drive PWMs initialized (Left=GP%d, Right=GP%d @ 50Hz LEDC, 1500us neutral)\n",
                  PIN_PWM_FOOT_LEFT, PIN_PWM_FOOT_RIGHT);
    Serial.flush();
}

void driveControlUpdate() {
    bool      armed       = false;
    bool      linkUp      = false;
    uint16_t  steerUs     = PWM_NEUTRAL_US;
    uint16_t  throttleUs  = PWM_NEUTRAL_US;
    SpeedMode speedMode   = SpeedMode::SLOW;

    if (lockState(pdMS_TO_TICKS(2))) {
        linkUp     = g_robotState.rcLinkUp;
        armed      = g_robotState.isArmed;
        steerUs    = g_robotState.rcChannelsUs[RC_CH_DRIVE_STEER];
        throttleUs = g_robotState.rcChannelsUs[RC_CH_DRIVE_THROTTLE];
        speedMode  = g_robotState.speedMode;
        unlockState();
    }

    // Zero-latency safety check: force 1500us neutral whenever disarmed or link is down
    if (!linkUp || !armed) {
        writePwmUs(LEDC_CH_FOOT_LEFT,  PWM_NEUTRAL_US);
        writePwmUs(LEDC_CH_FOOT_RIGHT, PWM_NEUTRAL_US);

        if (lockState(0)) {
            g_robotState.leftFootPwmUs  = PWM_NEUTRAL_US;
            g_robotState.rightFootPwmUs = PWM_NEUTRAL_US;
            unlockState();
        }
        return;
    }

    // Center inputs around 0 (-500..+500)
    int32_t steer    = (int32_t)constrain(steerUs, PWM_MIN_US, PWM_MAX_US) - PWM_NEUTRAL_US;
    int32_t throttle = (int32_t)constrain(throttleUs, PWM_MIN_US, PWM_MAX_US) - PWM_NEUTRAL_US;

    // Apply center deadband
    if (abs(steer) <= PWM_DEADBAND_US)    steer = 0;
    if (abs(throttle) <= PWM_DEADBAND_US) throttle = 0;

    // Differential Arcade Mixing
    int32_t leftMix  = constrain(throttle + steer, -500, 500);
    int32_t rightMix = constrain(throttle - steer, -500, 500);

    // Apply Speed Mode scaling
    uint8_t scalePct = g_robotConfig.speedSlowPct;
    if (speedMode == SpeedMode::MEDIUM)    scalePct = g_robotConfig.speedMedPct;
    else if (speedMode == SpeedMode::FAST) scalePct = g_robotConfig.speedFastPct;

    leftMix  = (leftMix  * (int32_t)scalePct) / 100;
    rightMix = (rightMix * (int32_t)scalePct) / 100;

    // Optional Motor Direction Inversion
    if (g_robotConfig.leftMotorInvert)  leftMix  = -leftMix;
    if (g_robotConfig.rightMotorInvert) rightMix = -rightMix;

    uint16_t outLeftUs  = (uint16_t)constrain(PWM_NEUTRAL_US + leftMix,  PWM_MIN_US, PWM_MAX_US);
    uint16_t outRightUs = (uint16_t)constrain(PWM_NEUTRAL_US + rightMix, PWM_MIN_US, PWM_MAX_US);

    writePwmUs(LEDC_CH_FOOT_LEFT,  outLeftUs);
    writePwmUs(LEDC_CH_FOOT_RIGHT, outRightUs);

    if (lockState(0)) {
        g_robotState.leftFootPwmUs  = outLeftUs;
        g_robotState.rightFootPwmUs = outRightUs;
        unlockState();
    }
}
