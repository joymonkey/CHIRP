#include "config.h"

// ===================================
// MP3 Trigger Compatibility Actions
// ===================================

// State Tracking
int lastPlayedRootIndex = 0; // 0-based index in rootTracks array

// Helper to select and prepare a stream for compat playback
static int selectStreamForCompat() {
    int streamToUse = 1; // Default to Stream 1 for legacy behavior
    
    if (!legacyMonophonic) {
        // Polyphonic Mode: Find first inactive stream
        streamToUse = -1;
        for (int i = 0; i < maxStreams; i++) {
            if (!streams[i].active) {
                streamToUse = i;
                break;
            }
        }
        
        // If all active, fall back to Stream 1
        if (streamToUse == -1) {
            streamToUse = 1;
        }
    }

    // Stop Stream if Monophonic (or if we are reusing an active stream)
    if (legacyMonophonic || streams[streamToUse].active) {
        stopStream(streamToUse);
    }
    
    return streamToUse;
}

// Helper to play a root track by index
void playRootTrack(int index) {
    if (rootTrackCount == 0) return;
    
    // Sparkfun "Next/Prev" wraps
    if (index < 0) index = rootTrackCount - 1;
    if (index >= rootTrackCount) index = 0;
    
    const char* filename = rootTracks[index];
    
    char fullPath[128];
    snprintf(fullPath, sizeof(fullPath), "/%s", filename);
    
    int streamToUse = selectStreamForCompat();
    
    if (startStream(streamToUse, fullPath)) {
        lastPlayedRootIndex = index;
        Serial.printf("COMPAT: Playing Root Track %d/%d (%s) on Stream %d\n", index + 1, rootTrackCount, filename, streamToUse);
    }
}

// Helper to play a sound from Bank 1 on SD
static void playBank1Sound(int soundIdx, int trackNum) {
    if (soundIdx < 0 || soundIdx >= bank1SoundCount) return;
    if (bank1DirName[0] == '\0') return;

    SoundFile& sound = bank1Sounds[soundIdx];
    int variantIdx = 0;
    if (sound.variantCount > 1) {
        variantIdx = random(sound.variantCount);
        if (variantIdx == sound.lastVariantPlayed) {
            variantIdx = (variantIdx + 1) % sound.variantCount;
        }
    }
    sound.lastVariantPlayed = variantIdx;
    const char* filename = sound.variants[variantIdx];

    char fullPath[128];
    snprintf(fullPath, sizeof(fullPath), "/%s/%s", bank1DirName, filename);

    int streamToUse = selectStreamForCompat();
    if (startStream(streamToUse, fullPath)) {
        Serial.printf("COMPAT: Playing Bank 1 Track %d (%s) on Stream %d\n", trackNum, fullPath, streamToUse);
    }
}

void action_togglePlayPause() {
    if (streams[1].active) {
        stopStream(1);
        Serial.println("COMPAT: Stop");
    } else {
        // Play last played root track
        playRootTrack(lastPlayedRootIndex);
    }
}

void action_playNext() {
    playRootTrack(lastPlayedRootIndex + 1);
}

void action_playPrev() {
    playRootTrack(lastPlayedRootIndex - 1);
}

void action_playTrackById(int trackNum) {
    // trackNum is 1-based (e.g. 1 -> "001.MP3")
    char prefix[8];
    snprintf(prefix, sizeof(prefix), "%03d", trackNum); // "001"
    
    // 1. Search rootTracks for file starting with "001"
    for (int i = 0; i < rootTrackCount; i++) {
        if (strncmp(rootTracks[i], prefix, 3) == 0) {
            playRootTrack(i);
            return;
        }
    }
    
    // Fallback: Try strict number match if filename is just "1.mp3"
    snprintf(prefix, sizeof(prefix), "%d.", trackNum); // "1."
    for (int i = 0; i < rootTrackCount; i++) {
        if (strncmp(rootTracks[i], prefix, strlen(prefix)) == 0) {
            playRootTrack(i);
            return;
        }
    }

    // 2. Fallback to active Bank 1 directory if not found in root (or if rootTrackCount == 0)
    snprintf(prefix, sizeof(prefix), "%03d", trackNum);
    for (int i = 0; i < bank1SoundCount; i++) {
        if (strncmp(bank1Sounds[i].basename, prefix, 3) == 0 ||
            atoi(bank1Sounds[i].basename) == trackNum) {
            playBank1Sound(i, trackNum);
            return;
        }
    }

    // 3. Fallback: 1-based index in Bank 1
    if (trackNum >= 1 && trackNum <= bank1SoundCount) {
        playBank1Sound(trackNum - 1, trackNum);
        return;
    }
    
    Serial.printf("COMPAT: Track ID %d not found in root or Bank 1.\n", trackNum);
}

void action_playTrackByIndex(int trackIndex) {
    // trackIndex is raw 1-based index from 'p' command
    if (trackIndex >= 1 && trackIndex <= rootTrackCount) {
        playRootTrack(trackIndex - 1);
    } else if (rootTrackCount == 0 && trackIndex >= 1 && trackIndex <= bank1SoundCount) {
        playBank1Sound(trackIndex - 1, trackIndex);
    }
}

void action_setSparkfunVolume(uint8_t sfVol) {
    // 0 = Loud, 255 = Silent
    float vol = 1.0f - ((float)sfVol / 255.0f);
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    
    // Apply to ALL streams for global volume control effect
    if (streams) {
        for (int i = 0; i < maxStreams; i++) {
            streams[i].volume = vol;
        }
    }
    Serial.printf("COMPAT: Volume set to %.2f\n", vol);
}

// ===================================
// MP3 Trigger Compatibility Parser (Non-blocking State Machine)
// ===================================

enum CompatParserState {
    PARSER_IDLE = 0,
    PARSER_WAIT_T_BINARY,  // Received 't', awaiting binary track byte (0-255)
    PARSER_WAIT_T_ASCII,   // Received 'T', awaiting ASCII digit ('0'-'9')
    PARSER_WAIT_V_BINARY,  // Received 'v', awaiting volume byte (0-255)
    PARSER_WAIT_P_BINARY   // Received 'p', awaiting index byte (0-255)
};

static CompatParserState usbParserState = PARSER_IDLE;
static unsigned long usbParserLastTime = 0;

static CompatParserState uartParserState = PARSER_IDLE;
static unsigned long uartParserLastTime = 0;

bool checkAndHandleMp3Command(Stream &s, uint8_t byte, int cmdPos) {
    CompatParserState& state = (&s == &Serial) ? usbParserState : uartParserState;
    unsigned long& lastTime = (&s == &Serial) ? usbParserLastTime : uartParserLastTime;

    // Check if we are waiting for the second byte of a 2-byte command
    if (state != PARSER_IDLE) {
        if (millis() - lastTime > 500) {
            // Timed out waiting for argument byte; reset to IDLE and fall through
            state = PARSER_IDLE;
        } else {
            CompatParserState pending = state;
            state = PARSER_IDLE;
            switch (pending) {
                case PARSER_WAIT_T_BINARY:
                    action_playTrackById((int)byte);
                    return true;
                case PARSER_WAIT_T_ASCII:
                    if (byte >= '0' && byte <= '9') {
                        action_playTrackById(byte - '0');
                        return true;
                    }
                    return false;
                case PARSER_WAIT_V_BINARY:
                    action_setSparkfunVolume(byte);
                    return true;
                case PARSER_WAIT_P_BINARY:
                    action_playTrackByIndex((int)byte);
                    return true;
                default:
                    return false;
            }
        }
    }

    // If an ASCII command is currently being buffered, do NOT hijack characters as legacy commands
    if (cmdPos > 0) {
        return false;
    }

    // Evaluate single-byte commands or start two-byte commands
    switch (byte) {
        // ----------------------------------------------------------
        // 1-Byte Navigation Commands
        // ----------------------------------------------------------
        case 'O': // Navigation Center (Start/Stop)
            action_togglePlayPause();
            return true;

        case 'F': // Navigation Forward (Next)
            action_playNext();
            return true;

        case 'R': // Navigation Reverse (Prev)
            action_playPrev();
            return true;

        // ----------------------------------------------------------
        // 2-Byte Trigger/Control Commands
        // ----------------------------------------------------------
        case 'T': // Trigger (ASCII): 'T' + '0'-'9'
            state = PARSER_WAIT_T_ASCII;
            lastTime = millis();
            return true;

        case 't': // Trigger (Binary): 't' + 0-255
            state = PARSER_WAIT_T_BINARY;
            lastTime = millis();
            return true;

        case 'v': // Set Volume (Binary): 'v' + 0-255
            state = PARSER_WAIT_V_BINARY;
            lastTime = millis();
            return true;

        case 'p': // Play (Binary): 'p' + 0-255 (Play by Index)
            state = PARSER_WAIT_P_BINARY;
            lastTime = millis();
            return true;

        default:
            return false;
    }
}
