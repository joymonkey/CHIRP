#pragma once

#include <Arduino.h>
#include "config.h"
#include "types.h"

// ============================================================================
// CRSF Protocol Constants
// ============================================================================
constexpr uint8_t CRSF_SYNC_BYTE_FC             = 0xC8; // Flight Controller / Robot Main Address
constexpr uint8_t CRSF_SYNC_BYTE_HANDSET        = 0xEA; // Radio Transmitter Address
constexpr uint8_t CRSF_SYNC_BYTE_RX             = 0xEC; // CRSF Receiver Address
constexpr uint8_t CRSF_SYNC_BYTE_TX             = 0xEE; // CRSF TX Module Address
constexpr uint8_t CRSF_MAX_PACKET_LEN           = 64;

// Frame Types
constexpr uint8_t CRSF_FRAMETYPE_GPS            = 0x02;
constexpr uint8_t CRSF_FRAMETYPE_BATTERY_SENSOR = 0x08;
constexpr uint8_t CRSF_FRAMETYPE_LINK_STATISTICS= 0x14;
constexpr uint8_t CRSF_FRAMETYPE_RC_CHANNELS    = 0x16;
constexpr uint8_t CRSF_FRAMETYPE_FLIGHT_MODE    = 0x21;
constexpr uint8_t CRSF_FRAMETYPE_COMMAND        = 0x32;

// CHIRP Direct Command Sub-IDs (inside Frame 0x32: [0xC8, 0xEA, SubCmd, ...])
constexpr uint8_t CHIRP_CMD_PLAY_SOUND          = 0xA1; // Payload: [0xC8, 0xEA, 0xA1, Bank, PageChar, Track]
constexpr uint8_t CHIRP_CMD_STOP_AUDIO          = 0xA2; // Payload: [0xC8, 0xEA, 0xA2]
constexpr uint8_t CHIRP_CMD_DOME_LIGHT          = 0xA3; // Payload: [0xC8, 0xEA, 0xA3, ...Marcduino string...]
constexpr uint8_t CHIRP_CMD_SET_PAGE            = 0xA4; // Payload: [0xC8, 0xEA, 0xA4, PageChar ('A'/'B'/'C')]
constexpr uint8_t CHIRP_CMD_SYNC_MANIFEST       = 0xA5; // Payload: [0xC8, 0xEA, 0xA5]

// ============================================================================
// Packed CRSF Structs
// ============================================================================
struct __attribute__((packed)) CrsfChannelsPacked {
    unsigned ch0  : 11;
    unsigned ch1  : 11;
    unsigned ch2  : 11;
    unsigned ch3  : 11;
    unsigned ch4  : 11;
    unsigned ch5  : 11;
    unsigned ch6  : 11;
    unsigned ch7  : 11;
    unsigned ch8  : 11;
    unsigned ch9  : 11;
    unsigned ch10 : 11;
    unsigned ch11 : 11;
    unsigned ch12 : 11;
    unsigned ch13 : 11;
    unsigned ch14 : 11;
    unsigned ch15 : 11;
};

struct __attribute__((packed)) CrsfLinkStatistics {
    uint8_t uplink_RSSI_1;
    uint8_t uplink_RSSI_2;
    uint8_t uplink_Link_quality;
    int8_t  uplink_SNR;
    uint8_t active_antenna;
    uint8_t rf_Mode;
    uint8_t uplink_TX_Power;
    uint8_t downlink_RSSI;
    uint8_t downlink_Link_quality;
    int8_t  downlink_SNR;
};

// CRSF 0x08 Battery Sensor Payload (8 bytes, Big-Endian fields)
struct __attribute__((packed)) CrsfBatterySensor {
    uint16_t voltage_dV;      // Voltage in 0.1V units (Big-Endian)
    uint16_t current_dA;      // Current in 0.1A units (Big-Endian)
    uint8_t  capacity_mAh[3]; // 24-bit capacity in mAh (Big-Endian)
    uint8_t  remaining_pct;   // 0..100%
};

// CRSF 0x02 GPS Sensor Payload (15 bytes, Big-Endian fields)
struct __attribute__((packed)) CrsfGpsSensor {
    int32_t  latitude;        // degree / 10,000,000 (Big-Endian)
    int32_t  longitude;       // degree / 10,000,000 (Big-Endian)
    uint16_t groundspeed;     // km/h / 10 (Big-Endian)
    uint16_t heading;         // degree / 100 (Big-Endian)
    uint16_t altitude;        // meters + 1000m offset (Big-Endian)
    uint8_t  satellites;      // satellite count / packed status bitfield
};

// ============================================================================
// CrsfLink Class (Sliding-Window CRC Receiver & Telemetry Uplink Driver)
// ============================================================================
class CrsfLink {
public:
    void begin(HardwareSerial& port, int8_t rxPin, int8_t txPin, uint32_t baud = CRSF_BAUD_RATE);
    void update();

    // Send a telemetry frame back to the ExpressLRS receiver (thread-safe across cores)
    void sendTelemetryFrame(uint8_t frameType, const void* payload, uint8_t payloadLen);

    // Diagnostic counters for bench verification
    uint32_t getRxByteCount() const     { return _rxByteCount; }
    uint32_t getValidFrameCount() const { return _validFrameCount; }
    uint32_t getCrcErrorCount() const   { return _crcErrorCount; }
    uint32_t getTxFrameCount() const    { return _txFrameCount; }

    static uint8_t  crc8(const uint8_t* data, uint8_t len);
    static uint16_t ticksToUs(uint16_t ticks);

private:
    HardwareSerial*   _port             = nullptr;
    SemaphoreHandle_t _txMutex          = nullptr;
    uint8_t           _rxBuf[CRSF_MAX_PACKET_LEN];
    uint8_t           _rxBufPos         = 0;
    uint32_t          _lastByteMs       = 0;

    // Diagnostic counters
    volatile uint32_t _rxByteCount      = 0;
    volatile uint32_t _validFrameCount  = 0;
    volatile uint32_t _crcErrorCount    = 0;
    volatile uint32_t _txFrameCount     = 0;

    // Edge-tracking for console logging, arm-speed interlock & sound fallback
    bool            _prevLinkUp          = false;
    bool            _prevArmed           = false;
    bool            _warnedBootArm       = false;
    bool            _speedLockedToSlow   = false;
    SpeedMode       _armLatchSpeedSwitch = SpeedMode::SLOW;
    SpeedMode       _prevSpeedMode       = SpeedMode::SLOW;
    AutoDomeMode    _prevAutoDome        = AutoDomeMode::OFF;
    uint8_t         _prevVolume          = 255;

    // CH9 Fallback Sound Trigger state (starts latched=true until idle <1025us is seen)
    bool            _ch9Latched       = true;
    uint16_t        _ch9PrevUs        = 1000;
    uint8_t         _ch9StableCount   = 0;

    void handleByteReceived();
    void shiftRxBuffer(uint8_t count);
    void processFrame(uint8_t frameType, const uint8_t* payload, uint8_t payloadLen);
    void handleRcChannels(const uint8_t* payload, uint8_t payloadLen);
    void handleLinkStats(const uint8_t* payload, uint8_t payloadLen);
    void handleCommandFrame(const uint8_t* payload, uint8_t payloadLen);
    void evaluateSwitchesAndTriggers(const uint16_t channelsUs[17]);
};

extern CrsfLink g_crsfLink;
