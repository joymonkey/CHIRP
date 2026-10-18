#include "dome_control.h"

extern RobotState  g_robotState;
extern RobotConfig g_robotConfig;
extern bool        lockState(TickType_t timeoutTicks);
extern void        unlockState();

// Native ESP32-S3 Hardware LEDC PWM Channel 2 for Dome Rotation
constexpr uint8_t  LEDC_CH_DOME_MOTOR  = 2;
constexpr uint32_t LEDC_PWM_FREQ_HZ    = 50;
constexpr uint8_t  LEDC_PWM_RES_BITS   = 14;
constexpr uint32_t LEDC_PERIOD_US      = 20000;
constexpr uint32_t LEDC_MAX_DUTY       = (1UL << LEDC_PWM_RES_BITS);

static char    s_radRxBuf[48];
static uint8_t s_radRxIdx = 0;

static inline void writeDomePwmUs(uint16_t pulseUs) {
    uint32_t clampedUs = constrain(pulseUs, PWM_MIN_US, PWM_MAX_US);
    uint32_t duty = (clampedUs * LEDC_MAX_DUTY) / LEDC_PERIOD_US;
    ledcWrite(LEDC_CH_DOME_MOTOR, duty);
}

static void parseRadLine(const char* line) {
    // Roam-A-Dome position reports start with #DP@, #DP!, #DP$, or #DP% followed by angle
    if (strncmp(line, "#DP@", 4) == 0 ||
        strncmp(line, "#DP!", 4) == 0 ||
        strncmp(line, "#DP$", 4) == 0 ||
        strncmp(line, "#DP%", 4) == 0) {
        int rawAngle = atoi(&line[4]);
        int adjusted = (rawAngle + g_robotConfig.domeOffset) % 360;
        if (adjusted < 0) adjusted += 360;

        if (lockState(0)) {
            g_robotState.domeHeadingDeg = (int16_t)adjusted;
            unlockState();
        }
    }
}

void domeControlInit() {
    ledcSetup(LEDC_CH_DOME_MOTOR, LEDC_PWM_FREQ_HZ, LEDC_PWM_RES_BITS);
    ledcAttachPin(PIN_PWM_DOME_MOTOR, LEDC_CH_DOME_MOTOR);
    writeDomePwmUs(PWM_NEUTRAL_US);

    // Initialize Hardware UART0 on native TX (GP43) and RX (GP44) pins
    Serial0.begin(RAD_BAUD_RATE);
    Serial0.println("#DPREPORT100");

    Serial.printf("[DOME] Dome PWM (GP%d) & Roam-A-Dome UART0 (TX=GP%d, RX=GP%d @ %lu baud) initialized\n",
                  PIN_PWM_DOME_MOTOR, PIN_RAD_TX, PIN_RAD_RX, (unsigned long)RAD_BAUD_RATE);
    Serial.flush();
}

void domeControlUpdate() {
    // 1. Non-blocking parse of incoming Roam-A-Dome position reports on Serial0
    while (Serial0.available() > 0) {
        char c = (char)Serial0.read();
        if (c == '\n' || c == '\r') {
            if (s_radRxIdx > 0) {
                s_radRxBuf[s_radRxIdx] = '\0';
                parseRadLine(s_radRxBuf);
                s_radRxIdx = 0;
            }
        } else if (s_radRxIdx < sizeof(s_radRxBuf) - 1) {
            s_radRxBuf[s_radRxIdx++] = c;
        } else {
            s_radRxIdx = 0; // Overflow protection
        }
    }

    // 2. Manual Dome Yaw Control (CH4) with Link-Loss Neutral Failsafe
    bool     linkUp = false;
    uint16_t yawUs  = PWM_NEUTRAL_US;

    if (lockState(pdMS_TO_TICKS(2))) {
        linkUp = g_robotState.rcLinkUp;
        yawUs  = g_robotState.rcChannelsUs[RC_CH_DOME_YAW];
        unlockState();
    }

    if (!linkUp) {
        writeDomePwmUs(PWM_NEUTRAL_US);
        if (lockState(0)) {
            g_robotState.domeMotorPwmUs = PWM_NEUTRAL_US;
            unlockState();
        }
        return;
    }

    int32_t delta = (int32_t)constrain(yawUs, PWM_MIN_US, PWM_MAX_US) - PWM_NEUTRAL_US;
    if (abs(delta) <= PWM_DEADBAND_US) {
        delta = 0;
    }
    if (g_robotConfig.domeInvert) {
        delta = -delta;
    }

    uint16_t outDomeUs = (uint16_t)constrain(PWM_NEUTRAL_US + delta, PWM_MIN_US, PWM_MAX_US);
    writeDomePwmUs(outDomeUs);

    if (lockState(0)) {
        g_robotState.domeMotorPwmUs = outDomeUs;
        unlockState();
    }
}
