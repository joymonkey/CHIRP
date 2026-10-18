#include "audio_link.h"
#include <stdarg.h>

extern RobotState  g_robotState;
extern RobotConfig g_robotConfig;
extern bool        lockState(TickType_t timeoutTicks);
extern void        unlockState();

AudioManifest  g_audioManifest;
AudioSyncState g_audioSyncState = AudioSyncState::IDLE;

// --- Non-blocking UART2 Line Parser State ---
static char     s_rxLineBuf[128];
static uint8_t  s_rxLinePos       = 0;

// --- Manifest Sync State Machine ---
static uint32_t s_syncTimerMs     = 0;
static uint8_t  s_syncBankIdx     = 0; // 0..3
static uint8_t  s_syncPageIdx     = 0; // 0..2
static uint8_t  s_syncSoundIdx    = 1; // 1..24
static uint8_t  s_gnmeRetryCount  = 0;
static uint32_t s_gmanRetryCount  = 0;

// --- Vocal Feedback (PVOICE) Priority State Machine ---
enum class VoiceState : uint8_t {
    IDLE = 0,
    WAITING_ACK,
    WORD_GAP
};

static constexpr uint8_t VOICE_QUEUE_CAP = 16;
static const char* s_voiceQueue[VOICE_QUEUE_CAP];
static uint8_t     s_voiceHead           = 0;
static uint8_t     s_voiceTail           = 0;
static VoiceState  s_voiceState          = VoiceState::IDLE;
static bool        s_voiceAckReceived    = false;
static uint32_t    s_voiceStateMs        = 0;
static uint32_t    s_lastVoicePrioMs[4]  = {0, 0, 0, 0};
static uint8_t     s_activeVoicePriority = 3;

static void clearVoiceQueue() {
    s_voiceHead = 0;
    s_voiceTail = 0;
}

static bool pushVoiceWord(const char* word) {
    uint8_t next = (s_voiceHead + 1) % VOICE_QUEUE_CAP;
    if (next == s_voiceTail) return false;
    s_voiceQueue[s_voiceHead] = word;
    s_voiceHead = next;
    return true;
}

static const char* popVoiceWord() {
    if (s_voiceHead == s_voiceTail) return nullptr;
    const char* word = s_voiceQueue[s_voiceTail];
    s_voiceTail = (s_voiceTail + 1) % VOICE_QUEUE_CAP;
    return word;
}

static bool isVoiceQueueEmpty() {
    return (s_voiceHead == s_voiceTail);
}

static void resetManifestDefaults() {
    for (uint8_t b = 0; b < AUDIO_MAX_BANKS; b++) {
        for (uint8_t p = 0; p < AUDIO_MAX_PAGES; p++) {
            g_audioManifest.banks[b].pages[p].soundCount = 0;
            snprintf(g_audioManifest.banks[b].pages[p].name,
                     sizeof(g_audioManifest.banks[b].pages[p].name),
                     "Pg%c", (char)('A' + p));
            for (uint8_t s = 0; s < AUDIO_MAX_SOUNDS; s++) {
                snprintf(g_audioManifest.banks[b].pages[p].sounds[s].name,
                         sizeof(g_audioManifest.banks[b].pages[p].sounds[s].name),
                         "Snd %u", (unsigned)(s + 1));
            }
        }
    }
}

static void requestNextSoundName() {
    while (s_syncBankIdx < AUDIO_MAX_BANKS) {
        while (s_syncPageIdx < AUDIO_MAX_PAGES) {
            if (s_syncSoundIdx == 0) s_syncSoundIdx = 1;

            uint8_t count = g_audioManifest.banks[s_syncBankIdx].pages[s_syncPageIdx].soundCount;
            if (s_syncSoundIdx <= count && s_syncSoundIdx <= AUDIO_MAX_SOUNDS) {
                char pageChar = (char)('A' + s_syncPageIdx);
                Serial2.printf("GNME:%u,%c,%u\n",
                               (unsigned)(s_syncBankIdx + 1),
                               pageChar,
                               (unsigned)s_syncSoundIdx);
                s_syncTimerMs = millis();
                return;
            } else {
                s_syncSoundIdx   = 1;
                s_gnmeRetryCount = 0;
                s_syncPageIdx++;
            }
        }
        s_syncPageIdx = 0;
        s_syncBankIdx++;
    }

    // All banks and pages have been synced!
    g_audioSyncState = AudioSyncState::COMPLETE;
    Serial.println("[AUDIO] Manifest Sync COMPLETE.");
    audioSayPriority(2, "sync", "complete", nullptr);
}

static void parseAudioLine(char* line) {
    // Trim trailing whitespace
    int len = (int)strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ')) {
        line[--len] = '\0';
    }
    if (len == 0) return;

    // 1. Voice ACK: PACK:PVOICE
    if (strncmp(line, "PACK:PVOICE", 11) == 0) {
        s_voiceAckReceived = true;
        return;
    }

    // 2. Manifest Header: MDAT:...
    if (strncmp(line, "MDAT:", 5) == 0) {
        Serial.printf("[AUDIO] RX Manifest Header: %s\n", line);
        return;
    }

    // 3. Bank Page Entry: BANK:<bank>,<pageStr>,<count>
    if (strncmp(line, "BANK:", 5) == 0) {
        char* p1 = strchr(line + 5, ',');
        if (!p1) return;
        *p1 = '\0';
        int bankNum = atoi(line + 5);

        char* pageStr = p1 + 1;
        char* p2 = strchr(pageStr, ',');
        if (!p2) return;
        *p2 = '\0';
        int count = atoi(p2 + 1);

        char pageChar = 'A';
        char* underscore = strchr(pageStr, '_');
        if (underscore != nullptr && underscore > pageStr) {
            char before = *(underscore - 1);
            if (before >= 'A' && before <= 'Z') pageChar = before;
        } else if (pageStr[0] >= 'A' && pageStr[0] <= 'Z') {
            pageChar = pageStr[0];
        }

        int pIdx = pageChar - 'A';
        if (bankNum >= 1 && bankNum <= AUDIO_MAX_BANKS && pIdx >= 0 && pIdx < AUDIO_MAX_PAGES) {
            uint8_t clampedCount = (uint8_t)constrain(count, 0, (int)AUDIO_MAX_SOUNDS);
            g_audioManifest.banks[bankNum - 1].pages[pIdx].soundCount = clampedCount;

            const char* cleanPageName = pageStr;
            if (underscore != nullptr) {
                cleanPageName = (*(underscore + 1) != '\0') ? (underscore + 1) : "";
            }
            strncpy(g_audioManifest.banks[bankNum - 1].pages[pIdx].name,
                    cleanPageName,
                    AUDIO_MAX_NAME_LEN);
            g_audioManifest.banks[bankNum - 1].pages[pIdx].name[AUDIO_MAX_NAME_LEN] = '\0';
        }
        return;
    }

    // 4. Manifest Structure End: MEND
    if (strncmp(line, "MEND", 4) == 0) {
        Serial.println("[AUDIO] Manifest structure received (MEND). Fetching track names...");
        s_syncBankIdx    = 0;
        s_syncPageIdx    = 0;
        s_syncSoundIdx   = 1;
        s_gnmeRetryCount = 0;
        g_audioSyncState = AudioSyncState::FETCHING_NAMES;
        requestNextSoundName();
        return;
    }

    // 5. Track Name Response: NAME:<bank>,<page>,<index>,<name>
    if (strncmp(line, "NAME:", 5) == 0) {
        char* p1 = strchr(line + 5, ',');
        if (!p1) return;
        *p1 = '\0';
        int bankNum = atoi(line + 5);

        char* pageStr = p1 + 1;
        char* p2 = strchr(pageStr, ',');
        if (!p2) return;
        *p2 = '\0';

        char* idxStr = p2 + 1;
        char* p3 = strchr(idxStr, ',');
        if (!p3) return;
        *p3 = '\0';
        int trackIdx = atoi(idxStr);

        char* nameStr = p3 + 1;
        // Strip file extension if present
        char* dot = strrchr(nameStr, '.');
        if (dot != nullptr && dot > nameStr) {
            *dot = '\0';
        }

        char pageChar = (pageStr[0] >= 'A' && pageStr[0] <= 'Z') ? pageStr[0] : 'A';
        int pIdx = pageChar - 'A';

        if (bankNum >= 1 && bankNum <= AUDIO_MAX_BANKS &&
            pIdx >= 0 && pIdx < AUDIO_MAX_PAGES &&
            trackIdx >= 1 && trackIdx <= AUDIO_MAX_SOUNDS) {
            strncpy(g_audioManifest.banks[bankNum - 1].pages[pIdx].sounds[trackIdx - 1].name,
                    nameStr,
                    AUDIO_MAX_NAME_LEN);
            g_audioManifest.banks[bankNum - 1].pages[pIdx].sounds[trackIdx - 1].name[AUDIO_MAX_NAME_LEN] = '\0';
        }

        if (g_audioSyncState == AudioSyncState::FETCHING_NAMES) {
            s_gnmeRetryCount = 0;
            s_syncSoundIdx++;
            requestNextSoundName();
        }
        return;
    }
}

void audioLinkInit() {
    resetManifestDefaults();

    // Enable pull-up on RX pin so an unconnected audio harness never floats
    pinMode(PIN_AUDIO_RX, INPUT_PULLUP);
    Serial2.setRxBufferSize(512);
    Serial2.begin(AUDIO_BAUD_RATE, SERIAL_8N1, PIN_AUDIO_RX, PIN_AUDIO_TX);

    Serial.printf("[AUDIO] CHIRP_Audio UART2 initialized (RX=GP%d, TX=GP%d @ %lu baud)\n",
                  PIN_AUDIO_RX, PIN_AUDIO_TX, (unsigned long)AUDIO_BAUD_RATE);
    Serial.flush();

    // Send initial Bank 1 Page and Max Streams config, then request sound manifest
    audioSetBank1Page(g_robotConfig.bank1Page);
    audioSetMaxStreams(g_robotConfig.maxAudioStreams);
    audioRequestManifestSync();

    // Queue boot voice announcement
    audioSayPriority(1, "chirp", "control", "ready", nullptr);
}

void audioRequestManifestSync() {
    Serial2.println("GMAN");
    g_audioSyncState = AudioSyncState::REQUESTING_MANIFEST;
    s_syncTimerMs    = millis();
    s_gmanRetryCount = 0;
    Serial.println("[AUDIO] TX -> GMAN (Requesting sound manifest)");
}

void audioLinkUpdate() {
    uint32_t nowMs = millis();

    // 1. Non-blocking drain of Serial2 RX buffer
    while (Serial2.available() > 0) {
        char c = (char)Serial2.read();
        if (c == '\n' || c == '\r') {
            if (s_rxLinePos > 0) {
                s_rxLineBuf[s_rxLinePos] = '\0';
                parseAudioLine(s_rxLineBuf);
                s_rxLinePos = 0;
            }
        } else if (s_rxLinePos < sizeof(s_rxLineBuf) - 1) {
            s_rxLineBuf[s_rxLinePos++] = c;
        } else {
            s_rxLinePos = 0; // Overflow protection
        }
    }

    // 2. Manifest Sync Timeout & Retry State Machine
    if (g_audioSyncState == AudioSyncState::REQUESTING_MANIFEST) {
        if (nowMs - s_syncTimerMs >= AUDIO_GMAN_RETRY_MS) {
            s_syncTimerMs = nowMs;
            s_gmanRetryCount++;
            Serial2.println("GMAN");
            if (s_gmanRetryCount <= 3 || (s_gmanRetryCount % 12 == 0)) {
                Serial.printf("[AUDIO] Waiting for CHIRP_Audio manifest (GMAN retry #%lu)...\n",
                              (unsigned long)s_gmanRetryCount);
            }
        }
    } else if (g_audioSyncState == AudioSyncState::FETCHING_NAMES) {
        if (nowMs - s_syncTimerMs >= AUDIO_GNME_TIMEOUT_MS) {
            s_gnmeRetryCount++;
            if (s_gnmeRetryCount > AUDIO_GNME_MAX_RETRIES) {
                Serial.printf("[AUDIO] GNME timeout on B%u P%c T%u -> skipping to next track\n",
                              (unsigned)(s_syncBankIdx + 1),
                              (char)('A' + s_syncPageIdx),
                              (unsigned)s_syncSoundIdx);
                s_gnmeRetryCount = 0;
                s_syncSoundIdx++;
            }
            requestNextSoundName();
        }
    }

    // 3. Vocal Status Feedback (PVOICE) State Machine
    switch (s_voiceState) {
        case VoiceState::IDLE:
            if (!isVoiceQueueEmpty()) {
                const char* word = popVoiceWord();
                if (word != nullptr) {
                    Serial2.printf("PVOICE:%s\n", word);
                    s_voiceAckReceived = false;
                    s_voiceState       = VoiceState::WAITING_ACK;
                    s_voiceStateMs     = nowMs;
                }
            }
            break;

        case VoiceState::WAITING_ACK:
            if (s_voiceAckReceived || (nowMs - s_voiceStateMs >= VOICE_WORD_TIMEOUT_MS)) {
                s_voiceState   = VoiceState::WORD_GAP;
                s_voiceStateMs = nowMs;
            }
            break;

        case VoiceState::WORD_GAP:
            if (nowMs - s_voiceStateMs >= VOICE_WORD_GAP_MS) {
                s_voiceState = VoiceState::IDLE;
            }
            break;
    }
}

void audioPlaySound(uint8_t bank, char pageChar, uint8_t track, uint8_t volume) {
    uint8_t clampedBank = (uint8_t)constrain(bank, 1, AUDIO_MAX_BANKS);
    char    clampedPage = (pageChar >= 'A' && pageChar <= AUDIO_MAX_PAGE_CHAR) ? pageChar : 'A';
    // Bank 1 uses its configured page (default 'A')
    if (clampedBank == 1) {
        clampedPage = (g_robotConfig.bank1Page >= 'A' && g_robotConfig.bank1Page <= AUDIO_MAX_PAGE_CHAR)
                      ? g_robotConfig.bank1Page : 'A';
    }
    uint8_t clampedVol = (uint8_t)constrain(volume, 0, 99);

    Serial2.printf("PLAY:%u,%u,%c,%u\n",
                   (unsigned)track,
                   (unsigned)clampedBank,
                   clampedPage,
                   (unsigned)clampedVol);
    Serial.printf("[AUDIO] TX -> PLAY:%u,%u,%c,%u\n",
                  (unsigned)track,
                  (unsigned)clampedBank,
                  clampedPage,
                  (unsigned)clampedVol);
}

void audioStopAll() {
    Serial2.println("STOP");
    Serial.println("[AUDIO] TX -> STOP");
}

void audioSetVolume(uint8_t volume) {
    uint8_t clampedVol = (uint8_t)constrain(volume, 0, 99);
    Serial2.printf("VOL:%u\n", (unsigned)clampedVol);
    Serial.printf("[AUDIO] TX -> VOL:%u\n", (unsigned)clampedVol);
}

void audioSetBank1Page(char pageChar) {
    char p = (pageChar >= 'A' && pageChar <= AUDIO_MAX_PAGE_CHAR) ? pageChar : 'A';
    Serial2.printf("BPAGE:%c\n", p);
    Serial.printf("[AUDIO] TX -> BPAGE:%c\n", p);
}

void audioSetMaxStreams(uint8_t maxStreams) {
    uint8_t clamped = (uint8_t)constrain(maxStreams, 1, 10);
    Serial2.printf("STRM:%u\n", (unsigned)clamped);
    Serial.printf("[AUDIO] TX -> STRM:%u\n", (unsigned)clamped);
}

void audioSendRawCommand(const char* cmd) {
    if (cmd == nullptr || cmd[0] == '\0') return;
    Serial2.println(cmd);
    Serial.printf("[AUDIO] TX -> %s\n", cmd);
}

uint8_t audioGetSoundCount(uint8_t bank, uint8_t pageIdx) {
    if (bank < 1 || bank > AUDIO_MAX_BANKS || pageIdx >= AUDIO_MAX_PAGES) return 0;
    return g_audioManifest.banks[bank - 1].pages[pageIdx].soundCount;
}

const char* audioGetPageName(uint8_t bank, uint8_t pageIdx) {
    if (bank < 1 || bank > AUDIO_MAX_BANKS || pageIdx >= AUDIO_MAX_PAGES) return "ErrP";
    return g_audioManifest.banks[bank - 1].pages[pageIdx].name;
}

const char* audioGetSoundName(uint8_t bank, uint8_t pageIdx, uint8_t track) {
    if (bank < 1 || bank > AUDIO_MAX_BANKS || pageIdx >= AUDIO_MAX_PAGES ||
        track < 1 || track > AUDIO_MAX_SOUNDS) {
        return "ErrS";
    }
    return g_audioManifest.banks[bank - 1].pages[pageIdx].sounds[track - 1].name;
}

void audioSayPriority(uint8_t priority, const char* word1, ...) {
    if (!g_robotConfig.voiceDebug || word1 == nullptr) return;
    if (priority > 3) priority = 3;

    uint32_t nowMs = millis();
    if ( s_lastVoicePrioMs[priority] != 0 &&
        (nowMs - s_lastVoicePrioMs[priority] < VOICE_COOLDOWN_MS)) {
        return;
    }

    if (s_voiceState != VoiceState::IDLE || !isVoiceQueueEmpty()) {
        if (priority < s_activeVoicePriority) {
            clearVoiceQueue();
            s_voiceState = VoiceState::IDLE;
        } else {
            return;
        }
    }

    s_lastVoicePrioMs[priority] = nowMs;
    s_activeVoicePriority       = priority;

    va_list args;
    va_start(args, word1);
    const char* w = word1;
    while (w != nullptr) {
        pushVoiceWord(w);
        w = va_arg(args, const char*);
    }
    va_end(args);
}

void audioDispatchVoiceTag(uint8_t priority, const char* tag) {
    if (tag == nullptr) return;

    if (strcmp(tag, "LINK_UP") == 0) {
        audioSayPriority(priority, "receiver", "connected", nullptr);
    } else if (strcmp(tag, "LINK_DOWN") == 0) {
        audioSayPriority(priority, "warning", "receiver", "disconnected", nullptr);
    } else if (strcmp(tag, "ARMED") == 0) {
        audioSayPriority(priority, "foot_drives", "armed", nullptr);
    } else if (strcmp(tag, "DISARMED") == 0) {
        audioSayPriority(priority, "foot_drives", "disarmed", nullptr);
    } else if (strcmp(tag, "SPD_SLOW") == 0) {
        audioSayPriority(priority, "speed", "slow", nullptr);
    } else if (strcmp(tag, "SPD_MED") == 0) {
        audioSayPriority(priority, "speed", "medium", nullptr);
    } else if (strcmp(tag, "SPD_FAST") == 0) {
        audioSayPriority(priority, "speed", "fast", nullptr);
    } else if (strcmp(tag, "BATT_LOW") == 0) {
        audioSayPriority(0, "warning", "primary", "battery", "low", nullptr);
    }
}

