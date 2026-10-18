#include "crsf_link.h"

extern RobotState        g_robotState;
extern RobotConfig       g_robotConfig;
extern QueueHandle_t     g_commandQueue;
extern bool              lockState(TickType_t timeoutTicks);
extern void              unlockState();

CrsfLink g_crsfLink;

// Standard CRSF CRC8 lookup table (Polynomial 0xD5)
static const uint8_t CRSF_CRC8_TAB[256] = {
    0x00, 0xD5, 0x7F, 0xAA, 0xFE, 0x2B, 0x81, 0x54, 0x29, 0xFC, 0x56, 0x83, 0xD7, 0x02, 0xA8, 0x7D,
    0x52, 0x87, 0x2D, 0xF8, 0xAC, 0x79, 0xD3, 0x06, 0x7B, 0xAE, 0x04, 0xD1, 0x85, 0x50, 0xFA, 0x2F,
    0xA4, 0x71, 0xDB, 0x0E, 0x5A, 0x8F, 0x25, 0xF0, 0x8D, 0x58, 0xF2, 0x27, 0x73, 0xA6, 0x0C, 0xD9,
    0xF6, 0x23, 0x89, 0x5C, 0x08, 0xDD, 0x77, 0xA2, 0xDF, 0x0A, 0xA0, 0x75, 0x21, 0xF4, 0x5E, 0x8B,
    0x9D, 0x48, 0xE2, 0x37, 0x63, 0xB6, 0x1C, 0xC9, 0xB4, 0x61, 0xCB, 0x1E, 0x4A, 0x9F, 0x35, 0xE0,
    0xCF, 0x1A, 0xB0, 0x65, 0x31, 0xE4, 0x4E, 0x9B, 0xE6, 0x33, 0x99, 0x4C, 0x18, 0xCD, 0x67, 0xB2,
    0x39, 0xEC, 0x46, 0x93, 0xC7, 0x12, 0xB8, 0x6D, 0x10, 0xC5, 0x6F, 0xBA, 0xEE, 0x3B, 0x91, 0x44,
    0x6B, 0xBE, 0x14, 0xC1, 0x95, 0x40, 0xEA, 0x3F, 0x42, 0x97, 0x3D, 0xE8, 0xBC, 0x69, 0xC3, 0x16,
    0xEF, 0x3A, 0x90, 0x45, 0x11, 0xC4, 0x6E, 0xBB, 0xC6, 0x13, 0xB9, 0x6C, 0x38, 0xED, 0x47, 0x92,
    0xBD, 0x68, 0xC2, 0x17, 0x43, 0x96, 0x3C, 0xE9, 0x94, 0x41, 0xEB, 0x3E, 0x6A, 0xBF, 0x15, 0xC0,
    0x4B, 0x9E, 0x34, 0xE1, 0xB5, 0x60, 0xCA, 0x1F, 0x62, 0xB7, 0x1D, 0xC8, 0x9C, 0x49, 0xE3, 0x36,
    0x19, 0xCC, 0x66, 0xB3, 0xE7, 0x32, 0x98, 0x4D, 0x30, 0xE5, 0x4F, 0x9A, 0xCE, 0x1B, 0xB1, 0x64,
    0x72, 0xA7, 0x0D, 0xD8, 0x8C, 0x59, 0xF3, 0x26, 0x5B, 0x8E, 0x24, 0xF1, 0xA5, 0x70, 0xDA, 0x0F,
    0x20, 0xF5, 0x5F, 0x8A, 0xDE, 0x0B, 0xA1, 0x74, 0x09, 0xDC, 0x76, 0xA3, 0xF7, 0x22, 0x88, 0x5D,
    0xD6, 0x03, 0xA9, 0x7C, 0x28, 0xFD, 0x57, 0x82, 0xFF, 0x2A, 0x80, 0x55, 0x01, 0xD4, 0x7E, 0xAB,
    0x84, 0x51, 0xFB, 0x2E, 0x7A, 0xAF, 0x05, 0xD0, 0xAD, 0x78, 0xD2, 0x07, 0x53, 0x86, 0x2C, 0xF9
};

uint8_t CrsfLink::crc8(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc = CRSF_CRC8_TAB[crc ^ data[i]];
    }
    return crc;
}

uint16_t CrsfLink::ticksToUs(uint16_t ticks) {
    // Standard CRSF 11-bit ticks: 172 -> 988us, 992 -> 1500us, 1811 -> 2012us
    int32_t us = 1500 + (((int32_t)ticks - 992) * 5) / 8;
    if (us < 900)  us = 900;
    if (us > 2100) us = 2100;
    return (uint16_t)us;
}

void CrsfLink::begin(HardwareSerial& port, int8_t rxPin, int8_t txPin, uint32_t baud) {
    _port = &port;
    if (_txMutex == nullptr) {
        _txMutex = xSemaphoreCreateMutex();
    }

    _port->setRxBufferSize(512);
    _port->begin(baud, SERIAL_8N1, rxPin, txPin);
    _port->setRxFIFOFull(1);
    _port->setRxTimeout(1);

    _rxBufPos   = 0;
    _lastByteMs = millis();

    Serial.printf("[CRSF] Initialized on UART1 (RX=GP%d, TX=GP%d @ %lu baud)\n",
                  rxPin, txPin, (unsigned long)baud);
    Serial.flush();
}

void CrsfLink::shiftRxBuffer(uint8_t count) {
    if (count >= _rxBufPos) {
        _rxBufPos = 0;
        return;
    }
    uint8_t* src = &_rxBuf[count];
    uint8_t* dst = &_rxBuf[0];
    _rxBufPos -= count;
    uint8_t left = _rxBufPos;
    while (left--) {
        *dst++ = *src++;
    }
}

// Direct implementation of CapnBry's CRServoF / CrsfSerial::handleByteReceived()
void CrsfLink::handleByteReceived() {
    bool reprocess;
    do {
        reprocess = false;
        if (_rxBufPos > 1) {
            uint8_t len = _rxBuf[1];

            // Sanity check the declared length isn't outside Type + Payload{1..60} + CRC
            if (len < 3 || len > (CRSF_MAX_PACKET_LEN - 2)) {
                shiftRxBuffer(1);
                reprocess = true;
            } else if (_rxBufPos >= (len + 2)) {
                uint8_t inCrc = _rxBuf[2 + len - 1];
                uint8_t crc   = crc8(&_rxBuf[2], len - 1);

                if (crc == inCrc) {
                    _validFrameCount++;
                    uint8_t frameType  = _rxBuf[2];
                    uint8_t payloadLen = len - 2; // Exclude Type and CRC
                    processFrame(frameType, &_rxBuf[3], payloadLen);
                    shiftRxBuffer(len + 2);
                    reprocess = true;
                } else {
                    _crcErrorCount++;
                    shiftRxBuffer(1);
                    reprocess = true;
                }
            }
        }
    } while (reprocess);
}

void CrsfLink::update() {
    if (_port == nullptr) return;

    uint32_t nowMs = millis();

    // Flush stale partial buffer a byte at a time if no data received for > 100ms (matches CRServoF)
    if (_rxBufPos > 0 && (nowMs - _lastByteMs > 100)) {
        while (_rxBufPos > 0) {
            shiftRxBuffer(1);
        }
    }

    // 1. Drain all available bytes from UART RX ring buffer into sliding window
    while (_port->available() > 0) {
        uint8_t b = (uint8_t)_port->read();
        _lastByteMs = nowMs;
        _rxByteCount++;

        _rxBuf[_rxBufPos++] = b;
        handleByteReceived();

        if (_rxBufPos >= sizeof(_rxBuf)) {
            _rxBufPos = 0;
        }
    }

    // 2. Check for RC Link Timeout (250ms Zero-Latency Failsafe)
    nowMs = millis();
    bool isUp = false;

    if (lockState(pdMS_TO_TICKS(2))) {
        uint32_t lastValid = g_robotState.lastValidCrsfMs;
        isUp = (lastValid > 0) && ((nowMs - lastValid) <= RC_FAILSAFE_TIMEOUT_MS);

        if (!isUp) {
            g_robotState.rcLinkUp          = false;
            g_robotState.isArmed           = false;
            g_robotState.armSwitchSafeSeen = false; // Require fresh switch cycle on reconnect!
            g_robotState.speedMode         = SpeedMode::SLOW;
            g_robotState.leftFootPwmUs     = PWM_NEUTRAL_US;
            g_robotState.rightFootPwmUs    = PWM_NEUTRAL_US;
            g_robotState.domeMotorPwmUs    = PWM_NEUTRAL_US;
        } else {
            g_robotState.rcLinkUp = true;
        }
        unlockState();
    }

    if (isUp != _prevLinkUp) {
        _prevLinkUp = isUp;
        _warnedBootArm = false;
        if (!isUp) {
            _prevArmed         = false;
            _speedLockedToSlow = false;
            _prevSpeedMode     = SpeedMode::SLOW;
            _ch9Latched        = true;
            _ch9StableCount    = 0;
        }
        Serial.printf("[CRSF] RC Link -> %s\n", isUp ? "UP (Connected)" : "DOWN (Failsafe Active)");

        if (g_commandQueue != nullptr) {
            SystemCommand vcmd = {};
            vcmd.type       = CommandType::PLAY_VOICE_PROMPT;
            vcmd.paramId    = isUp ? 1 : 0; // Priority 1 for connect, Priority 0 (urgent) for disconnect
            strncpy(vcmd.text, isUp ? "LINK_UP" : "LINK_DOWN", sizeof(vcmd.text) - 1);
            xQueueSend(g_commandQueue, &vcmd, 0);
        }
    }
}

void CrsfLink::processFrame(uint8_t frameType, const uint8_t* payload, uint8_t payloadLen) {
    switch (frameType) {
        case CRSF_FRAMETYPE_RC_CHANNELS:
            handleRcChannels(payload, payloadLen);
            break;

        case CRSF_FRAMETYPE_LINK_STATISTICS:
            handleLinkStats(payload, payloadLen);
            break;

        case CRSF_FRAMETYPE_COMMAND:
            handleCommandFrame(payload, payloadLen);
            break;

        default:
            break;
    }
}

void CrsfLink::handleRcChannels(const uint8_t* payload, uint8_t payloadLen) {
    if (payloadLen < sizeof(CrsfChannelsPacked)) return;

    const CrsfChannelsPacked* ch = reinterpret_cast<const CrsfChannelsPacked*>(payload);
    uint16_t channelsUs[17];
    channelsUs[0]  = PWM_NEUTRAL_US;
    channelsUs[1]  = ticksToUs(ch->ch0);
    channelsUs[2]  = ticksToUs(ch->ch1);
    channelsUs[3]  = ticksToUs(ch->ch2);
    channelsUs[4]  = ticksToUs(ch->ch3);
    channelsUs[5]  = ticksToUs(ch->ch4);
    channelsUs[6]  = ticksToUs(ch->ch5);
    channelsUs[7]  = ticksToUs(ch->ch6);
    channelsUs[8]  = ticksToUs(ch->ch7);
    channelsUs[9]  = ticksToUs(ch->ch8);
    channelsUs[10] = ticksToUs(ch->ch9);
    channelsUs[11] = ticksToUs(ch->ch10);
    channelsUs[12] = ticksToUs(ch->ch11);
    channelsUs[13] = ticksToUs(ch->ch12);
    channelsUs[14] = ticksToUs(ch->ch13);
    channelsUs[15] = ticksToUs(ch->ch14);
    channelsUs[16] = ticksToUs(ch->ch15);

    evaluateSwitchesAndTriggers(channelsUs);
}

void CrsfLink::handleLinkStats(const uint8_t* payload, uint8_t payloadLen) {
    if (payloadLen < sizeof(CrsfLinkStatistics)) return;
    const CrsfLinkStatistics* stats = reinterpret_cast<const CrsfLinkStatistics*>(payload);

    if (lockState(pdMS_TO_TICKS(2))) {
        g_robotState.linkQuality = stats->uplink_Link_quality;
        g_robotState.rssiDbm     = -(int8_t)stats->uplink_RSSI_1;
        unlockState();
    }
}

void CrsfLink::evaluateSwitchesAndTriggers(const uint16_t channelsUs[17]) {
    uint32_t nowMs = millis();

    // 1. Arm Switch SE (CH5) with Boot-Disarmed Safety Interlock
    uint16_t armCh = channelsUs[RC_CH_ARM_SWITCH];
    bool switchRequestedArm = (armCh > 1700);
    bool switchIsDisarmed   = (armCh < 1300);

    // 2. Physical Speed Mode Switch SB (CH6)
    uint16_t speedCh = channelsUs[RC_CH_SPEED_MODE];
    SpeedMode rawSpeedSwitch = SpeedMode::MEDIUM;
    if (speedCh < 1300)      rawSpeedSwitch = SpeedMode::SLOW;
    else if (speedCh > 1700) rawSpeedSwitch = SpeedMode::FAST;

    // 3. AutoDome Mode Switch SC (CH7)
    uint16_t autoDomeCh = channelsUs[RC_CH_AUTODOME_MODE];
    AutoDomeMode newAutoDome = AutoDomeMode::LESS_FREQUENT;
    if (autoDomeCh < 1300)      newAutoDome = AutoDomeMode::OFF;
    else if (autoDomeCh > 1700) newAutoDome = AutoDomeMode::MORE_FREQUENT;

    // 4. Master Volume Dial S2 (CH8: 1000..2000us -> 0..99%)
    int32_t volPct = map((long)constrain(channelsUs[RC_CH_MASTER_VOLUME], 1000, 2000), 1000, 2000, 0, 99);
    uint8_t newVolume = (uint8_t)constrain(volPct, 0, 99);

    bool      armedNow           = false;
    bool      safeSeen           = false;
    bool      justArmedWithNonSlow = false;
    SpeedMode effectiveSpeedMode = SpeedMode::SLOW;

    if (lockState(pdMS_TO_TICKS(2))) {
        g_robotState.lastValidCrsfMs = nowMs;
        g_robotState.rcLinkUp        = true;

        for (uint8_t i = 1; i <= 16; i++) {
            g_robotState.rcChannelsUs[i] = channelsUs[i];
        }

        if (switchIsDisarmed) {
            g_robotState.armSwitchSafeSeen = true;
            g_robotState.isArmed           = false;
        } else if (switchRequestedArm && g_robotState.armSwitchSafeSeen) {
            g_robotState.isArmed           = true;
        } else if (!switchRequestedArm) {
            g_robotState.isArmed           = false;
        }

        armedNow = g_robotState.isArmed;
        safeSeen = g_robotState.armSwitchSafeSeen;

        // Enforce Slow-Start Safety Interlock on Disarmed -> Armed transition:
        // Always start in SLOW (30%) when arming until operator changes Switch SB.
        if (!armedNow) {
            _speedLockedToSlow = false;
            effectiveSpeedMode = SpeedMode::SLOW;
        } else {
            if (!_prevArmed) {
                // Transitioned from Disarmed -> Armed right now
                _armLatchSpeedSwitch = rawSpeedSwitch;
                if (rawSpeedSwitch != SpeedMode::SLOW) {
                    _speedLockedToSlow   = true;
                    justArmedWithNonSlow = true;
                } else {
                    _speedLockedToSlow   = false;
                }
                effectiveSpeedMode = SpeedMode::SLOW;
            } else if (_speedLockedToSlow) {
                // Remain locked in SLOW until operator moves Switch SB to a different position
                if (rawSpeedSwitch != _armLatchSpeedSwitch) {
                    _speedLockedToSlow = false;
                    effectiveSpeedMode = rawSpeedSwitch;
                } else {
                    effectiveSpeedMode = SpeedMode::SLOW;
                }
            } else {
                effectiveSpeedMode = rawSpeedSwitch;
            }
        }

        g_robotState.speedMode    = effectiveSpeedMode;
        g_robotState.autoDomeMode = newAutoDome;
        g_robotState.masterVolume = newVolume;
        unlockState();
    }

    // Warn once if operator connected with SE already flipped to ARMED
    if (switchRequestedArm && !safeSeen && !_warnedBootArm) {
        _warnedBootArm = true;
        Serial.println("[SAFE] Boot Interlock Active: Switch SE is ARMED on connect! Cycle SE to DISARMED first.");
    }

    // Log Arm state transitions
    if (armedNow != _prevArmed) {
        _prevArmed = armedNow;
        Serial.printf("[SAFE] Foot Drives -> %s\n", armedNow ? "ARMED" : "DISARMED (Locked at 1500us, Speed reset to SLOW)");
        if (justArmedWithNonSlow) {
            Serial.println("[SAFE] Speed Interlock: Armed with Switch SB in MED/FAST -> Starting in SLOW (30%) until SB is moved.");
        }
        if (g_commandQueue != nullptr) {
            SystemCommand vcmd = {};
            vcmd.type    = CommandType::PLAY_VOICE_PROMPT;
            vcmd.paramId = 1;
            strncpy(vcmd.text, armedNow ? "ARMED" : "DISARMED", sizeof(vcmd.text) - 1);
            xQueueSend(g_commandQueue, &vcmd, 0);
        }
    }

    // Log Speed Mode transitions
    if (effectiveSpeedMode != _prevSpeedMode) {
        _prevSpeedMode = effectiveSpeedMode;
        const char* spdStr = (effectiveSpeedMode == SpeedMode::SLOW) ? "SLOW (30%)" :
                             (effectiveSpeedMode == SpeedMode::MEDIUM) ? "MEDIUM (70%)" : "FAST (100%)";
        Serial.printf("[MODE] Drive Speed -> %s\n", spdStr);
        if (g_commandQueue != nullptr) {
            SystemCommand vcmd = {};
            vcmd.type    = CommandType::PLAY_VOICE_PROMPT;
            vcmd.paramId = 2;
            const char* vTag = (effectiveSpeedMode == SpeedMode::SLOW) ? "SPD_SLOW" :
                               (effectiveSpeedMode == SpeedMode::MEDIUM) ? "SPD_MED" : "SPD_FAST";
            strncpy(vcmd.text, vTag, sizeof(vcmd.text) - 1);
            xQueueSend(g_commandQueue, &vcmd, 0);
        }
    }

    // Log AutoDome transitions
    if (newAutoDome != _prevAutoDome) {
        _prevAutoDome = newAutoDome;
        const char* adStr = (newAutoDome == AutoDomeMode::OFF) ? "OFF" :
                            (newAutoDome == AutoDomeMode::LESS_FREQUENT) ? "LESS FREQUENT" : "MORE FREQUENT";
        Serial.printf("[MODE] AutoDome -> %s\n", adStr);
    }

    // Queue volume update when dial changes by >= 2%
    if (_prevVolume == 255 || abs((int)newVolume - (int)_prevVolume) >= 2) {
        _prevVolume = newVolume;
        if (g_commandQueue != nullptr) {
            SystemCommand cmd = {};
            cmd.type = CommandType::SET_VOLUME;
            cmd.paramValue = newVolume;
            xQueueSend(g_commandQueue, &cmd, 0);
        }
    }

    // 5. Universal Fallback Sound Trigger on CH9 (1000us Idle, 1050..2000us Active)
    uint16_t ch9Us = channelsUs[RC_CH_SOUND_FALLBACK];
    if (ch9Us < 1025) {
        _ch9Latched     = false;
        _ch9StableCount = 0;
    } else if (!_ch9Latched && ch9Us >= 1040) {
        // Require 2 consecutive consistent frames (within 3us) to avoid mid-slew triggers
        if (abs((int)ch9Us - (int)_ch9PrevUs) <= 3) {
            _ch9StableCount++;
        } else {
            _ch9StableCount = 1;
        }

        if (_ch9StableCount >= 2) {
            _ch9Latched = true;
            // Decode Sound ID 1..288 from 1050..2000us
            float norm = (float)(constrain(ch9Us, 1050, 2000) - 1050) * 287.0f / 950.0f;
            int soundId = (int)roundf(norm) + 1;
            soundId = constrain(soundId, 1, 288);

            uint8_t bank  = (uint8_t)(((soundId - 1) / 72) + 1);
            char    page  = (char)('A' + (((soundId - 1) % 72) / 24));
            uint8_t track = (uint8_t)(((soundId - 1) % 24) + 1);

            Serial.printf("[TRIG] CH9 Fallback Pulse (%u us) -> ID %d (Bank %u, Page %c, Track %u)\n",
                          ch9Us, soundId, bank, page, track);

            if (g_commandQueue != nullptr) {
                SystemCommand cmd = {};
                cmd.type  = CommandType::PLAY_SOUND;
                cmd.bank  = bank;
                cmd.page  = page;
                cmd.track = track;
                xQueueSend(g_commandQueue, &cmd, 0);
            }
        }
    }
    _ch9PrevUs = ch9Us;
}

void CrsfLink::handleCommandFrame(const uint8_t* payload, uint8_t payloadLen) {
    // Expected prefix: [Dest=0xC8, Src=0xEA, CmdOrParamId, ...]
    if (payloadLen < 3 || payload[0] != CRSF_SYNC_BYTE_FC) {
        return;
    }

    uint8_t cmdId = payload[2];

    // 1. Direct Sound Trigger from EdgeTX Lua: [0xC8, 0xEA, 0xA1, Bank, Page, Track]
    if (cmdId == CHIRP_CMD_PLAY_SOUND && payloadLen >= 6) {
        uint8_t bank = payload[3];
        char page    = (payload[4] >= 'A' && payload[4] <= 'Z') ? (char)payload[4] : (char)('A' + (payload[4] % 3));
        uint8_t track = payload[5];

        Serial.printf("[TRIG] CRSF Direct 0x32 Play -> Bank %u, Page %c, Track %u\n", bank, page, track);

        if (g_commandQueue != nullptr) {
            SystemCommand cmd = {};
            cmd.type  = CommandType::PLAY_SOUND;
            cmd.bank  = bank;
            cmd.page  = page;
            cmd.track = track;
            xQueueSend(g_commandQueue, &cmd, 0);
        }
        return;
    }

    // 2. Direct Audio Stop from EdgeTX Lua: [0xC8, 0xEA, 0xA2]
    if (cmdId == CHIRP_CMD_STOP_AUDIO) {
        Serial.println("[TRIG] CRSF Direct 0x32 Stop Audio");
        if (g_commandQueue != nullptr) {
            SystemCommand cmd = {};
            cmd.type = CommandType::STOP_AUDIO;
            xQueueSend(g_commandQueue, &cmd, 0);
        }
        return;
    }

    // 3. Direct Marcduino Dome Light Command: [0xC8, 0xEA, 0xA3, ...text...]
    if (cmdId == CHIRP_CMD_DOME_LIGHT && payloadLen >= 4) {
        SystemCommand cmd = {};
        cmd.type = CommandType::DOME_LIGHT_CMD;
        uint8_t strLen = payloadLen - 3;
        if (strLen >= sizeof(cmd.text)) strLen = sizeof(cmd.text) - 1;
        memcpy(cmd.text, &payload[3], strLen);
        cmd.text[strLen] = '\0';
        if (g_commandQueue != nullptr) {
            xQueueSend(g_commandQueue, &cmd, 0);
        }
        return;
    }

    // 4. Set Active Sound Page from EdgeTX Lua: [0xC8, 0xEA, 0xA4, PageChar]
    if (cmdId == CHIRP_CMD_SET_PAGE && payloadLen >= 4) {
        char page = (payload[3] >= 'A' && payload[3] <= AUDIO_MAX_PAGE_CHAR)
                    ? (char)payload[3] : (char)('A' + (payload[3] % AUDIO_MAX_PAGES));
        Serial.printf("[AUDIO] CRSF Active Page -> %c\n", page);
        if (lockState(pdMS_TO_TICKS(2))) {
            g_robotState.activePage = page;
            unlockState();
        }
        if (g_commandQueue != nullptr) {
            SystemCommand cmd = {};
            cmd.type = CommandType::SET_ACTIVE_PAGE;
            cmd.page = page;
            xQueueSend(g_commandQueue, &cmd, 0);
        }
        return;
    }

    // 5. Request Fresh Manifest Sync: [0xC8, 0xEA, 0xA5]
    if (cmdId == CHIRP_CMD_SYNC_MANIFEST) {
        Serial.println("[AUDIO] CRSF Requested Manifest Re-Sync (GMAN)");
        if (g_commandQueue != nullptr) {
            SystemCommand cmd = {};
            cmd.type = CommandType::SYNC_MANIFEST;
            xQueueSend(g_commandQueue, &cmd, 0);
        }
        return;
    }

    // 6. Configuration Parameter Update (IDs 1..9 from Lua Settings Screen)
    if (cmdId >= 1 && cmdId <= 9 && payloadLen >= 4) {
        uint8_t rawVal = payload[3];
        Serial.printf("[CFG] CRSF Setting Update -> Param %u = %u\n", cmdId, rawVal);

        if (g_commandQueue != nullptr) {
            SystemCommand cmd = {};
            cmd.type       = CommandType::UPDATE_SETTING;
            cmd.paramId    = cmdId;
            cmd.paramValue = (int16_t)rawVal;
            xQueueSend(g_commandQueue, &cmd, 0);
        }
    }
}

void CrsfLink::sendTelemetryFrame(uint8_t frameType, const void* payload, uint8_t payloadLen) {
    if (_port == nullptr || payloadLen > (CRSF_MAX_PACKET_LEN - 4)) return;

    uint8_t frame[CRSF_MAX_PACKET_LEN];
    frame[0] = CRSF_SYNC_BYTE_FC;
    frame[1] = payloadLen + 2; // Type (1) + Payload (N) + CRC (1)
    frame[2] = frameType;

    if (payload != nullptr && payloadLen > 0) {
        memcpy(&frame[3], payload, payloadLen);
    }

    frame[3 + payloadLen] = crc8(&frame[2], payloadLen + 1);

    if (_txMutex != nullptr) {
        if (xSemaphoreTake(_txMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            _port->write(frame, payloadLen + 4);
            _txFrameCount++;
            xSemaphoreGive(_txMutex);
        }
    } else {
        _port->write(frame, payloadLen + 4);
        _txFrameCount++;
    }
}
