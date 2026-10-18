#include "storage_manager.h"
#include "audio_link.h"
#include <Preferences.h>

extern RobotConfig g_robotConfig;

static Preferences s_prefs;
static constexpr const char* NVS_NAMESPACE = "chirp_cfg";

void storageManagerInit() {
    if (!s_prefs.begin(NVS_NAMESPACE, false)) {
        Serial.println("[NVS] Warning: Failed to open Preferences namespace, using defaults.");
        return;
    }

    g_robotConfig.bank1Page        = (char)s_prefs.getUChar("b1page", 'A');
    if (g_robotConfig.bank1Page < 'A' || g_robotConfig.bank1Page > AUDIO_MAX_PAGE_CHAR) {
        g_robotConfig.bank1Page = 'A';
    }
    g_robotConfig.domeOffset       = s_prefs.getChar("domeOff", 0);
    g_robotConfig.domeInvert       = s_prefs.getBool("domeInv", false);
    g_robotConfig.voiceDebug       = s_prefs.getBool("voiceDbg", true);
    g_robotConfig.leftMotorInvert  = s_prefs.getBool("lMotInv", false);
    g_robotConfig.rightMotorInvert = s_prefs.getBool("rMotInv", false);
    g_robotConfig.speedSlowPct     = (uint8_t)constrain(s_prefs.getUChar("spdSlow", 30), 0, 100);
    g_robotConfig.speedMedPct      = (uint8_t)constrain(s_prefs.getUChar("spdMed",  70), 0, 100);
    g_robotConfig.speedFastPct     = (uint8_t)constrain(s_prefs.getUChar("spdFast", 100), 0, 100);

    Serial.printf("[NVS] Config loaded (B1Page=%c, DomeOff=%d, DomeInv=%u, Voice=%u, LInv=%u, RInv=%u, Spd=%u/%u/%u%%)\n",
                  g_robotConfig.bank1Page,
                  (int)g_robotConfig.domeOffset,
                  g_robotConfig.domeInvert ? 1 : 0,
                  g_robotConfig.voiceDebug ? 1 : 0,
                  g_robotConfig.leftMotorInvert ? 1 : 0,
                  g_robotConfig.rightMotorInvert ? 1 : 0,
                  g_robotConfig.speedSlowPct,
                  g_robotConfig.speedMedPct,
                  g_robotConfig.speedFastPct);
}

void storageManagerSave() {
    s_prefs.putUChar("b1page",  (uint8_t)g_robotConfig.bank1Page);
    s_prefs.putChar("domeOff",  g_robotConfig.domeOffset);
    s_prefs.putBool("domeInv",  g_robotConfig.domeInvert);
    s_prefs.putBool("voiceDbg", g_robotConfig.voiceDebug);
    s_prefs.putBool("lMotInv",  g_robotConfig.leftMotorInvert);
    s_prefs.putBool("rMotInv",  g_robotConfig.rightMotorInvert);
    s_prefs.putUChar("spdSlow", g_robotConfig.speedSlowPct);
    s_prefs.putUChar("spdMed",  g_robotConfig.speedMedPct);
    s_prefs.putUChar("spdFast", g_robotConfig.speedFastPct);
    Serial.println("[NVS] Configuration saved to flash.");
}

void storageManagerUpdateSetting(uint8_t paramId, int16_t rawVal) {
    bool changed = true;
    switch (paramId) {
        case 1: {
            char page = (rawVal >= 'A' && rawVal <= AUDIO_MAX_PAGE_CHAR) ? (char)rawVal : 'A';
            g_robotConfig.bank1Page = page;
            audioSetBank1Page(page);
            break;
        }
        case 2:
            g_robotConfig.domeOffset = (int8_t)((uint8_t)rawVal);
            break;
        case 3:
            g_robotConfig.domeInvert = (rawVal > 0);
            break;
        case 4:
            g_robotConfig.voiceDebug = (rawVal > 0);
            break;
        case 5:
            g_robotConfig.leftMotorInvert = (rawVal > 0);
            break;
        case 6:
            g_robotConfig.rightMotorInvert = (rawVal > 0);
            break;
        case 7:
            g_robotConfig.speedSlowPct = (uint8_t)constrain(rawVal, 0, 100);
            break;
        case 8:
            g_robotConfig.speedMedPct = (uint8_t)constrain(rawVal, 0, 100);
            break;
        case 9:
            g_robotConfig.speedFastPct = (uint8_t)constrain(rawVal, 0, 100);
            break;
        default:
            changed = false;
            break;
    }

    if (changed) {
        storageManagerSave();
    }
}

