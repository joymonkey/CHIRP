#pragma once

#include <Arduino.h>

// ============================================================================
// CHIRP Droid Control — Firmware Version & Build Metadata
// ============================================================================
#define CHIRP_FW_VERSION        "26.10.18-ESP32S3"

#if defined(BOARD_ESP32_S3_DEVKIT)
  #define CHIRP_TARGET_NAME     "ESP32-S3-DevKitC-1 N16R8 (16MB Flash / 8MB OPI PSRAM)"
  constexpr int8_t PIN_STATUS_NEOPIXEL = 48; // Standard DevKitC-1 RGB LED pin (some clones use 38)
#else
  #define CHIRP_TARGET_NAME     "Waveshare ESP32-S3-Zero (4MB Flash / 2MB PSRAM)"
  constexpr int8_t PIN_STATUS_NEOPIXEL = 21; // Waveshare S3-Zero onboard RGB LED
#endif

// ============================================================================
// Thru-Hole Pin Budget (Compatible with both S3-Zero & S3-DevKitC-1 N16R8)
// ============================================================================
// Left Header (S3-Zero Pins 1-9, Top to Bottom):
//   Pin 1: 5V
//   Pin 2: GND
//   Pin 3: 3V3
//   Pin 4: GP1  -> CHIRP_Audio TX (HW UART2 / Serial2)
//   Pin 5: GP2  -> CHIRP_Audio RX (HW UART2 / Serial2)
//   Pin 6: GP3  -> Spare / Aux Output (Strapping pin JTAG_SEL - isolated from UART pull-ups)
//   Pin 7: GP4  -> ExpressLRS / CRSF TX (HW UART1 / Serial1 -> Connect to Receiver RX)
//   Pin 8: GP5  -> ExpressLRS / CRSF RX (HW UART1 / Serial1 -> Connect to Receiver TX)
//   Pin 9: GP6  -> Left Foot Motor PWM (50Hz LEDC Ch0)
//
// Right Header (S3-Zero Pins 1-9, Top to Bottom):
//   Pin 1: GP43 -> Roam-A-Dome TX (HW UART0 / Serial0)
//   Pin 2: GP44 -> Roam-A-Dome RX (HW UART0 / Serial0)
//   Pin 3: GP13 -> Dome Lighting RX (SoftSerial)
//   Pin 4: GP12 -> Dome Lighting TX (SoftSerial)
//   Pin 5: GP11 -> Dome Motor PWM (50Hz LEDC Ch2)
//   Pin 6: GP10 -> Battery 3 Voltage ADC (ADC1_CH9)
//   Pin 7: GP9  -> Battery 2 Voltage ADC (ADC1_CH8)
//   Pin 8: GP8  -> Battery 1 Voltage ADC (ADC1_CH7)
//   Pin 9: GP7  -> Right Foot Motor PWM (50Hz LEDC Ch1)

// --- Hardware UART 1 (Serial1): ExpressLRS / CRSF Receiver & Telemetry ---
constexpr int8_t   PIN_CRSF_TX          = 4;   // GP4 -> Connect to ELRS Receiver RX
constexpr int8_t   PIN_CRSF_RX          = 5;   // GP5 -> Connect to ELRS Receiver TX
constexpr uint32_t CRSF_BAUD_RATE       = 420000;

// --- Hardware UART 0 (Serial0): Roam-A-Dome (RAD) Position Controller ---
constexpr int8_t   PIN_RAD_TX           = 43;  // TX (GP43)
constexpr int8_t   PIN_RAD_RX           = 44;  // RX (GP44)
constexpr uint32_t RAD_BAUD_RATE        = 9600;

// --- Hardware UART 2 (Serial2): CHIRP_Audio Sound Trigger Board ---
constexpr int8_t   PIN_AUDIO_TX         = 1;
constexpr int8_t   PIN_AUDIO_RX         = 2;
constexpr uint32_t AUDIO_BAUD_RATE      = 115200;

// --- SoftwareSerial: RSeries Dome Lighting Logic Engine ---
constexpr int8_t   PIN_DOME_LIGHT_TX    = 12;
constexpr int8_t   PIN_DOME_LIGHT_RX    = 13;
constexpr uint32_t DOME_LIGHT_BAUD_RATE = 9600;

// --- 50Hz RC PWM Outputs (1000-2000us, 1500us Neutral) ---
constexpr int8_t   PIN_PWM_FOOT_LEFT    = 6;
constexpr int8_t   PIN_PWM_FOOT_RIGHT   = 7;
constexpr int8_t   PIN_PWM_DOME_MOTOR   = 11;

constexpr uint16_t PWM_MIN_US           = 1000;
constexpr uint16_t PWM_NEUTRAL_US       = 1500;
constexpr uint16_t PWM_MAX_US           = 2000;
constexpr uint16_t PWM_DEADBAND_US      = 25;

// --- Spare / Auxiliary Pin (Strapping Pin GP3 - Output Only) ---
constexpr int8_t   PIN_AUX_SPARE        = 3;

// --- Battery Voltage Sensing (Strictly on ADC1 for Wi-Fi / ESP-NOW Compatibility) ---
constexpr int8_t   PIN_ADC_BATT1        = 8;   // ADC1_CH7: Primary Drive Battery (12V/24V)
constexpr int8_t   PIN_ADC_BATT2        = 9;   // ADC1_CH8: Audio System Battery
constexpr int8_t   PIN_ADC_BATT3        = 10;  // ADC1_CH9: Aux / Dome Battery

// --- Onboard WS2812B Status Pixel ---
constexpr uint8_t  STATUS_NEOPIXEL_CNT  = 1;
constexpr uint8_t  STATUS_LED_BRIGHTNESS = 32; // Safe moderate brightness (0-255)

// ============================================================================
// Radiomaster Zorro RC Channel Assignments (1-Indexed, 1..16)
// ============================================================================
constexpr uint8_t  RC_CH_DRIVE_STEER    = 1;   // Right Stick X (Differential Steering)
constexpr uint8_t  RC_CH_DRIVE_THROTTLE = 2;   // Right Stick Y (Forward / Reverse)
constexpr uint8_t  RC_CH_RESERVED_LY    = 3;   // Left Stick Y  (Reserved / Aux)
constexpr uint8_t  RC_CH_DOME_YAW       = 4;   // Left Stick X  (Dome CW / CCW Rotation)
constexpr uint8_t  RC_CH_ARM_SWITCH     = 5;   // Switch SE     (2-Pos Disarm / Arm Interlock)
constexpr uint8_t  RC_CH_SPEED_MODE     = 6;   // Switch SB     (3-Pos Slow / Med / Fast)
constexpr uint8_t  RC_CH_AUTODOME_MODE  = 7;   // Switch SC     (3-Pos Off / Less / More)
constexpr uint8_t  RC_CH_MASTER_VOLUME  = 8;   // Dial S2       (Continuous 0-99% Volume)
constexpr uint8_t  RC_CH_SOUND_FALLBACK = 9;   // Optional CH9 Pulse Trigger Fallback (1050-2000us)

// ============================================================================
// FreeRTOS Task & Timing Configuration
// ============================================================================
constexpr uint32_t CONTROL_LOOP_PERIOD_MS   = 5;    // 200Hz Real-Time Control Loop (Core 1)
constexpr uint32_t COMMS_LOOP_PERIOD_MS     = 10;   // 100Hz Background Comms Loop (Core 0)
constexpr uint32_t HEARTBEAT_PERIOD_MS      = 500;  // 500ms Status LED Heartbeat Toggle
constexpr uint32_t RC_FAILSAFE_TIMEOUT_MS   = 250;  // Disarm & neutral motors if no CRSF frame in 250ms

// ============================================================================
// Phase 3: CHIRP_Audio Manifest, Voice Prompts & Telemetry Configuration
// ============================================================================
constexpr uint8_t  AUDIO_MAX_BANKS          = 4;
constexpr uint8_t  AUDIO_MAX_PAGES          = 6;    // 'A'..'F' (dynamically discovered from SD card)
constexpr char     AUDIO_MAX_PAGE_CHAR      = (char)('A' + AUDIO_MAX_PAGES - 1);
constexpr uint8_t  AUDIO_MAX_SOUNDS         = 24;
constexpr uint8_t  AUDIO_MAX_NAME_LEN       = 11;   // Max 11 chars for CRSF Flight Mode telemetry packet

constexpr uint32_t AUDIO_GMAN_RETRY_MS      = 2500; // Retry interval for GMAN when syncing manifest
constexpr uint32_t AUDIO_GNME_TIMEOUT_MS    = 800;  // Timeout per GNME sound name query
constexpr uint8_t  AUDIO_GNME_MAX_RETRIES   = 3;    // Skip track name after 3 timeouts so sync never stalls

// Voice Feedback (PVOICE) Timing
constexpr uint32_t VOICE_WORD_TIMEOUT_MS    = 500;  // Max wait for PACK:PVOICE ACK
constexpr uint32_t VOICE_WORD_GAP_MS        = 100;  // Pause between words in a phrase
constexpr uint32_t VOICE_COOLDOWN_MS        = 3000; // Min interval between same-priority announcements

// CRSF Telemetry Uplink & Battery ADC1 Sensing
constexpr uint32_t TELEMETRY_SLOT_PERIOD_MS = 35;   // 1 telemetry frame per 35ms (~28Hz total across 3 slots)
constexpr uint32_t BATT_SAMPLE_PERIOD_MS    = 250;  // 4Hz ADC1 voltage sampling on GP8, GP9, GP10
constexpr float    BATT_DIVIDER_RATIO       = 10.0f;// Voltage divider multiplier (e.g. 10:1 -> 0..33V range)
constexpr float    BATT1_LOW_WARN_VOLTS     = 11.0f;// Trigger low-battery voice alert below 11.0V
constexpr float    BATT1_LOW_RESET_VOLTS    = 11.5f;// Hysteresis reset threshold

