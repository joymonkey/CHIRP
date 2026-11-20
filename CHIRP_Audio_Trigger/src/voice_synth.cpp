#include "config.h"
#include <math.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// Built-In Droid Voice Pipelines
// ============================================================================
// To add a new built-in voice style, append a VoicePipeline entry below!
// "CUSTOM" (and "MP3" if ENABLE_LEGACY_MP3_VOICE is defined) are appended automatically.
static const VoicePipeline kBuiltInVoicePipelines[] = {
    {
        .name            = "DAFT",
        .useLegacyMp3    = false,
        .britishRp       = false,
        .sourceWave      = KLATT_SRC_CHORD,
        .bwScale         = 0.58f,
        .steppedPitch    = true,
        // Harmonized Daft Punk / talkbox chord vocoder with musical quantized steps
        .pitchMeanHz     = 110.0f,
        .pitchStdDevHz   = 6.0f,
        .durationStretch = 1.05f,
        .formantScale    = 1.02f,
        .wobbleHz        = 0.0f,
        .wobbleDepth     = 0.0f,
        .hpCutoffHz      = 140.0f,
        .lpCutoffHz      = 6200.0f,
        .peakFreqHz      = 1600.0f,
        .peakGainDb      = 3.5f,
        .peakQ           = 1.3f,
        .ringModHz       = 320.0f,
        .ringModMix      = 0.0f,
        .combDelayMs     = 2.8f,
        .combFeedback    = 0.20f,
        .combDamping     = 0.30f,
        .combMix         = 0.14f,
        .crushBits       = 16,
        .driveGain       = 1.22f,
        .outputGain      = 1.40f,
    },
    {
        .name            = "K2SO",
        .useLegacyMp3    = false,
        .britishRp       = true,
        .sourceWave      = KLATT_SRC_GLOTTAL,
        .bwScale         = 0.95f,
        .steppedPitch    = false,
        // Articulate, brisk British RP Imperial security droid (Alan Tudyk cadence)
        .pitchMeanHz     = 118.0f,
        .pitchStdDevHz   = 18.0f,
        .durationStretch = 0.92f,
        .formantScale    = 0.96f,
        .wobbleHz        = 0.0f,
        .wobbleDepth     = 0.0f,
        .hpCutoffHz      = 155.0f,
        .lpCutoffHz      = 5200.0f,
        .peakFreqHz      = 1400.0f,
        .peakGainDb      = 4.0f,
        .peakQ           = 1.5f,
        .ringModHz       = 420.0f,
        .ringModMix      = 0.02f,
        .combDelayMs     = 3.1f,
        .combFeedback    = 0.25f,
        .combDamping     = 0.30f,
        .combMix         = 0.25f,
        .crushBits       = 16,
        .driveGain       = 1.12f,
        .outputGain      = 1.40f,
    },
    {
        .name            = "B1",
        .useLegacyMp3    = false,
        .britishRp       = false,
        .sourceWave      = KLATT_SRC_PULSE,
        .bwScale         = 0.82f,
        .steppedPitch    = false,
        // High-pitched, nasal Separatist battle droid with metallic pulse clank
        .pitchMeanHz     = 175.0f,
        .pitchStdDevHz   = 24.0f,
        .durationStretch = 0.88f,
        .formantScale    = 1.22f,
        .wobbleHz        = 6.5f,
        .wobbleDepth     = 0.010f,
        .hpCutoffHz      = 420.0f,
        .lpCutoffHz      = 3600.0f,
        .peakFreqHz      = 2150.0f,
        .peakGainDb      = 7.0f,
        .peakQ           = 2.2f,
        .ringModHz       = 78.0f,
        .ringModMix      = 0.24f,
        .combDelayMs     = 2.4f,
        .combFeedback    = 0.28f,
        .combDamping     = 0.22f,
        .combMix         = 0.28f,
        .crushBits       = 11,
        .driveGain       = 1.30f,
        .outputGain      = 1.45f,
    },
    {
        .name            = "DROID",
        .useLegacyMp3    = false,
        .britishRp       = false,
        .sourceWave      = KLATT_SRC_GLOTTAL,
        .bwScale         = 0.88f,
        .steppedPitch    = false,
        // Clean synthetic droid voice
        .pitchMeanHz     = 108.0f,
        .pitchStdDevHz   = 10.0f,
        .durationStretch = 0.96f,
        .formantScale    = 1.00f,
        .wobbleHz        = 0.0f,
        .wobbleDepth     = 0.0f,
        .hpCutoffHz      = 150.0f,
        .lpCutoffHz      = 4800.0f,
        .peakFreqHz      = 1850.0f,
        .peakGainDb      = 3.0f,
        .peakQ           = 1.2f,
        .ringModHz       = 320.0f,
        .ringModMix      = 0.05f,
        .combDelayMs     = 4.2f,
        .combFeedback    = 0.22f,
        .combDamping     = 0.30f,
        .combMix         = 0.22f,
        .crushBits       = 16,
        .driveGain       = 1.08f,
        .outputGain      = 1.35f,
    },
};

static const int kNumBuiltInPipelines =
    (int)(sizeof(kBuiltInVoicePipelines) / sizeof(kBuiltInVoicePipelines[0]));

#ifdef ENABLE_LEGACY_MP3_VOICE
static const VoicePipeline kLegacyMp3Pipeline = {
    .name            = "MP3",
    .useLegacyMp3    = true,
    .britishRp       = false,
    .sourceWave      = KLATT_SRC_GLOTTAL,
    .bwScale         = 1.00f,
    .steppedPitch    = false,
    .pitchMeanHz     = 118.0f,
    .pitchStdDevHz   = 18.0f,
    .durationStretch = 0.92f,
    .formantScale    = 0.96f,
    .wobbleHz        = 0.0f,
    .wobbleDepth     = 0.0f,
    .hpCutoffHz      = 155.0f,
    .lpCutoffHz      = 5200.0f,
    .peakFreqHz      = 1400.0f,
    .peakGainDb      = 4.0f,
    .peakQ           = 1.5f,
    .ringModHz       = 420.0f,
    .ringModMix      = 0.02f,
    .combDelayMs     = 3.1f,
    .combFeedback    = 0.25f,
    .combDamping     = 0.30f,
    .combMix         = 0.25f,
    .crushBits       = 16,
    .driveGain       = 1.12f,
    .outputGain      = 1.40f,
};
#endif

// Runtime State
static VoicePipeline s_customPipeline = kBuiltInVoicePipelines[0];
static bool s_customModified = false;
static bool s_voiceInitialized = false;
static int s_activePipelineIdx = 0; // Default: DAFT

// Comb Resonator Delay Buffer (4 KB allocated once on first use)
static constexpr int COMB_BUF_LEN = 1024;
static float* s_combBuffer = nullptr;

// ============================================================================
// Pipeline Management
// ============================================================================
static float clampf_range(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void clampPipelineParams(VoicePipeline& p) {
    p.pitchMeanHz     = clampf_range(p.pitchMeanHz, 40.0f, 400.0f);
    p.pitchStdDevHz   = clampf_range(p.pitchStdDevHz, 0.0f, 80.0f);
    p.durationStretch = clampf_range(p.durationStretch, 0.4f, 2.5f);
    p.formantScale    = clampf_range(p.formantScale, 0.5f, 2.0f);
    p.wobbleHz        = clampf_range(p.wobbleHz, 0.0f, 30.0f);
    p.wobbleDepth     = clampf_range(p.wobbleDepth, 0.0f, 0.08f);
    p.hpCutoffHz      = clampf_range(p.hpCutoffHz, 20.0f, 4000.0f);
    p.lpCutoffHz      = clampf_range(p.lpCutoffHz, 300.0f, 18000.0f);
    p.peakFreqHz      = clampf_range(p.peakFreqHz, 100.0f, 10000.0f);
    p.peakGainDb      = clampf_range(p.peakGainDb, -18.0f, 18.0f);
    p.peakQ           = clampf_range(p.peakQ, 0.3f, 10.0f);
    p.ringModHz       = clampf_range(p.ringModHz, 10.0f, 4000.0f);
    p.ringModMix      = clampf_range(p.ringModMix, 0.0f, 1.0f);
    p.combDelayMs     = clampf_range(p.combDelayMs, 0.0f, 22.0f);
    p.combFeedback    = clampf_range(p.combFeedback, -0.85f, 0.85f);
    p.combDamping     = clampf_range(p.combDamping, 0.0f, 0.95f);
    p.combMix         = clampf_range(p.combMix, 0.0f, 1.0f);
    if (p.crushBits < 4) p.crushBits = 4;
    if (p.crushBits > 16) p.crushBits = 16;
    p.driveGain       = clampf_range(p.driveGain, 0.5f, 5.0f);
    p.outputGain      = clampf_range(p.outputGain, 0.1f, 5.0f);
    if (p.sourceWave >= KLATT_SRC_COUNT) p.sourceWave = KLATT_SRC_GLOTTAL;
    p.bwScale         = clampf_range(p.bwScale, 0.35f, 2.0f);
}

void initVoiceSynth() {
    if (s_voiceInitialized) return;
    s_customPipeline = kBuiltInVoicePipelines[0];
    s_customPipeline.name = "CUSTOM";
    s_customPipeline.useLegacyMp3 = false;
    s_customModified = false;
    s_activePipelineIdx = 0; // DAFT
    s_voiceInitialized = true;
}

int getVoicePipelineCount() {
#ifdef ENABLE_LEGACY_MP3_VOICE
    // Built-in presets + CUSTOM + MP3
    return kNumBuiltInPipelines + 2;
#else
    // Built-in presets + CUSTOM
    return kNumBuiltInPipelines + 1;
#endif
}

const VoicePipeline* getVoicePipelineByIndex(int idx) {
    if (!s_voiceInitialized) initVoiceSynth();
    if (idx < 0 || idx >= getVoicePipelineCount()) return nullptr;
    if (idx < kNumBuiltInPipelines) return &kBuiltInVoicePipelines[idx];
#ifdef ENABLE_LEGACY_MP3_VOICE
    if (idx == kNumBuiltInPipelines) return &s_customPipeline;
    return &kLegacyMp3Pipeline;
#else
    return &s_customPipeline;
#endif
}

bool setVoiceModeByName(const char* modeName) {
    if (!s_voiceInitialized) initVoiceSynth();
    if (!modeName || modeName[0] == '\0') return false;

    char clean[32];
    strncpy(clean, modeName, sizeof(clean) - 1);
    clean[sizeof(clean) - 1] = '\0';
    // Trim trailing whitespace
    int len = strlen(clean);
    while (len > 0 && (clean[len - 1] == ' ' || clean[len - 1] == '\r' || clean[len - 1] == '\n')) {
        clean[--len] = '\0';
    }

    int count = getVoicePipelineCount();
    for (int i = 0; i < count; i++) {
        const VoicePipeline* p = getVoicePipelineByIndex(i);
        if (p && strcasecmp(p->name, clean) == 0) {
            s_activePipelineIdx = i;
            return true;
        }
    }
    return false;
}

const char* getVoiceModeName() {
    const VoicePipeline& active = getActiveVoicePipeline();
    return active.name;
}

const VoicePipeline& getActiveVoicePipeline() {
    const VoicePipeline* p = getVoicePipelineByIndex(s_activePipelineIdx);
    return p ? *p : kBuiltInVoicePipelines[0];
}

VoicePipeline& getCustomVoicePipeline() {
    if (!s_voiceInitialized) initVoiceSynth();
    return s_customPipeline;
}

bool isCustomVoiceModified() {
    return s_customModified;
}

bool resetCustomVoiceFromPreset(const char* presetName) {
    if (!s_voiceInitialized) initVoiceSynth();
    if (!presetName || presetName[0] == '\0') return false;

    for (int i = 0; i < kNumBuiltInPipelines; i++) {
        if (strcasecmp(kBuiltInVoicePipelines[i].name, presetName) == 0) {
            s_customPipeline = kBuiltInVoicePipelines[i];
            s_customPipeline.name = "CUSTOM";
            s_customPipeline.useLegacyMp3 = false;
            s_customModified = true;
            return true;
        }
    }
    return false;
}

bool parseCustomVoiceConfig(const char* configLine) {
    if (!s_voiceInitialized) initVoiceSynth();
    if (!configLine || configLine[0] == '\0') return false;

    char buf[220];
    strncpy(buf, configLine, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    float vals[24];
    int idx = 0;
    char* savePtr = nullptr;
    char* tok = strtok_r(buf, ",", &savePtr);
    while (tok != nullptr && idx < 24) {
        while (*tok == ' ') tok++;
        char* endPtr = nullptr;
        vals[idx++] = strtof(tok, &endPtr);
        if (endPtr == tok) return false;
        tok = strtok_r(nullptr, ",", &savePtr);
    }

    if (idx < 19) return false;

    VoicePipeline& p = s_customPipeline;
    p.pitchMeanHz     = vals[0];
    p.pitchStdDevHz   = vals[1];
    p.durationStretch = vals[2];
    p.formantScale    = vals[3];
    p.wobbleHz        = vals[4];
    p.wobbleDepth     = vals[5];
    p.hpCutoffHz      = vals[6];
    p.lpCutoffHz      = vals[7];
    p.peakFreqHz      = vals[8];
    p.peakGainDb      = vals[9];
    p.peakQ           = vals[10];
    p.ringModHz       = vals[11];
    p.ringModMix      = vals[12];
    p.combDelayMs     = vals[13];
    p.combFeedback    = vals[14];
    p.combDamping     = vals[15];
    p.combMix         = vals[16];
    p.crushBits       = (uint8_t)((int)(vals[17] + 0.5f));
    p.driveGain       = vals[18];
    p.outputGain      = (idx >= 20) ? vals[19] : 1.30f;
    if (idx >= 21) {
        p.britishRp = (vals[20] >= 0.5f);
    }
    if (idx >= 22) {
        p.sourceWave = (uint8_t)((int)(vals[21] + 0.5f));
    }
    if (idx >= 23) {
        p.bwScale = vals[22];
    }
    if (idx >= 24) {
        p.steppedPitch = (vals[23] >= 0.5f);
    }

    clampPipelineParams(p);
    p.name = "CUSTOM";
    p.useLegacyMp3 = false;
    s_customModified = true;
    return true;
}

void formatCustomVoiceConfig(char* outBuf, size_t outSize) {
    if (!outBuf || outSize == 0) return;
    const VoicePipeline& p = getCustomVoicePipeline();
    snprintf(outBuf, outSize,
             "%.1f,%.1f,%.2f,%.2f,%.1f,%.3f,%.0f,%.0f,%.0f,%.1f,%.2f,%.0f,%.2f,%.1f,%.2f,%.2f,%.2f,%u,%.2f,%.2f,%d,%u,%.2f,%d",
             p.pitchMeanHz, p.pitchStdDevHz, p.durationStretch, p.formantScale,
             p.wobbleHz, p.wobbleDepth,
             p.hpCutoffHz, p.lpCutoffHz, p.peakFreqHz, p.peakGainDb, p.peakQ,
             p.ringModHz, p.ringModMix,
             p.combDelayMs, p.combFeedback, p.combDamping, p.combMix,
             (unsigned)p.crushBits, p.driveGain, p.outputGain,
             p.britishRp ? 1 : 0, (unsigned)p.sourceWave, p.bwScale,
             p.steppedPitch ? 1 : 0);
}

bool applyCustomVoiceParam(const char* keyVal) {
    if (!s_voiceInitialized) initVoiceSynth();
    if (!keyVal) return false;

    const char* eq = strchr(keyVal, '=');
    if (!eq || eq == keyVal) return false;

    char key[32];
    size_t keyLen = (size_t)(eq - keyVal);
    if (keyLen >= sizeof(key)) keyLen = sizeof(key) - 1;
    strncpy(key, keyVal, keyLen);
    key[keyLen] = '\0';
    // Trim trailing spaces on key
    while (keyLen > 0 && key[keyLen - 1] == ' ') key[--keyLen] = '\0';

    const char* valStr = eq + 1;
    while (*valStr == ' ') valStr++;

    VoicePipeline& p = s_customPipeline;
    if (strcasecmp(key, "src") == 0 || strcasecmp(key, "source") == 0) {
        int src = klattParseSourceType(valStr);
        if (src < 0) return false;
        p.sourceWave = (uint8_t)src;
        clampPipelineParams(p);
        s_customModified = true;
        return true;
    }

    char* endPtr = nullptr;
    float val = strtof(valStr, &endPtr);
    if (endPtr == valStr) return false;

    if (strcasecmp(key, "pitch") == 0)         p.pitchMeanHz = val;
    else if (strcasecmp(key, "std") == 0)      p.pitchStdDevHz = val;
    else if (strcasecmp(key, "stretch") == 0)  p.durationStretch = val;
    else if (strcasecmp(key, "formant") == 0)  p.formantScale = val;
    else if (strcasecmp(key, "bw") == 0)       p.bwScale = val;
    else if (strcasecmp(key, "step") == 0)     p.steppedPitch = (val >= 0.5f);
    else if (strcasecmp(key, "wobbleHz") == 0) p.wobbleHz = val;
    else if (strcasecmp(key, "wobble") == 0)   p.wobbleDepth = val;
    else if (strcasecmp(key, "hp") == 0)       p.hpCutoffHz = val;
    else if (strcasecmp(key, "lp") == 0)       p.lpCutoffHz = val;
    else if (strcasecmp(key, "peakHz") == 0)   p.peakFreqHz = val;
    else if (strcasecmp(key, "peakDb") == 0)   p.peakGainDb = val;
    else if (strcasecmp(key, "peakQ") == 0)    p.peakQ = val;
    else if (strcasecmp(key, "rmHz") == 0)     p.ringModHz = val;
    else if (strcasecmp(key, "rmMix") == 0)    p.ringModMix = val;
    else if (strcasecmp(key, "combMs") == 0)   p.combDelayMs = val;
    else if (strcasecmp(key, "combFb") == 0)   p.combFeedback = val;
    else if (strcasecmp(key, "combDamp") == 0) p.combDamping = val;
    else if (strcasecmp(key, "combMix") == 0)  p.combMix = val;
    else if (strcasecmp(key, "bits") == 0)     p.crushBits = (uint8_t)((int)(val + 0.5f));
    else if (strcasecmp(key, "drive") == 0)    p.driveGain = val;
    else if (strcasecmp(key, "gain") == 0)     p.outputGain = val;
    else if (strcasecmp(key, "uk") == 0 || strcasecmp(key, "british") == 0) p.britishRp = (val >= 0.5f);
    else return false;

    clampPipelineParams(p);
    s_customModified = true;
    return true;
}

// ============================================================================
// Text & Legacy Token Pre-Processor
// ============================================================================
static const char* const kLetterPhonetics[26] = {
    "ay", "bee", "see", "dee", "ee", "eff", "jee", "aitch", "eye",
    "jay", "kay", "ell", "em", "en", "oh", "pee", "cue", "ar",
    "ess", "tee", "you", "vee", "double you", "ex", "why", "zee"
};

struct AcronymEntry {
    const char* token;
    const char* spoken;
};

static const AcronymEntry kAcronymMap[] = {
    { "crc",     "see ar see" },
    { "usb",     "you ess bee" },
    { "mp3",     "em pee three" },
    { "wav",     "wave" },
    { "ini",     "eye en eye" },
    { "rc",      "ar see" },
    { "hz",      "hertz" },
    { "khz",     "kilohertz" },
    { "mhz",     "megahertz" },
    { "ghz",     "gigahertz" },
    { "kb",      "kilobytes" },
    { "mb",      "megabytes" },
    { "gb",      "gigabytes" },
    { "sd",      "ess dee" },
    { "ip",      "eye pee" },
    { "uart",    "you art" },
    { "i2s",     "eye two ess" },
    { "k2so",    "kay two ess oh" },
    { "k-2so",   "kay two ess oh" },
    { "b1",      "bee one" },
    { "b-1",     "bee one" },
    { "r2d2",    "ar two dee two" },
    { "r2-d2",   "ar two dee two" },
    { "c3po",    "see three pee oh" },
    { "c-3po",   "see three pee oh" },
    { "bb8",     "bee bee eight" },
    { "bb-8",    "bee bee eight" },
    { "bd1",     "bee dee one" },
    { "bd-1",    "bee dee one" },
    { "ig11",    "eye jee eleven" },
    { "ig-11",   "eye jee eleven" },
    { "ig88",    "eye jee eighty eight" },
    { "ig-88",   "eye jee eighty eight" },
    { "c1-10p",  "see one ten pee" },
};

static void appendWord(char* dst, size_t dstSize, const char* word) {
    if (!dst || dstSize == 0 || !word || word[0] == '\0') return;
    size_t curLen = strlen(dst);
    if (curLen > 0 && curLen + 1 < dstSize) {
        dst[curLen++] = ' ';
        dst[curLen] = '\0';
    }
    if (curLen + 1 < dstSize) {
        strncat(dst, word, dstSize - curLen - 1);
    }
}

static void appendRawNoSpace(char* dst, size_t dstSize, const char* s) {
    if (!dst || dstSize == 0 || !s || s[0] == '\0') return;
    size_t curLen = strlen(dst);
    if (curLen + 1 < dstSize) {
        strncat(dst, s, dstSize - curLen - 1);
    }
}

static const char* lookupAcronym(const char* word) {
    if (!word || word[0] == '\0') return nullptr;
    for (size_t i = 0; i < sizeof(kAcronymMap) / sizeof(kAcronymMap[0]); i++) {
        if (strcasecmp(word, kAcronymMap[i].token) == 0) {
            return kAcronymMap[i].spoken;
        }
    }
    return nullptr;
}

static void appendDigitsIndividually(const char* digits, size_t len, char* dst, size_t dstSize) {
    static const char* const kDigitWords[10] = {
        "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"
    };
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)digits[i];
        if (isdigit(c)) {
            appendWord(dst, dstSize, kDigitWords[c - '0']);
        }
    }
}

static uint64_t parseUint64Digits(const char* digits, size_t len) {
    uint64_t val = 0;
    for (size_t i = 0; i < len; i++) {
        val = val * 10ULL + (uint64_t)(digits[i] - '0');
    }
    return val;
}

static bool isValidOrdinalSuffix(uint64_t val, const char* suffix) {
    if (!suffix || strlen(suffix) != 2) return false;
    uint64_t mod100 = val % 100ULL;
    const char* expected = "th";
    if (mod100 < 11ULL || mod100 > 13ULL) {
        uint64_t mod10 = val % 10ULL;
        if (mod10 == 1ULL) expected = "st";
        else if (mod10 == 2ULL) expected = "nd";
        else if (mod10 == 3ULL) expected = "rd";
    }
    return (strcasecmp(suffix, expected) == 0);
}

static bool tryNormalizeNumberToken(const char* token, char* outPhrase, size_t outSize) {
    if (!token || token[0] == '\0') return false;

    const char* p = token;
    bool hasPrefix = false;
    bool hasHash = false;
    bool isCurrency = false;
    bool isNegative = false;
    bool isPositive = false;

    if (*p == '#' && isdigit((unsigned char)p[1])) {
        hasHash = true;
        hasPrefix = true;
        p++;
    } else if (*p == '$' && (isdigit((unsigned char)p[1]) || (p[1] == '.' && isdigit((unsigned char)p[2])))) {
        isCurrency = true;
        hasPrefix = true;
        p++;
    } else if (*p == '-' && (isdigit((unsigned char)p[1]) || (p[1] == '.' && isdigit((unsigned char)p[2])))) {
        isNegative = true;
        hasPrefix = true;
        p++;
    } else if (*p == '+' && (isdigit((unsigned char)p[1]) || (p[1] == '.' && isdigit((unsigned char)p[2])))) {
        isPositive = true;
        hasPrefix = true;
        p++;
    }

    // Time format: H:MM or HH:MM (hours <= 24, mins < 60)
    if (!hasPrefix) {
        size_t tLen = strlen(p);
        int colonPos = -1;
        if (tLen == 4 && isdigit((unsigned char)p[0]) && p[1] == ':' &&
            isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3])) {
            colonPos = 1;
        } else if (tLen == 5 && isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) &&
                   p[2] == ':' && isdigit((unsigned char)p[3]) && isdigit((unsigned char)p[4])) {
            colonPos = 2;
        }
        if (colonPos > 0) {
            uint64_t hours = parseUint64Digits(p, (size_t)colonPos);
            uint64_t mins = parseUint64Digits(p + colonPos + 1, 2);
            if (hours <= 24 && mins < 60) {
                char buf[64];
                klattNumberToWords(hours, buf, sizeof(buf));
                appendWord(outPhrase, outSize, buf);
                if (mins == 0) {
                    appendWord(outPhrase, outSize, (hours >= 1 && hours <= 12) ? "o'clock" : "hundred");
                } else if (mins < 10) {
                    appendWord(outPhrase, outSize, "oh");
                    klattNumberToWords(mins, buf, sizeof(buf));
                    appendWord(outPhrase, outSize, buf);
                } else {
                    klattNumberToWords(mins, buf, sizeof(buf));
                    appendWord(outPhrase, outSize, buf);
                }
                return true;
            }
        }
    }

    // Multi-dot numeric sequences (e.g., 1.2.3, 192.168.1.1)
    if (!hasPrefix && isdigit((unsigned char)p[0])) {
        int dotCount = 0;
        bool onlyDigitsAndDots = true;
        bool badDot = false;
        size_t tLen = strlen(p);
        if (tLen == 0 || p[tLen - 1] == '.') {
            onlyDigitsAndDots = false;
        }
        for (size_t i = 0; i < tLen && onlyDigitsAndDots; i++) {
            if (p[i] == '.') {
                dotCount++;
                if (p[i + 1] == '.') badDot = true;
            } else if (!isdigit((unsigned char)p[i])) {
                onlyDigitsAndDots = false;
            }
        }
        if (onlyDigitsAndDots && !badDot && dotCount >= 2) {
            const char* seg = p;
            bool firstSeg = true;
            while (*seg) {
                const char* dot = strchr(seg, '.');
                size_t segLen = dot ? (size_t)(dot - seg) : strlen(seg);
                if (!firstSeg) {
                    appendWord(outPhrase, outSize, "point");
                }
                if (segLen <= 12) {
                    char buf[192];
                    klattNumberToWords(parseUint64Digits(seg, segLen), buf, sizeof(buf));
                    appendWord(outPhrase, outSize, buf);
                } else {
                    appendDigitsIndividually(seg, segLen, outPhrase, outSize);
                }
                firstSeg = false;
                if (!dot) break;
                seg = dot + 1;
            }
            return true;
        }
    }

    // Parse [intDigits (with optional valid \d{1,3}(,\d{3})+ commas)] [.fracDigits] [suffix]
    char intDigits[128];
    size_t intLen = 0;
    const char* scan = p;
    if (isdigit((unsigned char)*scan)) {
        size_t g0Len = 0;
        while (isdigit((unsigned char)*scan)) {
            if (intLen < sizeof(intDigits) - 1) intDigits[intLen++] = *scan;
            scan++;
            g0Len++;
        }
        if (*scan == ',') {
            if (g0Len == 0 || g0Len > 3) return false;
            while (*scan == ',') {
                scan++;
                if (!isdigit((unsigned char)scan[0]) ||
                    !isdigit((unsigned char)scan[1]) ||
                    !isdigit((unsigned char)scan[2]) ||
                    isdigit((unsigned char)scan[3])) {
                    return false;
                }
                for (int k = 0; k < 3; k++) {
                    if (intLen < sizeof(intDigits) - 1) intDigits[intLen++] = scan[k];
                }
                scan += 3;
            }
        }
    }
    intDigits[intLen] = '\0';

    bool hasFrac = false;
    const char* fracDigits = nullptr;
    size_t fracLen = 0;
    if (*scan == '.') {
        scan++;
        if (!isdigit((unsigned char)*scan)) {
            return false;
        }
        hasFrac = true;
        fracDigits = scan;
        while (isdigit((unsigned char)*scan)) {
            fracLen++;
            scan++;
        }
    }

    if (intLen == 0 && !hasFrac) {
        return false;
    }

    const char* suffix = scan;
    bool emitOrdinal = false;
    const char* unitWord = nullptr;
    uint64_t intVal = (intLen > 0 && intLen <= 12) ? parseUint64Digits(intDigits, intLen) : 0ULL;
    bool singular = (!hasFrac && intLen > 0 && intLen <= 12 && intVal == 1ULL);

    if (suffix[0] == '\0') {
        if (isCurrency) {
            unitWord = singular ? "dollar" : "dollars";
        }
    } else if (!hasFrac && !hasHash && !isNegative && !isPositive && !isCurrency &&
               intLen > 0 && intLen <= 12 && isValidOrdinalSuffix(intVal, suffix)) {
        emitOrdinal = true;
    } else if (!isCurrency && !hasHash) {
        if (strcmp(suffix, "%") == 0)          unitWord = "percent";
        else if (strcasecmp(suffix, "v") == 0)   unitWord = singular ? "volt" : "volts";
        else if (strcasecmp(suffix, "mv") == 0)  unitWord = singular ? "millivolt" : "millivolts";
        else if (strcasecmp(suffix, "ma") == 0)  unitWord = singular ? "milliamp" : "milliamps";
        else if (strcasecmp(suffix, "hz") == 0)  unitWord = "hertz";
        else if (strcasecmp(suffix, "khz") == 0) unitWord = "kilohertz";
        else if (strcasecmp(suffix, "mhz") == 0) unitWord = "megahertz";
        else if (strcasecmp(suffix, "ghz") == 0) unitWord = "gigahertz";
        else if (strcasecmp(suffix, "ms") == 0)  unitWord = singular ? "millisecond" : "milliseconds";
        else if (strcasecmp(suffix, "db") == 0)  unitWord = singular ? "decibel" : "decibels";
        else if (strcasecmp(suffix, "kb") == 0)  unitWord = singular ? "kilobyte" : "kilobytes";
        else if (strcasecmp(suffix, "mb") == 0)  unitWord = singular ? "megabyte" : "megabytes";
        else if (strcasecmp(suffix, "gb") == 0)  unitWord = singular ? "gigabyte" : "gigabytes";
        else if (strcasecmp(suffix, "bps") == 0) unitWord = "bits per second";
        else return false;
    } else {
        return false;
    }

    if (hasHash) {
        appendWord(outPhrase, outSize, "number");
    } else if (isNegative) {
        appendWord(outPhrase, outSize, "minus");
    } else if (isPositive) {
        appendWord(outPhrase, outSize, "plus");
    }

    if (emitOrdinal) {
        char buf[192];
        klattOrdinalToWords(intVal, buf, sizeof(buf));
        appendWord(outPhrase, outSize, buf);
        return true;
    }

    if (intLen > 0) {
        if (intLen <= 12) {
            char buf[192];
            klattNumberToWords(intVal, buf, sizeof(buf));
            appendWord(outPhrase, outSize, buf);
        } else {
            appendDigitsIndividually(intDigits, intLen, outPhrase, outSize);
        }
    }

    if (hasFrac) {
        appendWord(outPhrase, outSize, "point");
        appendDigitsIndividually(fracDigits, fracLen, outPhrase, outSize);
    }

    if (unitWord) {
        appendWord(outPhrase, outSize, unitWord);
    }
    return true;
}

static void normalizeSingleSegment(const char* seg, char* outPhrase, size_t outSize) {
    if (!seg || seg[0] == '\0') return;

    const char* acro = lookupAcronym(seg);
    if (acro) {
        appendWord(outPhrase, outSize, acro);
        return;
    }
    if (tryNormalizeNumberToken(seg, outPhrase, outSize)) {
        return;
    }

    bool hasLetter = false;
    bool hasDigit = false;
    for (const char* q = seg; *q; q++) {
        if (isalpha((unsigned char)*q)) hasLetter = true;
        else if (isdigit((unsigned char)*q)) hasDigit = true;
    }

    if (hasLetter && hasDigit) {
        const char* q = seg;
        while (*q) {
            if (isalpha((unsigned char)*q)) {
                char run[64];
                size_t rLen = 0;
                while (isalpha((unsigned char)*q)) {
                    if (rLen < sizeof(run) - 1) run[rLen++] = *q;
                    q++;
                }
                run[rLen] = '\0';
                if (rLen == 1) {
                    int lIdx = tolower((unsigned char)run[0]) - 'a';
                    if (lIdx >= 0 && lIdx < 26) {
                        appendWord(outPhrase, outSize, kLetterPhonetics[lIdx]);
                    }
                } else {
                    const char* rAcro = lookupAcronym(run);
                    appendWord(outPhrase, outSize, rAcro ? rAcro : run);
                }
            } else if (isdigit((unsigned char)*q)) {
                char numRun[96];
                size_t nLen = 0;
                while (isdigit((unsigned char)*q)) {
                    if (nLen < sizeof(numRun) - 1) numRun[nLen++] = *q;
                    q++;
                }
                if (*q == '.' && isdigit((unsigned char)q[1])) {
                    if (nLen < sizeof(numRun) - 1) numRun[nLen++] = *q;
                    q++;
                    while (isdigit((unsigned char)*q)) {
                        if (nLen < sizeof(numRun) - 1) numRun[nLen++] = *q;
                        q++;
                    }
                }
                numRun[nLen] = '\0';
                if (!tryNormalizeNumberToken(numRun, outPhrase, outSize)) {
                    appendWord(outPhrase, outSize, numRun);
                }
            } else {
                q++;
            }
        }
        return;
    }

    appendWord(outPhrase, outSize, seg);
}

static void normalizeCoreToken(const char* core, char* outPhrase, size_t outSize) {
    if (!core || core[0] == '\0') return;

    const char* acro = lookupAcronym(core);
    if (acro) {
        appendWord(outPhrase, outSize, acro);
        return;
    }
    if (tryNormalizeNumberToken(core, outPhrase, outSize)) {
        return;
    }

    bool hasInternalSep = false;
    for (size_t i = 1; core[i] != '\0'; i++) {
        if (core[i] == '-' || core[i] == '/') {
            hasInternalSep = true;
            break;
        }
    }

    if (hasInternalSep) {
        const char* p = core;
        while (*p) {
            while (*p == '-' || *p == '/') p++;
            if (!*p) break;
            char seg[128];
            size_t sLen = 0;
            while (*p && *p != '-' && *p != '/') {
                if (sLen < sizeof(seg) - 1) seg[sLen++] = *p;
                p++;
            }
            seg[sLen] = '\0';
            if (sLen > 0) {
                normalizeSingleSegment(seg, outPhrase, outSize);
            }
        }
        return;
    }

    normalizeSingleSegment(core, outPhrase, outSize);
}

void normalizeVoiceToken(const char* rawInput, char* outPhrase, size_t outSize) {
    if (!outPhrase || outSize == 0) return;
    outPhrase[0] = '\0';
    if (!rawInput) return;

    char work[192];
    strncpy(work, rawInput, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    // Trim leading/trailing whitespace
    char* start = work;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    size_t len = strlen(start);
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t' ||
                       start[len - 1] == '\r' || start[len - 1] == '\n')) {
        start[--len] = '\0';
    }
    if (len == 0) return;

    // Strip trailing .wav or .mp3 extension if present
    if (len > 4) {
        const char* ext = start + len - 4;
        if (strcasecmp(ext, ".wav") == 0 || strcasecmp(ext, ".mp3") == 0) {
            start[len - 4] = '\0';
            len -= 4;
        }
    }

    // Single-letter legacy token: "_a" .. "_z"
    if (len == 2 && start[0] == '_' && isalpha((unsigned char)start[1])) {
        int letterIdx = tolower((unsigned char)start[1]) - 'a';
        if (letterIdx >= 0 && letterIdx < 26) {
            strncpy(outPhrase, kLetterPhonetics[letterIdx], outSize - 1);
            outPhrase[outSize - 1] = '\0';
            return;
        }
    }

    // Replace underscores with spaces and normalize token by token
    for (size_t i = 0; i < len; i++) {
        if (start[i] == '_') start[i] = ' ';
    }

    char* savePtr = nullptr;
    char* tok = strtok_r(start, " \t", &savePtr);
    while (tok != nullptr) {
        // Peel leading outer punctuation (( [ { " ')
        while (*tok == '(' || *tok == '[' || *tok == '{' || *tok == '"' || *tok == '\'') {
            tok++;
        }

        // Peel trailing punctuation () ] } " ' . , ! ? ; :) and keep sentence punctuation
        size_t tokLen = strlen(tok);
        size_t end = tokLen;
        while (end > 0) {
            char c = tok[end - 1];
            if (c == ')' || c == ']' || c == '}' || c == '"' || c == '\'' ||
                c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':') {
                end--;
            } else {
                break;
            }
        }

        char trailPunct[8];
        size_t tpLen = 0;
        for (size_t i = end; i < tokLen && tpLen < sizeof(trailPunct) - 1; i++) {
            char c = tok[i];
            if (c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':') {
                trailPunct[tpLen++] = c;
            }
        }
        trailPunct[tpLen] = '\0';
        tok[end] = '\0';

        if (tok[0] != '\0') {
            normalizeCoreToken(tok, outPhrase, outSize);
        }
        if (tpLen > 0) {
            appendRawNoSpace(outPhrase, outSize, trailPunct);
        }

        tok = strtok_r(nullptr, " \t", &savePtr);
    }
}

// ============================================================================
// Real-Time Droid DSP Components (44.1 kHz)
// ============================================================================
struct BiquadFilter {
    float b0, b1, b2, a1, a2;
    float z1, z2;
    bool bypassed;

    void reset() {
        z1 = 0.0f;
        z2 = 0.0f;
    }

    void setBypass() {
        bypassed = true;
        b0 = 1.0f; b1 = b2 = a1 = a2 = 0.0f;
        reset();
    }

    void setHighPass(float freqHz, float q, float sampleRate) {
        if (freqHz <= 20.0f) { setBypass(); return; }
        bypassed = false;
        float w0 = 2.0f * (float)M_PI * freqHz / sampleRate;
        float cosw0 = cosf(w0);
        float sinw0 = sinf(w0);
        float alpha = sinw0 / (2.0f * q);
        float a0 = 1.0f + alpha;
        b0 = ((1.0f + cosw0) * 0.5f) / a0;
        b1 = -(1.0f + cosw0) / a0;
        b2 = b0;
        a1 = (-2.0f * cosw0) / a0;
        a2 = (1.0f - alpha) / a0;
        reset();
    }

    void setLowPass(float freqHz, float q, float sampleRate) {
        if (freqHz >= 18000.0f) { setBypass(); return; }
        bypassed = false;
        float w0 = 2.0f * (float)M_PI * freqHz / sampleRate;
        float cosw0 = cosf(w0);
        float sinw0 = sinf(w0);
        float alpha = sinw0 / (2.0f * q);
        float a0 = 1.0f + alpha;
        b0 = ((1.0f - cosw0) * 0.5f) / a0;
        b1 = (1.0f - cosw0) / a0;
        b2 = b0;
        a1 = (-2.0f * cosw0) / a0;
        a2 = (1.0f - alpha) / a0;
        reset();
    }

    void setPeaking(float freqHz, float gainDb, float q, float sampleRate) {
        if (fabsf(gainDb) < 0.05f) { setBypass(); return; }
        bypassed = false;
        float A = powf(10.0f, gainDb / 40.0f);
        float w0 = 2.0f * (float)M_PI * freqHz / sampleRate;
        float cosw0 = cosf(w0);
        float sinw0 = sinf(w0);
        float alpha = sinw0 / (2.0f * q);
        float a0 = 1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / a0;
        b1 = (-2.0f * cosw0) / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = (-2.0f * cosw0) / a0;
        a2 = (1.0f - alpha / A) / a0;
        reset();
    }

    inline float process(float x) {
        if (bypassed) return x;
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

static bool ensureCombReady() {
    if (!s_combBuffer) {
        s_combBuffer = (float*)calloc(COMB_BUF_LEN, sizeof(float));
        if (!s_combBuffer) {
            Serial.println("VoiceSynth ERROR: Failed to allocate comb buffer!");
            return false;
        }
    }
    return true;
}

static bool pushStereoSampleWithWait(RingBuffer* rb, int16_t sample) {
    int waitUs = 0;
    while (rb->availableForWrite() < 2) {
        fillStreamBuffers();
        delayMicroseconds(200);
        waitUs += 200;
        if (waitUs > 1000000) {
            return false; // Safety timeout if Core 1 stops draining
        }
    }
    rb->push(sample); // Left
    rb->push(sample); // Right
    return true;
}

struct VoiceDspStreamState {
    AudioStream* stream;
    const VoicePipeline* pipe;
    BiquadFilter hpFilter;
    BiquadFilter peakFilter;
    BiquadFilter lpFilter;
    int combWriteIdx;
    int combDelaySamples;
    float combLpState;
    bool combEnabled;
    uint32_t rmPhase;
    uint32_t rmInc;
    bool rmEnabled;
    uint32_t wobblePhase;
    uint32_t wobbleInc;
    bool wobbleEnabled;
    bool crushEnabled;
    float crushLevels;
    float invCrushLevels;
    double baseStep;
    double srcPhase;
    float prevSample;
    bool hasFirst;
    bool aborted;
};

static bool processDspAndPush(VoiceDspStreamState& st, float x) {
    if (!st.stream->active || st.stream->stopRequested) {
        st.aborted = true;
        return false;
    }

    const VoicePipeline& p = *st.pipe;

    // Stage 2: 3-band Biquad Formant / Comlink EQ
    x = st.hpFilter.process(x);
    x = st.peakFilter.process(x);
    x = st.lpFilter.process(x);

    // Stage 3: Ring Modulator
    if (st.rmEnabled) {
        float carrier = (float)SINE_LUT[st.rmPhase >> 24] * (1.0f / 127.0f);
        st.rmPhase += st.rmInc;
        x = x * (1.0f - p.ringModMix) + (x * carrier) * p.ringModMix;
    }

    // Stage 4: Metallic Chassis Comb Resonator
    if (st.combEnabled) {
        int readIdx = (st.combWriteIdx - st.combDelaySamples + COMB_BUF_LEN) & (COMB_BUF_LEN - 1);
        float delayed = s_combBuffer[readIdx];
        st.combLpState = delayed * (1.0f - p.combDamping) + st.combLpState * p.combDamping;
        s_combBuffer[st.combWriteIdx] = x + st.combLpState * p.combFeedback;
        st.combWriteIdx = (st.combWriteIdx + 1) & (COMB_BUF_LEN - 1);
        x = x * (1.0f - p.combMix) + delayed * p.combMix;
    }

    // Stage 5: Vocoder Drive, Bitcrush & Output Gain
    if (p.driveGain > 1.01f || p.driveGain < 0.99f) {
        x *= p.driveGain;
        x = x / (1.0f + fabsf(x) * 0.35f);
    }
    if (st.crushEnabled) {
        x = roundf(x * st.crushLevels) * st.invCrushLevels;
    }

    float scaled = x * p.outputGain * 32767.0f;
    if (scaled > 32767.0f) scaled = 32767.0f;
    else if (scaled < -32768.0f) scaled = -32768.0f;

    if (!pushStereoSampleWithWait(st.stream->ringBuffer, (int16_t)scaled)) {
        st.aborted = true;
        return false;
    }
    return true;
}

static bool onKlattSample(float curSample, void* userData) {
    VoiceDspStreamState* st = (VoiceDspStreamState*)userData;
    if (!st || st->aborted || !st->stream->active || st->stream->stopRequested) {
        return false;
    }

    if (!st->hasFirst) {
        st->prevSample = curSample;
        st->srcPhase = 0.0;
        st->hasFirst = true;
        return true;
    }

    while (st->srcPhase < 1.0) {
        float x = st->prevSample + (float)st->srcPhase * (curSample - st->prevSample);
        if (!processDspAndPush(*st, x)) {
            return false;
        }
        double step = st->baseStep;
        if (st->wobbleEnabled) {
            float lfo = (float)SINE_LUT[st->wobblePhase >> 24] * (1.0f / 127.0f);
            st->wobblePhase += st->wobbleInc;
            step *= (1.0 + (double)st->pipe->wobbleDepth * (double)lfo);
        }
        st->srcPhase += step;
    }

    st->srcPhase -= 1.0;
    st->prevSample = curSample;
    return true;
}

// ============================================================================
// Synthesis + Droid DSP + Stream 0 Playback
// ============================================================================
bool playSynthesizedVoice(const char* rawTokenOrPhrase) {
    if (!streams || maxStreams < 1 || !rawTokenOrPhrase || rawTokenOrPhrase[0] == '\0') {
        return false;
    }

    const char* trimmed = rawTokenOrPhrase;
    while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\r' || *trimmed == '\n') {
        trimmed++;
    }
    size_t rawLen = strlen(trimmed);
    while (rawLen > 0 && (trimmed[rawLen - 1] == ' ' || trimmed[rawLen - 1] == '\t' ||
                          trimmed[rawLen - 1] == '\r' || trimmed[rawLen - 1] == '\n')) {
        rawLen--;
    }
    if (rawLen == 0) return false;
    if (rawLen > VOICE_SYNTH_MAX_PHRASE_LEN) {
        Serial.printf("VoiceSynth ERROR: Phrase too long (%u > %d chars)\n",
                      (unsigned)rawLen, VOICE_SYNTH_MAX_PHRASE_LEN);
        return false;
    }

    char phrase[384];
    normalizeVoiceToken(rawTokenOrPhrase, phrase, sizeof(phrase));
    if (phrase[0] == '\0') return false;

    if (!ensureCombReady()) return false;

    // Select active pipeline (or DAFT when falling back from MP3 mode)
    const VoicePipeline& active = getActiveVoicePipeline();
    const VoicePipeline& p = active.useLegacyMp3 ? kBuiltInVoicePipelines[0] : active;

    Serial.printf("[%lu] VoiceSynth [%s/%s/%s]: \"%s\"\n",
                  millis(), p.name, p.britishRp ? "RP" : "US",
                  klattSourceTypeName(p.sourceWave), phrase);

    KlattPhoneme phones[KLATT_MAX_PHONEMES];
    int numPhones = klattTextToPhonemes(phrase, p.britishRp, phones, KLATT_MAX_PHONEMES);
    if (numPhones <= 0) {
        return false;
    }

    // Prepare Stream 0
    bool wasMuted = !g_allowAudio;
    g_allowAudio = true;
    if (wasMuted) {
        delay(120); // Ramp up I2S/DAC on Rev.A or initial boot
    }

    stopStream(0);
    AudioStream* s = &streams[0];
    s->ringBuffer->clear();
    s->type = STREAM_TYPE_VOICE_SYNTH;
    s->decoderIndex = -1;
    s->channels = 1;
    s->sampleRate = KLATT_SAMPLE_RATE;
    s->volume = 1.0f;
    s->fileFinished = false;
    s->stopRequested = false;
    s->startTime = millis();
    strncpy(s->filename, phrase, sizeof(s->filename) - 1);
    s->filename[sizeof(s->filename) - 1] = '\0';
    s->active = true;

    // Configure streaming Droid DSP state (16 kHz Klatt -> 44.1 kHz Stream 0)
    VoiceDspStreamState st;
    memset(&st, 0, sizeof(st));
    const float outRate = (float)SAMPLE_RATE;
    st.stream = s;
    st.pipe = &p;
    st.hpFilter.setHighPass(p.hpCutoffHz, 0.70710678f, outRate);
    st.peakFilter.setPeaking(p.peakFreqHz, p.peakGainDb, p.peakQ, outRate);
    st.lpFilter.setLowPass(p.lpCutoffHz, 0.70710678f, outRate);

    memset(s_combBuffer, 0, COMB_BUF_LEN * sizeof(float));
    st.combWriteIdx = 0;
    st.combDelaySamples = (int)(p.combDelayMs * (outRate / 1000.0f) + 0.5f);
    if (st.combDelaySamples < 1) st.combDelaySamples = 1;
    if (st.combDelaySamples >= COMB_BUF_LEN) st.combDelaySamples = COMB_BUF_LEN - 1;
    st.combLpState = 0.0f;
    st.combEnabled = (p.combMix > 0.001f && p.combDelayMs > 0.05f);

    st.rmPhase = 0;
    st.rmInc = (uint32_t)(p.ringModHz * (4294967296.0 / (double)outRate));
    st.rmEnabled = (p.ringModMix > 0.001f);

    st.wobblePhase = 0;
    st.wobbleInc = (uint32_t)(p.wobbleHz * (4294967296.0 / (double)outRate));
    st.wobbleEnabled = (p.wobbleHz > 0.01f && p.wobbleDepth > 0.0001f);

    st.crushEnabled = (p.crushBits < 16);
    st.crushLevels = (float)(1U << (p.crushBits - 1));
    st.invCrushLevels = 1.0f / st.crushLevels;

    st.baseStep = (double)KLATT_SAMPLE_RATE / (double)SAMPLE_RATE;
    st.srcPhase = 0.0;
    st.prevSample = 0.0f;
    st.hasFirst = false;
    st.aborted = false;

    KlattConfig kcfg;
    kcfg.pitchMeanHz = p.pitchMeanHz;
    kcfg.pitchStdDevHz = p.pitchStdDevHz;
    kcfg.durationStretch = p.durationStretch;
    kcfg.formantScale = p.formantScale;
    kcfg.bwScale = p.bwScale;
    kcfg.sourceType = p.sourceWave;
    kcfg.britishRp = p.britishRp;
    kcfg.steppedPitch = p.steppedPitch;

    klattSynthesize(phones, numPhones, kcfg, onKlattSample, &st);

    // Emit ~50 ms decay tail for biquad/comb resonators if playback wasn't aborted
    const int tailOutSamples = (SAMPLE_RATE * 50) / 1000;
    for (int i = 0; i < tailOutSamples && !st.aborted; i++) {
        if (!processDspAndPush(st, 0.0f)) {
            break;
        }
    }

    s->fileFinished = true;

    // Wait for Stream 0 ring buffer to drain while continuing to pump other streams
    while (s->active) {
        fillStreamBuffers();

        if (s->stopRequested) {
            stopStream(0);
            s->stopRequested = false;
        }
        if (s->active && s->fileFinished && s->ringBuffer->availableForRead() == 0) {
            stopStream(0);
        }
        delay(1);
    }

    // Rev.A (Software Mute): restore silence if no other streams are playing
    #if !defined(I2S_MUTE_PIN)
    bool otherActive = false;
    for (int i = 1; i < maxStreams; i++) {
        if (streams[i].active) {
            otherActive = true;
            break;
        }
    }
    if (!otherActive) {
        g_allowAudio = false;
    }
    #endif

    delay(5);
    return true;
}
