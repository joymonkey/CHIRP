#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

// ============================================================================
// Enumerations
// ============================================================================

enum class SpeedMode : uint8_t {
    DISARMED = 0,
    SLOW     = 1,   // ~30% max output
    MEDIUM   = 2,   // ~70% max output
    FAST     = 3    // 100% max output
};

enum class AutoDomeMode : uint8_t {
    OFF           = 0,
    LESS_FREQUENT = 1,
    MORE_FREQUENT = 2
};

enum class CommandType : uint8_t {
    NONE = 0,
    PLAY_SOUND,         // Trigger PLAY:<track>,<bank>,<page>,<vol> on CHIRP_Audio
    STOP_AUDIO,         // Stop active audio playback
    SET_VOLUME,         // Update master volume (VOL:<0-99>)
    PLAY_VOICE_PROMPT,  // Trigger vocal status feedback (PVOICE)
    UPDATE_SETTING,     // Update & persist NVS configuration parameter
    DOME_LIGHT_CMD,     // Send Marcduino command string to RSeries Logic Engine
    SET_ACTIVE_PAGE,    // Update active sound page ('A', 'B', 'C') from handset
    SYNC_MANIFEST       // Trigger fresh GMAN sound manifest sync with CHIRP_Audio
};

enum class AudioSyncState : uint8_t {
    IDLE = 0,
    REQUESTING_MANIFEST = 1,
    FETCHING_NAMES      = 2,
    COMPLETE            = 3
};

// ============================================================================
// CHIRP_Audio Sound Manifest Structures (4 Banks x 3 Pages x 24 Tracks)
// ============================================================================
struct AudioSoundEntry {
    char name[12]; // Up to 11 chars + null terminator
};

struct AudioBankPage {
    char            name[12];
    uint8_t         soundCount;
    AudioSoundEntry sounds[24];
};

struct AudioBank {
    AudioBankPage pages[6]; // 0='A', 1='B', 2='C', 3='D', 4='E', 5='F'
};

struct AudioManifest {
    AudioBank banks[4];     // 0=Bank1 .. 3=Bank4
};

// ============================================================================
// Inter-Core Command Queue Message (Core 1 -> Core 0)
// ============================================================================
struct SystemCommand {
    CommandType type;
    uint8_t     bank;       // 1..4
    char        page;       // 'A', 'B', or 'C'
    uint8_t     track;      // 1..255
    uint8_t     paramId;    // Config parameter ID
    int16_t     paramValue; // Config parameter value or volume (0..99)
    char        text[24];   // Optional short command / voice identifier
};

// ============================================================================
// Persistent User Configuration (Stored in NVS)
// ============================================================================
struct RobotConfig {
    char    bank1Page        = 'A';
    int8_t  domeOffset       = 0;
    bool    domeInvert       = false;
    bool    voiceDebug       = true;
    bool    leftMotorInvert  = false;
    bool    rightMotorInvert = false;
    uint8_t speedSlowPct     = 30;
    uint8_t speedMedPct      = 70;
    uint8_t speedFastPct     = 100;
};

// ============================================================================
// Thread-Safe Shared Robot State
// ============================================================================
struct RobotState {
    // --- Link & Safety Interlock State ---
    bool          rcLinkUp            = false;
    bool          armSwitchSafeSeen   = false; // Boot-disarm interlock: must see Disarmed first
    bool          isArmed             = false;
    uint32_t      lastValidCrsfMs     = 0;

    // --- RC Channel Values (1000..2000us, 1-indexed [1..16]) ---
    uint16_t      rcChannelsUs[17]    = {
        1500,
        1500, 1500, 1500, 1500, 1000, 1000, 1000, 1000,
        1000, 1500, 1500, 1500, 1500, 1500, 1500, 1500
    };

    // --- Current Motor Outputs (us) ---
    uint16_t      leftFootPwmUs       = 1500;
    uint16_t      rightFootPwmUs      = 1500;
    uint16_t      domeMotorPwmUs      = 1500;

    // --- Operational Modes & Audio ---
    SpeedMode     speedMode           = SpeedMode::SLOW;
    AutoDomeMode  autoDomeMode        = AutoDomeMode::OFF;
    bool          autoChirpEnabled    = false;
    uint8_t       masterVolume        = 50;    // 0..99%
    char          activePage          = 'A';   // 'A', 'B', or 'C'
    int16_t       domeHeadingDeg      = 0;     // 0..359 degrees from Roam-A-Dome

    // --- Telemetry & Power Monitors ---
    uint8_t       linkQuality         = 0;     // 0..100%
    int8_t        rssiDbm             = 0;
    float         battery1Volts       = 0.0f;  // Primary drive bus voltage
    float         battery2Volts       = 0.0f;  // Audio system voltage
    float         battery3Volts       = 0.0f;  // Aux / dome voltage
    float         currentDrawAmps     = 0.0f;
    float         peakCurrentAmps     = 0.0f;

    // --- System Diagnostics ---
    uint32_t      core1LoopHz         = 0;
    uint32_t      core0LoopHz         = 0;
};

