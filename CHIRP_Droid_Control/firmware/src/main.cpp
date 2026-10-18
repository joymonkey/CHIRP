#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include "types.h"
#include "crsf_link.h"
#include "drive_control.h"
#include "dome_control.h"
#include "dome_lighting.h"
#include "storage_manager.h"
#include "audio_link.h"
#include "telemetry.h"

// ============================================================================
// Global Shared State & Synchronization Primitives
// ============================================================================
RobotState        g_robotState;
RobotConfig       g_robotConfig;
SemaphoreHandle_t g_stateMutex   = nullptr;
QueueHandle_t     g_commandQueue = nullptr;

// Task Handles
TaskHandle_t      g_controlTaskHandle = nullptr;
TaskHandle_t      g_commsTaskHandle   = nullptr;

// Onboard WS2812B RGB Status Pixel (Waveshare ESP32-S3-Zero uses NEO_GRB wire order)
Adafruit_NeoPixel g_statusPixel(STATUS_NEOPIXEL_CNT, PIN_STATUS_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// ============================================================================
// Thread-Safe State Helpers
// ============================================================================
bool lockState(TickType_t timeoutTicks = pdMS_TO_TICKS(5)) {
    if (g_stateMutex == nullptr) return false;
    return (xSemaphoreTake(g_stateMutex, timeoutTicks) == pdTRUE);
}

void unlockState() {
    if (g_stateMutex != nullptr) {
        xSemaphoreGive(g_stateMutex);
    }
}

// ============================================================================
// Core 1 Task: Real-Time Deterministic Control & Safety Failsafe (200Hz)
// ============================================================================
void taskRealTimeControl(void* pvParameters) {
    (void)pvParameters;

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t periodTicks = pdMS_TO_TICKS(CONTROL_LOOP_PERIOD_MS);

    uint32_t loopCounter  = 0;
    uint32_t lastHzCalcMs = millis();

    for (;;) {
        uint32_t nowMs = millis();
        loopCounter++;

        // 1. Poll CRSF Receiver on Hardware UART1 (GP4/GP5 @ 420kbaud)
        g_crsfLink.update();

        // 2. Update Arcade Foot Drive Mixer & Safety Failsafe (GP6/GP7)
        driveControlUpdate();

        // 3. Update Dome Rotation PWM (GP11) & Roam-A-Dome UART0 (GP43/GP44)
        domeControlUpdate();

        // 4. Service Dome Lighting SoftSerial (GP12/GP13)
        domeLightingUpdate();

        // Update Core 1 loop frequency diagnostic once per second
        if (nowMs - lastHzCalcMs >= 1000) {
            if (lockState(0)) {
                g_robotState.core1LoopHz = loopCounter;
                unlockState();
            }
            loopCounter  = 0;
            lastHzCalcMs = nowMs;
        }

        vTaskDelayUntil(&lastWakeTime, periodTicks);
    }
}

// ============================================================================
// Core 0 Task: Communications, Audio Link, Telemetry & Status LED (100Hz)
// ============================================================================
void taskCommunications(void* pvParameters) {
    (void)pvParameters;

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t periodTicks = pdMS_TO_TICKS(COMMS_LOOP_PERIOD_MS);

    uint32_t loopCounter     = 0;
    uint32_t lastHzCalcMs    = millis();
    uint32_t lastHeartbeatMs = millis();
    bool     heartbeatPhase  = false;

    for (;;) {
        uint32_t nowMs = millis();
        loopCounter++;

        // 1. Drain any pending commands from Core 1
        SystemCommand cmd;
        while (xQueueReceive(g_commandQueue, &cmd, 0) == pdTRUE) {
            switch (cmd.type) {
                case CommandType::PLAY_SOUND: {
                    uint8_t vol = 50;
                    if (lockState(pdMS_TO_TICKS(2))) {
                        vol = g_robotState.masterVolume;
                        unlockState();
                    }
                    audioPlaySound(cmd.bank, cmd.page, cmd.track, vol);
                    break;
                }

                case CommandType::STOP_AUDIO:
                    audioStopAll();
                    break;

                case CommandType::SET_VOLUME:
                    audioSetVolume((uint8_t)constrain(cmd.paramValue, 0, 99));
                    break;

                case CommandType::PLAY_VOICE_PROMPT:
                    audioDispatchVoiceTag(cmd.paramId, cmd.text);
                    break;

                case CommandType::SET_ACTIVE_PAGE: {
                    static const char* const PAGE_WORDS[6] = { "_a", "_b", "_c", "_d", "_e", "_f" };
                    uint8_t idx = (cmd.page >= 'A' && cmd.page <= 'F') ? (uint8_t)(cmd.page - 'A') : 0;
                    audioSayPriority(2, "page", PAGE_WORDS[idx], nullptr);
                    break;
                }

                case CommandType::SYNC_MANIFEST:
                    audioRequestManifestSync();
                    break;

                case CommandType::DOME_LIGHT_CMD:
                    domeLightingSendCommand(cmd.text);
                    break;

                case CommandType::UPDATE_SETTING:
                    storageManagerUpdateSetting(cmd.paramId, cmd.paramValue);
                    break;

                default:
                    break;
            }
        }

        // 2. Service CHIRP_Audio UART2 (Manifest Sync & PVOICE Queue)
        audioLinkUpdate();

        // 3. Service ADC1 Battery Monitor & CRSF Telemetry Uplink
        telemetryUpdate();

        // 4. Heartbeat Status LED (500ms pulse)
        //    - Link DOWN:         Teal / Dim Blue
        //    - Link UP, Disarmed: Amber / Dim Blue
        //    - Link UP, Armed:    Green / Dim Green
        if (nowMs - lastHeartbeatMs >= HEARTBEAT_PERIOD_MS) {
            lastHeartbeatMs = nowMs;
            heartbeatPhase  = !heartbeatPhase;

            bool linkUp = false;
            bool armed  = false;
            if (lockState(0)) {
                linkUp = g_robotState.rcLinkUp;
                armed  = g_robotState.isArmed;
                unlockState();
            }

            if (!linkUp) {
                if (heartbeatPhase) {
                    g_statusPixel.setPixelColor(0, g_statusPixel.Color(0, 180, 220)); // Teal
                } else {
                    g_statusPixel.setPixelColor(0, g_statusPixel.Color(0, 10, 40));   // Dim Blue
                }
            } else if (!armed) {
                if (heartbeatPhase) {
                    g_statusPixel.setPixelColor(0, g_statusPixel.Color(255, 140, 0)); // Amber (Connected, Safe/Disarmed)
                } else {
                    g_statusPixel.setPixelColor(0, g_statusPixel.Color(0, 15, 45));   // Dim Blue
                }
            } else {
                if (heartbeatPhase) {
                    g_statusPixel.setPixelColor(0, g_statusPixel.Color(0, 230, 40));  // Bright Green (Armed)
                } else {
                    g_statusPixel.setPixelColor(0, g_statusPixel.Color(0, 30, 5));    // Dim Green
                }
            }
            g_statusPixel.show();
        }

        // 5. Update Core 0 loop frequency diagnostic once per second
        if (nowMs - lastHzCalcMs >= 1000) {
            if (lockState(pdMS_TO_TICKS(2))) {
                g_robotState.core0LoopHz = loopCounter;
                unlockState();
            }
            loopCounter  = 0;
            lastHzCalcMs = nowMs;
        }

        vTaskDelayUntil(&lastWakeTime, periodTicks);
    }
}

// Non-intrusive 40ms boot check on PIN_CRSF_RX (GP5) before starting UART1
static void checkCrsfRxLineAtBoot() {
    pinMode(PIN_CRSF_RX, INPUT_PULLDOWN);
    delayMicroseconds(300);
    uint8_t pd = (uint8_t)digitalRead(PIN_CRSF_RX);

    pinMode(PIN_CRSF_RX, INPUT_PULLUP);
    delayMicroseconds(300);
    uint8_t pu = (uint8_t)digitalRead(PIN_CRSF_RX);
    uint8_t last = pu;
    uint32_t edges = 0;

    uint32_t startMs = millis();
    while (millis() - startMs < 40) {
        uint8_t s = (uint8_t)digitalRead(PIN_CRSF_RX);
        if (s != last) {
            edges++;
            last = s;
        }
    }

    const char* status = "FLOAT (Unconnected)";
    if (edges > 6)               status = "PULSING (Active CRSF Stream)";
    else if (pd == 1 && pu == 1) status = "HIGH (3.3V UART Idle - Waiting for RF/Model Match)";
    else if (pd == 0 && pu == 0) status = "LOW (0V/GND)";

    Serial.printf("[CRSF] Boot Check GP%d: %s (%lu edges/40ms)\n",
                  (int)PIN_CRSF_RX, status, (unsigned long)edges);
}

// ============================================================================
// Arduino Setup & Main Entrypoint
// ============================================================================
void setup() {
    // Initialize Native USB CDC Serial
    Serial.begin(115200);

    // Initialize Onboard WS2812B Status LED immediately
    g_statusPixel.begin();
    g_statusPixel.setBrightness(STATUS_LED_BRIGHTNESS);
    g_statusPixel.setPixelColor(0, g_statusPixel.Color(0, 180, 220));
    g_statusPixel.show();

    // Brief non-blocking wait (up to 2.5s) for USB CDC host connection
    uint32_t usbWaitStart = millis();
    while (!Serial && (millis() - usbWaitStart < 2500)) {
        delay(10);
    }
    delay(200);

    Serial.println();
    Serial.println("========================================");
    Serial.printf("  CHIRP Droid Control %s\n", CHIRP_FW_VERSION);
    Serial.printf("  Target: %s\n", CHIRP_TARGET_NAME);
    Serial.println("========================================");
    Serial.printf("[SYS] CPU Freq: %lu MHz | Flash: %lu MB | PSRAM: %lu KB free\n",
                  (unsigned long)getCpuFrequencyMhz(),
                  (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)),
                  (unsigned long)(ESP.getFreePsram() / 1024));

    // Create RTOS Synchronization Primitives
    g_stateMutex   = xSemaphoreCreateMutex();
    g_commandQueue = xQueueCreate(32, sizeof(SystemCommand));

    if (g_stateMutex == nullptr || g_commandQueue == nullptr) {
        Serial.println("[ERR] Failed to allocate FreeRTOS synchronization primitives!");
        g_statusPixel.setPixelColor(0, g_statusPixel.Color(255, 0, 0));
        g_statusPixel.show();
        while (true) {
            delay(1000);
        }
    }

    // Initialize Phase 2 & Phase 3 Hardware Peripherals
    storageManagerInit();
    driveControlInit();
    domeControlInit();
    domeLightingInit();
    audioLinkInit();
    telemetryInit();
    checkCrsfRxLineAtBoot();
    g_crsfLink.begin(Serial1, PIN_CRSF_RX, PIN_CRSF_TX, CRSF_BAUD_RATE);

    // Launch Core 1 Real-Time Deterministic Control Task (High Priority)
    BaseType_t c1Result = xTaskCreatePinnedToCore(
        taskRealTimeControl,
        "ControlCore1",
        8192,
        nullptr,
        configMAX_PRIORITIES - 2,
        &g_controlTaskHandle,
        1 // Pinned to Core 1
    );

    // Launch Core 0 Communications & Background Services Task (Normal Priority)
    BaseType_t c0Result = xTaskCreatePinnedToCore(
        taskCommunications,
        "CommsCore0",
        8192,
        nullptr,
        2,
        &g_commsTaskHandle,
        0 // Pinned to Core 0
    );

    if (c1Result == pdPASS && c0Result == pdPASS) {
        Serial.println("[SYS] FreeRTOS tasks started successfully.");
    } else {
        Serial.println("[ERR] Failed to start FreeRTOS tasks!");
    }
}

void loop() {
    // Periodic 2-second diagnostic log over USB Serial for bench verification
    static uint32_t lastStatusPrintMs = 0;
    uint32_t nowMs = millis();

    if (nowMs - lastStatusPrintMs >= 2000) {
        lastStatusPrintMs = nowMs;

        RobotState snap;
        if (lockState(pdMS_TO_TICKS(10))) {
            snap = g_robotState;
            unlockState();
        }

        const char* spdStr = (snap.speedMode == SpeedMode::SLOW) ? "SLO" :
                             (snap.speedMode == SpeedMode::MEDIUM) ? "MED" : "FST";
        const char* audStr = (g_audioSyncState == AudioSyncState::COMPLETE) ? "OK" :
                             (g_audioSyncState == AudioSyncState::FETCHING_NAMES) ? "NAMES" : "WAIT";

        Serial.printf("[HB] Up:%lus | Link:%s (LQ:%u%% %ddBm RX:%lu TX:%lu) | Arm:%s | Spd:%s | Vol:%u%% | "
                      "Aud:%s(Pg%c) | Batt:%.1f/%.1f/%.1fV | PWM L:%u R:%u D:%u\n",
                      (unsigned long)(nowMs / 1000),
                      snap.rcLinkUp ? "UP" : "DOWN",
                      snap.linkQuality,
                      (int)snap.rssiDbm,
                      (unsigned long)g_crsfLink.getValidFrameCount(),
                      (unsigned long)g_crsfLink.getTxFrameCount(),
                      snap.isArmed ? "YES" : "NO",
                      spdStr,
                      snap.masterVolume,
                      audStr,
                      snap.activePage,
                      snap.battery1Volts,
                      snap.battery2Volts,
                      snap.battery3Volts,
                      snap.leftFootPwmUs,
                      snap.rightFootPwmUs,
                      snap.domeMotorPwmUs);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
}

