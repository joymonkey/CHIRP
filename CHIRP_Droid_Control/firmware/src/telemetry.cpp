#include "telemetry.h"
#include "crsf_link.h"
#include "audio_link.h"

extern RobotState  g_robotState;
extern RobotConfig g_robotConfig;
extern bool        lockState(TickType_t timeoutTicks);
extern void        unlockState();

// --- Battery ADC1 Monitor State ---
static uint32_t s_lastBattSampleMs = 0;
static bool     s_battInitialized  = false;
static bool     s_batt1LowWarned   = false;
static float    s_emaBatt1         = 0.0f;
static float    s_emaBatt2         = 0.0f;
static float    s_emaBatt3         = 0.0f;

// --- CRSF Telemetry Scheduler State ---
static uint32_t s_lastTelemMs      = 0;
static uint8_t  s_telemSlot        = 0; // 0=Battery, 1=GPS/Status, 2=FlightMode(Names)
static uint8_t  s_battCycleIdx     = 0; // 0=Batt1, 1=Batt2(+100V), 2=Batt3(+200V)

// --- Flight Mode Manifest Streamer Cursors ---
static uint8_t  s_syncBank         = 1; // 1..4
static uint8_t  s_syncPage         = 0; // 0..2 ('A'..'C')
static int8_t   s_syncStep         = -1;// -1 = send count ('#'), 0 = send page name ('0'), 1..24 = track name

static inline uint16_t toBigEndian16(uint16_t val) {
    return (uint16_t)((val >> 8) | (val << 8));
}

static inline int32_t toBigEndian32(int32_t val) {
    uint32_t u = (uint32_t)val;
    return (int32_t)(((u & 0x000000FFUL) << 24) |
                     ((u & 0x0000FF00UL) << 8)  |
                     ((u & 0x00FF0000UL) >> 8)  |
                     ((u & 0xFF000000UL) >> 24));
}

static float readAdcPinVolts(int8_t pin) {
    uint32_t mv = analogReadMilliVolts(pin);
    float volts = ((float)mv / 1000.0f) * BATT_DIVIDER_RATIO;
    // Suppress unconnected pin noise below 0.8V
    if (volts < 0.8f) {
        return 0.0f;
    }
    return volts;
}

static void sampleBatteriesAdc1() {
    float v1 = readAdcPinVolts(PIN_ADC_BATT1);
    float v2 = readAdcPinVolts(PIN_ADC_BATT2);
    float v3 = readAdcPinVolts(PIN_ADC_BATT3);

    if (!s_battInitialized) {
        s_emaBatt1        = v1;
        s_emaBatt2        = v2;
        s_emaBatt3        = v3;
        s_battInitialized = true;
    } else {
        constexpr float ALPHA = 0.25f;
        s_emaBatt1 = (v1 == 0.0f) ? 0.0f : (ALPHA * v1 + (1.0f - ALPHA) * s_emaBatt1);
        s_emaBatt2 = (v2 == 0.0f) ? 0.0f : (ALPHA * v2 + (1.0f - ALPHA) * s_emaBatt2);
        s_emaBatt3 = (v3 == 0.0f) ? 0.0f : (ALPHA * v3 + (1.0f - ALPHA) * s_emaBatt3);
    }

    if (lockState(pdMS_TO_TICKS(2))) {
        g_robotState.battery1Volts = s_emaBatt1;
        g_robotState.battery2Volts = s_emaBatt2;
        g_robotState.battery3Volts = s_emaBatt3;
        unlockState();
    }

    // Low Primary Battery Voice Alert (only when battery is actually connected > 5.0V)
    if (s_emaBatt1 > 5.0f && s_emaBatt1 < BATT1_LOW_WARN_VOLTS && !s_batt1LowWarned) {
        s_batt1LowWarned = true;
        Serial.printf("[PWR] WARNING: Primary Battery 1 Low (%.1fV)!\n", s_emaBatt1);
        audioDispatchVoiceTag(0, "BATT_LOW");
    } else if (s_emaBatt1 >= BATT1_LOW_RESET_VOLTS || s_emaBatt1 < 2.0f) {
        s_batt1LowWarned = false;
    }
}

static void sendBatteryTelemetry(const RobotState& snap) {
    float voltageToSend = 0.0f;
    if (s_battCycleIdx == 0) {
        voltageToSend = snap.battery1Volts;
    } else if (s_battCycleIdx == 1) {
        voltageToSend = snap.battery2Volts + 100.0f; // 100..199.9V encodes Battery 2
    } else {
        voltageToSend = snap.battery3Volts + 200.0f; // 200..299.9V encodes Battery 3
    }
    s_battCycleIdx = (s_battCycleIdx + 1) % 3;

    uint16_t voltDeci = (uint16_t)constrain((int)roundf(voltageToSend * 10.0f), 0, 65535);
    uint16_t currDeci = (uint16_t)constrain((int)roundf(snap.currentDrawAmps * 10.0f), 0, 65535);
    uint32_t peakDeci = (uint32_t)constrain((int)roundf(snap.peakCurrentAmps * 10.0f), 0, 0xFFFFFF);

    CrsfBatterySensor pkt = {};
    pkt.voltage_dV      = toBigEndian16(voltDeci);
    pkt.current_dA      = toBigEndian16(currDeci);
    pkt.capacity_mAh[0] = (uint8_t)((peakDeci >> 16) & 0xFF);
    pkt.capacity_mAh[1] = (uint8_t)((peakDeci >> 8)  & 0xFF);
    pkt.capacity_mAh[2] = (uint8_t)(peakDeci & 0xFF);
    pkt.remaining_pct   = 100;

    g_crsfLink.sendTelemetryFrame(CRSF_FRAMETYPE_BATTERY_SENSOR, &pkt, sizeof(pkt));
}

static void sendGpsStatusTelemetry(const RobotState& snap) {
    CrsfGpsSensor pkt = {};

    // Encode Dome Heading (0..359 deg) into latitude * 10,000,000 so Lua can read it via GPS or Hdg
    pkt.latitude    = toBigEndian32((int32_t)snap.domeHeadingDeg * 10000000L);
    pkt.longitude   = toBigEndian32((int32_t)(g_robotConfig.bank1Page - 'A') * 10000000L);

    // Master Volume (0..99%) in groundspeed (0.1 km/h units -> EdgeTX reads GSpd as 0..99)
    pkt.groundspeed = toBigEndian16((uint16_t)snap.masterVolume * 10);

    // Packed Status Bitfield in Satellites (Sats):
    //   Bits 0-1: Effective Drive Speed (0=Disarmed, 1=Slow, 2=Med, 3=Fast)
    //   Bit 2 (0x04): AutoDome enabled
    //   Bit 3 (0x08): AutoChirp enabled
    //   Bits 4-5 (0x30): AutoDome mode (0=Off, 1=Less, 2=More)
    uint8_t sats = snap.isArmed ? (uint8_t)snap.speedMode : 0;
    if (snap.autoDomeMode != AutoDomeMode::OFF) sats |= 0x04;
    if (snap.autoChirpEnabled)                  sats |= 0x08;
    sats |= (((uint8_t)snap.autoDomeMode & 0x03) << 4);
    pkt.satellites = sats;

    // Sound Counts for Active Page:
    //   Heading (Hdg): Banks 1 & 2 (bits 0..4 = Bank 1, bits 5..9 = Bank 2)
    //   Altitude (GAlt): Banks 3 & 4 (bits 0..4 = Bank 3, bits 5..9 = Bank 4) + 1000m CRSF offset
    uint8_t pageIdx = (snap.activePage >= 'A' && snap.activePage <= AUDIO_MAX_PAGE_CHAR)
                      ? (uint8_t)(snap.activePage - 'A') : 0;
    uint8_t b1PageIdx = (g_robotConfig.bank1Page >= 'A' && g_robotConfig.bank1Page <= AUDIO_MAX_PAGE_CHAR)
                        ? (uint8_t)(g_robotConfig.bank1Page - 'A') : 0;

    uint16_t c1 = audioGetSoundCount(1, b1PageIdx) & 0x1F;
    uint16_t c2 = audioGetSoundCount(2, pageIdx)   & 0x1F;
    uint16_t c3 = audioGetSoundCount(3, pageIdx)   & 0x1F;
    uint16_t c4 = audioGetSoundCount(4, pageIdx)   & 0x1F;

    uint16_t hdgPayload = c1 | (c2 << 5);
    uint16_t altPayload = c3 | (c4 << 5);

    pkt.heading  = toBigEndian16(hdgPayload);
    pkt.altitude = toBigEndian16(altPayload + 1000);

    g_crsfLink.sendTelemetryFrame(CRSF_FRAMETYPE_GPS, &pkt, sizeof(pkt));
}

static void advanceSyncCursor() {
    s_syncStep = -1;
    for (uint8_t tries = 0; tries < AUDIO_MAX_BANKS * AUDIO_MAX_PAGES; tries++) {
        s_syncPage++;
        if (s_syncPage >= AUDIO_MAX_PAGES) {
            s_syncPage = 0;
            s_syncBank++;
            if (s_syncBank > AUDIO_MAX_BANKS) {
                s_syncBank = 1;
            }
        }
        // Always sync pages A..C (0..2), and sync pages D..F (3..5) whenever they contain sounds
        if (s_syncPage < 3 || audioGetSoundCount(s_syncBank, s_syncPage) > 0) {
            break;
        }
    }
}

static void sendFlightModeNameTelemetry() {
    char fmString[20] = {0};
    char pageChar = (char)('A' + s_syncPage);
    uint8_t count = audioGetSoundCount(s_syncBank, s_syncPage);

    if (s_syncStep < 0) {
        // Step -1: Broadcast track count for (s_syncBank, s_syncPage) -> e.g. "1A#12"
        snprintf(fmString, sizeof(fmString), "%u%c#%u",
                 (unsigned)s_syncBank, pageChar, (unsigned)count);
        s_syncStep = 0;
    } else if (s_syncStep == 0) {
        // Step 0: Broadcast page name -> e.g. "1A0Main"
        const char* pName = audioGetPageName(s_syncBank, s_syncPage);
        snprintf(fmString, sizeof(fmString), "%u%c0%.11s",
                 (unsigned)s_syncBank, pageChar, pName);
        s_syncStep = 1;
    } else {
        // Step 1..count: Broadcast track name -> e.g. "1A1Whistle"
        uint8_t trackIdx = (uint8_t)s_syncStep;
        char trackChar = (trackIdx <= 9) ? (char)('0' + trackIdx) : (char)('a' + (trackIdx - 10));
        const char* sName = audioGetSoundName(s_syncBank, s_syncPage, trackIdx);
        snprintf(fmString, sizeof(fmString), "%u%c%c%.11s",
                 (unsigned)s_syncBank, pageChar, trackChar, sName);
        s_syncStep++;
    }

    uint8_t payloadLen = (uint8_t)(strlen(fmString) + 1); // Include null terminator per CRSF 0x21 spec
    g_crsfLink.sendTelemetryFrame(CRSF_FRAMETYPE_FLIGHT_MODE, fmString, payloadLen);

    // Advance cursor to next page/bank when all tracks in current page are sent
    if (s_syncStep > (int8_t)count || s_syncStep > (int8_t)AUDIO_MAX_SOUNDS) {
        advanceSyncCursor();
    }
}

void telemetryInit() {
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_ADC_BATT1, ADC_11db);
    analogSetPinAttenuation(PIN_ADC_BATT2, ADC_11db);
    analogSetPinAttenuation(PIN_ADC_BATT3, ADC_11db);

    sampleBatteriesAdc1();
    s_lastBattSampleMs = millis();
    s_lastTelemMs      = millis();

    Serial.printf("[TLM] Battery ADC1 Monitor (GP%d, GP%d, GP%d) & CRSF Telemetry Uplink initialized\n",
                  PIN_ADC_BATT1, PIN_ADC_BATT2, PIN_ADC_BATT3);
    Serial.flush();
}

void telemetryUpdate() {
    uint32_t nowMs = millis();

    // 1. Sample 3x Battery ADCs on ADC1 every 250ms (4Hz)
    if (nowMs - s_lastBattSampleMs >= BATT_SAMPLE_PERIOD_MS) {
        s_lastBattSampleMs = nowMs;
        sampleBatteriesAdc1();
    }

    // 2. Transmit CRSF Telemetry Frames when RC Link is Up
    if (nowMs - s_lastTelemMs >= TELEMETRY_SLOT_PERIOD_MS) {
        s_lastTelemMs = nowMs;

        RobotState snap;
        if (lockState(pdMS_TO_TICKS(2))) {
            snap = g_robotState;
            unlockState();
        }

        if (!snap.rcLinkUp) {
            return;
        }

        if (s_telemSlot == 0) {
            sendBatteryTelemetry(snap);
        } else if (s_telemSlot == 1) {
            sendGpsStatusTelemetry(snap);
        } else {
            sendFlightModeNameTelemetry();
        }

        s_telemSlot = (s_telemSlot + 1) % 3;
    }
}

