#include "config.h"
#include <math.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>

// Include Flite core C API directly (avoids flite_out_arduino.h's config.h collision)
#include "flite.h"

extern "C" {
    cst_voice *register_cmu_us_kal(const char *voxdir);
    cst_utterance *cmu_postlex(cst_utterance *u);
    cst_utterance *us_f0_model(cst_utterance *u);
}

// ============================================================================
// Built-In Droid Voice Pipelines
// ============================================================================
// To add a new built-in voice style, append a VoicePipeline entry below!
// "CUSTOM" and "MP3" are appended automatically at runtime.
static const VoicePipeline kBuiltInVoicePipelines[] = {
    {
        .name            = "K2SO",
        .useLegacyMp3    = false,
        .britishRp       = true,
        // Articulate, brisk British RP Imperial security droid (Alan Tudyk cadence)
        .pitchMeanHz     = 116.0f,
        .pitchStdDevHz   = 18.5f,
        .durationStretch = 0.90f,
        .formantScale    = 0.97f,
        .wobbleHz        = 0.0f,
        .wobbleDepth     = 0.0f,
        .hpCutoffHz      = 165.0f,
        .lpCutoffHz      = 4800.0f,
        .peakFreqHz      = 1380.0f,
        .peakGainDb      = 4.2f,
        .peakQ           = 1.6f,
        .ringModHz       = 420.0f,
        .ringModMix      = 0.03f,
        .combDelayMs     = 3.1f,
        .combFeedback    = 0.28f,
        .combDamping     = 0.30f,
        .combMix         = 0.28f,
        .crushBits       = 16,
        .driveGain       = 1.10f,
        .outputGain      = 1.35f,
    },
    {
        .name            = "B1",
        .useLegacyMp3    = false,
        .britishRp       = false,
        // High-pitched, nasal Separatist battle droid with metallic clank
        .pitchMeanHz     = 172.0f,
        .pitchStdDevHz   = 22.0f,
        .durationStretch = 0.88f,
        .formantScale    = 1.22f,
        .wobbleHz        = 6.5f,
        .wobbleDepth     = 0.012f,
        .hpCutoffHz      = 440.0f,
        .lpCutoffHz      = 3300.0f,
        .peakFreqHz      = 2150.0f,
        .peakGainDb      = 7.5f,
        .peakQ           = 2.2f,
        .ringModHz       = 78.0f,
        .ringModMix      = 0.32f,
        .combDelayMs     = 2.4f,
        .combFeedback    = 0.30f,
        .combDamping     = 0.20f,
        .combMix         = 0.30f,
        .crushBits       = 10,
        .driveGain       = 1.35f,
        .outputGain      = 1.45f,
    },
    {
        .name            = "DROID",
        .useLegacyMp3    = false,
        .britishRp       = false,
        // Clean synthetic droid voice
        .pitchMeanHz     = 105.0f,
        .pitchStdDevHz   = 9.0f,
        .durationStretch = 0.96f,
        .formantScale    = 1.00f,
        .wobbleHz        = 0.0f,
        .wobbleDepth     = 0.0f,
        .hpCutoffHz      = 150.0f,
        .lpCutoffHz      = 4500.0f,
        .peakFreqHz      = 1900.0f,
        .peakGainDb      = 3.0f,
        .peakQ           = 1.2f,
        .ringModHz       = 320.0f,
        .ringModMix      = 0.08f,
        .combDelayMs     = 4.5f,
        .combFeedback    = 0.25f,
        .combDamping     = 0.30f,
        .combMix         = 0.25f,
        .crushBits       = 16,
        .driveGain       = 1.05f,
        .outputGain      = 1.25f,
    },
};

static const int kNumBuiltInPipelines =
    (int)(sizeof(kBuiltInVoicePipelines) / sizeof(kBuiltInVoicePipelines[0]));

#ifdef ENABLE_LEGACY_MP3_VOICE
static const VoicePipeline kLegacyMp3Pipeline = {
    .name            = "MP3",
    .useLegacyMp3    = true,
    .britishRp       = false,
    .pitchMeanHz     = 86.0f,
    .pitchStdDevHz   = 4.5f,
    .durationStretch = 0.95f,
    .formantScale    = 0.91f,
    .wobbleHz        = 0.0f,
    .wobbleDepth     = 0.0f,
    .hpCutoffHz      = 120.0f,
    .lpCutoffHz      = 4200.0f,
    .peakFreqHz      = 1750.0f,
    .peakGainDb      = 4.5f,
    .peakQ           = 1.4f,
    .ringModHz       = 540.0f,
    .ringModMix      = 0.12f,
    .combDelayMs     = 6.8f,
    .combFeedback    = 0.42f,
    .combDamping     = 0.35f,
    .combMix         = 0.45f,
    .crushBits       = 16,
    .driveGain       = 1.15f,
    .outputGain      = 1.35f,
};
#endif

// Runtime State
static VoicePipeline s_customPipeline = kBuiltInVoicePipelines[0];
static bool s_customModified = false;
static bool s_voiceInitialized = false;
static int s_activePipelineIdx = 0; // Default: K2SO

// Flite Engine State (lazily initialized on first synthesis)
static cst_voice* s_kalVoice = nullptr;
static bool s_fliteReady = false;

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
}

void initVoiceSynth() {
    if (s_voiceInitialized) return;
    s_customPipeline = kBuiltInVoicePipelines[0];
    s_customPipeline.name = "CUSTOM";
    s_customPipeline.useLegacyMp3 = false;
    s_customModified = false;
    s_activePipelineIdx = 0; // K2SO
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

    float vals[21];
    int idx = 0;
    char* savePtr = nullptr;
    char* tok = strtok_r(buf, ",", &savePtr);
    while (tok != nullptr && idx < 21) {
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
             "%.1f,%.1f,%.2f,%.2f,%.1f,%.3f,%.0f,%.0f,%.0f,%.1f,%.2f,%.0f,%.2f,%.1f,%.2f,%.2f,%.2f,%u,%.2f,%.2f,%d",
             p.pitchMeanHz, p.pitchStdDevHz, p.durationStretch, p.formantScale,
             p.wobbleHz, p.wobbleDepth,
             p.hpCutoffHz, p.lpCutoffHz, p.peakFreqHz, p.peakGainDb, p.peakQ,
             p.ringModHz, p.ringModMix,
             p.combDelayMs, p.combFeedback, p.combDamping, p.combMix,
             (unsigned)p.crushBits, p.driveGain, p.outputGain,
             p.britishRp ? 1 : 0);
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
    char* endPtr = nullptr;
    float val = strtof(valStr, &endPtr);
    if (endPtr == valStr) return false;

    VoicePipeline& p = s_customPipeline;
    if (strcasecmp(key, "pitch") == 0)         p.pitchMeanHz = val;
    else if (strcasecmp(key, "std") == 0)      p.pitchStdDevHz = val;
    else if (strcasecmp(key, "stretch") == 0)  p.durationStretch = val;
    else if (strcasecmp(key, "formant") == 0)  p.formantScale = val;
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
    { "ini",     "eye en eye" },
    { "rc",      "ar see" },
    { "hz",      "hertz" },
    { "mhz",     "megahertz" },
    { "sd",      "ess dee" },
    { "kyber",   "kaiber" },
    { "padawan", "paduhwan" },
    { "k2so",    "kay two ess oh" },
    { "b1",      "bee one" },
};

static void appendWord(char* dst, size_t dstSize, const char* word) {
    if (!word || word[0] == '\0') return;
    size_t curLen = strlen(dst);
    if (curLen > 0 && curLen + 1 < dstSize) {
        dst[curLen++] = ' ';
        dst[curLen] = '\0';
    }
    strncat(dst, word, dstSize - curLen - 1);
}

static void appendBelowThousand(char* dst, size_t dstSize, int n) {
    static const char* const ones[] = {
        "", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
        "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen",
        "sixteen", "seventeen", "eighteen", "nineteen"
    };
    static const char* const tens[] = {
        "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"
    };

    if (n >= 100) {
        appendWord(dst, dstSize, ones[n / 100]);
        appendWord(dst, dstSize, "hundred");
        n %= 100;
    }
    if (n >= 20) {
        appendWord(dst, dstSize, tens[n / 10]);
        n %= 10;
    }
    if (n > 0) {
        appendWord(dst, dstSize, ones[n]);
    }
}

static void numberToWords(long n, char* out, size_t outSize) {
    out[0] = '\0';
    if (n == 0) {
        strncpy(out, "zero", outSize - 1);
        out[outSize - 1] = '\0';
        return;
    }
    if (n < 0) {
        appendWord(out, outSize, "minus");
        n = -n;
    }
    if (n >= 1000000L) {
        snprintf(out, outSize, "%ld", n);
        return;
    }
    if (n >= 1000) {
        appendBelowThousand(out, outSize, (int)(n / 1000));
        appendWord(out, outSize, "thousand");
        n %= 1000;
    }
    if (n > 0) {
        appendBelowThousand(out, outSize, (int)n);
    }
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

    // Pure numeric string (e.g. "0000", "0042", "0100")
    bool allDigits = true;
    for (size_t i = 0; i < len; i++) {
        if (!isdigit((unsigned char)start[i])) {
            allDigits = false;
            break;
        }
    }
    if (allDigits) {
        long val = strtol(start, nullptr, 10);
        numberToWords(val, outPhrase, outSize);
        return;
    }

    // Replace underscores with spaces and normalize word by word
    for (size_t i = 0; i < len; i++) {
        if (start[i] == '_') start[i] = ' ';
    }

    char* savePtr = nullptr;
    char* word = strtok_r(start, " ", &savePtr);
    while (word != nullptr) {
        const char* replacement = nullptr;
        for (size_t i = 0; i < sizeof(kAcronymMap) / sizeof(kAcronymMap[0]); i++) {
            if (strcasecmp(word, kAcronymMap[i].token) == 0) {
                replacement = kAcronymMap[i].spoken;
                break;
            }
        }
        appendWord(outPhrase, outSize, replacement ? replacement : word);
        word = strtok_r(nullptr, " ", &savePtr);
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

// ============================================================================
// British RP Phonology & Stepped High-Falling Prosody Hooks for Flite
// ============================================================================
static bool s_useBritishRp = true;

static bool isFliteVowel(const char* ph) {
    if (!ph) return false;
    return ( strcmp(ph, "aa") == 0 || strcmp(ph, "ae") == 0 || strcmp(ph, "ah") == 0 ||
             strcmp(ph, "ao") == 0 || strcmp(ph, "aw") == 0 || strcmp(ph, "ax") == 0 ||
             strcmp(ph, "axr") == 0 || strcmp(ph, "ay") == 0 || strcmp(ph, "eh") == 0 ||
             strcmp(ph, "er") == 0 || strcmp(ph, "ey") == 0 || strcmp(ph, "ih") == 0 ||
             strcmp(ph, "iy") == 0 || strcmp(ph, "ow") == 0 || strcmp(ph, "oy") == 0 ||
             strcmp(ph, "uh") == 0 || strcmp(ph, "uw") == 0 );
}

static bool isShortCheckedVowel(const char* ph) {
    if (!ph) return false;
    return ( strcmp(ph, "ih") == 0 || strcmp(ph, "eh") == 0 || strcmp(ph, "ae") == 0 ||
             strcmp(ph, "uh") == 0 || strcmp(ph, "ah") == 0 || strcmp(ph, "ao") == 0 );
}

static bool isCommonTrapNWord(const char* w) {
    if (!w) return false;
    return ( strcasecmp(w, "and") == 0 || strcasecmp(w, "hand") == 0 ||
             strcasecmp(w, "hands") == 0 || strcasecmp(w, "land") == 0 ||
             strcasecmp(w, "stand") == 0 || strcasecmp(w, "band") == 0 ||
             strcasecmp(w, "sand") == 0 || strcasecmp(w, "brand") == 0 ||
             strcasecmp(w, "grand") == 0 || strcasecmp(w, "plan") == 0 ||
             strcasecmp(w, "man") == 0 || strcasecmp(w, "can") == 0 ||
             strcasecmp(w, "ran") == 0 || strcasecmp(w, "than") == 0 ||
             strcasecmp(w, "scan") == 0 || strcasecmp(w, "bank") == 0 ||
             strcasecmp(w, "tank") == 0 || strcasecmp(w, "thank") == 0 ||
             strcasecmp(w, "thanks") == 0 );
}

static bool isLotRoundedWord(const char* w) {
    if (!w || w[0] == '\0') return false;
    for (const char* p = w; *p; p++) {
        if (tolower((unsigned char)*p) == 'o') return true;
    }
    if (strncasecmp(w, "wa", 2) == 0 || strncasecmp(w, "wh", 2) == 0 ||
        strncasecmp(w, "qua", 3) == 0 || strncasecmp(w, "squa", 4) == 0) {
        return true;
    }
    return false;
}

static cst_utterance* chirp_postlex(cst_utterance* u) {
    cmu_postlex(u);

    if (!s_useBritishRp) {
        // Default cmu_us_kal behavior
        for (cst_item* s = relation_head(utt_relation(u, "Segment")); s; s = item_next(s)) {
            if (cst_streq(item_feat_string(s, "name"), "ah")) {
                item_set_string(s, "name", "aa");
            }
        }
        return u;
    }

    // Pass 1: British RP vowel shifts (LOT rounding, TRAP-BATH split, NURSE/LETTER non-rhotic vowels, crisp T)
    for (cst_item* s = relation_head(utt_relation(u, "Segment")); s; s = item_next(s)) {
        const char* name = item_feat_string(s, "name");
        cst_item* next = item_next(s);
        const char* nextName = next ? item_feat_string(next, "name") : "pau";
        cst_item* next2 = next ? item_next(next) : nullptr;
        const char* next2Name = next2 ? item_feat_string(next2, "name") : "pau";

        const char* word = ffeature_string(s, "R:SylStructure.parent.parent.name");
        int stress = ffeature_int(s, "R:SylStructure.parent.stress");
        bool nextInSameWord = false;
        if (next) {
            const char* nextWord = ffeature_string(next, "R:SylStructure.parent.parent.name");
            nextInSameWord = (word && nextWord && strcmp(word, nextWord) == 0);
        }

        // 1. Crisp British /t/ (prevent American flapping)
        if (cst_streq(name, "dx")) {
            item_set_string(s, "name", "t");
            item_set_int(s, "rp_crisp_t", 1);
            continue;
        }
        if (cst_streq(name, "t") && nextInSameWord && isFliteVowel(nextName)) {
            item_set_int(s, "rp_crisp_t", 1);
        }

        // 2. LOT / CLOTH vowel rounding: American "aa" (or "ah" in what/was/want/watch/wash) -> British "ao"
        if (cst_streq(name, "aa") && !cst_streq(nextName, "r")) {
            if (isLotRoundedWord(word)) {
                item_set_string(s, "name", "ao");
                continue;
            }
        }
        if (cst_streq(name, "ah") && !cst_streq(nextName, "r")) {
            if (strcasecmp(word, "what") == 0 || strcasecmp(word, "what's") == 0 ||
                strcasecmp(word, "was") == 0 || strcasecmp(word, "wasn't") == 0 ||
                strcasecmp(word, "want") == 0 || strcasecmp(word, "wants") == 0 ||
                strcasecmp(word, "watch") == 0 || strcasecmp(word, "wash") == 0) {
                item_set_string(s, "name", "ao");
                continue;
            }
        }

        // 3. TRAP-BATH split: "ae" -> broad British "aa" before s/f/th or n+s/t/d/ch
        if (cst_streq(name, "ae") && nextInSameWord) {
            bool isBath = false;
            if (cst_streq(nextName, "s") || cst_streq(nextName, "f") || cst_streq(nextName, "th")) {
                bool next2SameWord = false;
                if (next2) {
                    const char* next2Word = ffeature_string(next2, "R:SylStructure.parent.parent.name");
                    next2SameWord = (word && next2Word && strcmp(word, next2Word) == 0);
                }
                if (!next2SameWord || cst_streq(next2Name, "t") ||
                    cst_streq(next2Name, "p") || cst_streq(next2Name, "k")) {
                    isBath = true;
                }
            } else if (cst_streq(nextName, "n") && !isCommonTrapNWord(word)) {
                if (cst_streq(next2Name, "s") || cst_streq(next2Name, "t") ||
                    cst_streq(next2Name, "d") || cst_streq(next2Name, "ch")) {
                    isBath = true;
                }
            }
            if (isBath) {
                item_set_string(s, "name", "aa");
                item_set_int(s, "rp_long", 1);
                continue;
            }
        }

        // 4. Non-rhotic "er" / "axr" (LETTER -> schwa "ax", stressed NURSE -> long central "ah")
        if ((cst_streq(name, "er") || cst_streq(name, "axr")) && !isFliteVowel(nextName)) {
            if (stress > 0 && cst_streq(name, "er")) {
                item_set_string(s, "name", "ah");
                item_set_int(s, "rp_long", 1);
            } else {
                item_set_string(s, "name", "ax");
            }
            continue;
        }
    }

    // Pass 2: Non-rhotic post-vocalic 'r' dropping / centering glide (preserves linking-R before vowels)
    cst_item* s = relation_head(utt_relation(u, "Segment"));
    while (s) {
        cst_item* next = item_next(s);
        const char* name = ffeature_string(s, "name");
        if (cst_streq(name, "r")) {
            cst_item* prev = item_prev(s);
            const char* prevName = prev ? ffeature_string(prev, "name") : "pau";
            const char* nextName = next ? ffeature_string(next, "name") : "pau";
            const char* word = ffeature_string(s, "R:SylStructure.parent.parent.name");
            const char* prevWord = prev ? ffeature_string(prev, "R:SylStructure.parent.parent.name") : "";
            bool sameWordAsPrev = (word && prevWord && word[0] != '\0' && strcmp(word, prevWord) == 0);

            if (sameWordAsPrev && isFliteVowel(prevName) && !isFliteVowel(nextName)) {
                if (cst_streq(prevName, "aa") || cst_streq(prevName, "ao") ||
                    cst_streq(prevName, "ah") || cst_streq(prevName, "ax")) {
                    // START / NORTH / FORCE: lengthen preceding vowel and drop 'r'
                    item_set_int(prev, "rp_long", 1);
                    cst_item* ss = item_as(s, "SylStructure");
                    if (ss) delete_item(ss);
                    delete_item(s);
                    s = next;
                    continue;
                } else {
                    // NEAR / SQUARE / CURE: convert 'r' into subtle centering schwa glide
                    item_set_string(s, "name", "ax");
                    item_set_int(s, "rp_glide", 1);
                }
            }
        }
        s = next;
    }

    return u;
}

static cst_utterance* chirp_duration_model(cst_utterance* u) {
    cart_duration(u);
    if (!s_useBritishRp) return u;

    // Rescale segment durations for clipped British RP rhythm + long non-rhotic vowels
    float prevOrigEnd = 0.0f;
    float accumEnd = 0.0f;
    for (cst_item* s = relation_head(utt_relation(u, "Segment")); s; s = item_next(s)) {
        float origEnd = ffeature_float(s, "end");
        float dur = origEnd - prevOrigEnd;
        if (dur < 0.005f) dur = 0.005f;
        prevOrigEnd = origEnd;

        const char* name = ffeature_string(s, "name");
        if (item_feat_present(s, "rp_long")) {
            dur *= 1.32f; // Long British /a:/, /o:/, /3:/
        } else if (item_feat_present(s, "rp_glide")) {
            dur *= 0.65f; // Subtle centering schwa glide
        } else if (item_feat_present(s, "rp_crisp_t")) {
            dur *= 1.10f; // Crisp aspirated British /t/
        } else if (cst_streq(name, "ax")) {
            dur *= 0.85f; // Brisk unstressed schwa
        } else if (isShortCheckedVowel(name)) {
            dur *= 0.90f; // Clipped British short checked vowels
        }

        accumEnd += dur;
        item_set_float(s, "end", accumEnd);
    }
    return u;
}

static cst_utterance* chirp_f0_model(cst_utterance* u) {
    us_f0_model(u);
    if (!s_useBritishRp) return u;

    cst_relation* targRel = utt_relation(u, "Target");
    if (!targRel) return u;

    float meanF0 = get_param_float(u->features, "int_f0_target_mean", 116.0f);

    // Apply British RP stepped "High Head" + High-Falling nuclear cadence across syllables
    bool seenFirstStressInPhrase = false;
    for (cst_item* syl = relation_head(utt_relation(u, "Syllable")); syl; syl = item_next(syl)) {
        cst_item* firstSeg = path_to_item(syl, "R:SylStructure.daughter.R:Segment");
        cst_item* lastSeg = path_to_item(syl, "R:SylStructure.daughtern.R:Segment");
        if (!firstSeg || !lastSeg) continue;

        cst_item* prevSeg = item_prev(firstSeg);
        float sylStart = prevSeg ? ffeature_float(prevSeg, "end") : 0.0f;
        float sylEnd = ffeature_float(lastSeg, "end");
        float sylMid = 0.5f * (sylStart + sylEnd);

        bool postBreak = (!prevSeg || cst_streq(ffeature_string(prevSeg, "name"), "pau"));
        cst_item* nextSeg = item_next(lastSeg);
        bool preBreak = (!nextSeg || cst_streq(ffeature_string(nextSeg, "name"), "pau"));
        if (postBreak) {
            seenFirstStressInPhrase = false;
        }

        int stress = ffeature_int(syl, "stress");
        const char* punc = ffeature_string(syl, "R:SylStructure.parent.R:Token.parent.punc");
        bool isQuestion = (punc && strchr(punc, '?') != nullptr);

        for (cst_item* t = relation_head(targRel); t; t = item_next(t)) {
            float pos = ffeature_float(t, "pos");
            if (pos < sylStart - 0.002f || pos > sylEnd + 0.002f) continue;

            float f0 = ffeature_float(t, "f0");
            bool isNearMid = (fabsf(pos - sylMid) <= 0.35f * (sylEnd - sylStart + 0.001f));
            bool isNearEnd = (pos >= sylEnd - 0.015f);

            if (stress > 0) {
                if (!seenFirstStressInPhrase) {
                    // British RP "High Head" step-up on the first stressed syllable of a clause
                    f0 += meanF0 * (isNearMid ? 0.13f : 0.07f);
                } else {
                    // Subsequent stressed syllable: high peak followed by crisp fall
                    f0 += meanF0 * (isNearMid ? 0.08f : -0.05f);
                }
            } else {
                // Unstressed syllables sit slightly lower and flatter for dry contrast
                f0 -= meanF0 * 0.04f;
            }

            if (preBreak && isNearEnd) {
                if (isQuestion) {
                    f0 = meanF0 * 1.16f;
                } else {
                    // Dry, deadpan K-2SO terminal drop at phrase boundary
                    f0 = meanF0 * 0.85f;
                }
            }

            if (f0 < 55.0f) f0 = 55.0f;
            if (f0 > 450.0f) f0 = 450.0f;
            item_set_float(t, "f0", f0);
        }

        if (stress > 0) {
            seenFirstStressInPhrase = true;
        }
    }

    return u;
}

static bool ensureFliteReady() {
    if (!s_fliteReady) {
        flite_init();
        s_kalVoice = register_cmu_us_kal(nullptr);
        if (!s_kalVoice) {
            Serial.println("VoiceSynth ERROR: register_cmu_us_kal failed!");
            return false;
        }
        feat_set(s_kalVoice->features, "postlex_func", uttfunc_val(&chirp_postlex));
        feat_set(s_kalVoice->features, "duration_model_func", uttfunc_val(&chirp_duration_model));
        feat_set(s_kalVoice->features, "f0_model_func", uttfunc_val(&chirp_f0_model));
        s_fliteReady = true;
    }
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

    char phrase[96];
    normalizeVoiceToken(rawTokenOrPhrase, phrase, sizeof(phrase));
    if (phrase[0] == '\0') return false;

    if (!ensureFliteReady()) return false;

    // Select active pipeline (or K2SO when falling back from MP3 mode)
    const VoicePipeline& active = getActiveVoicePipeline();
    const VoicePipeline& p = active.useLegacyMp3 ? kBuiltInVoicePipelines[0] : active;
    s_useBritishRp = p.britishRp;

    // Compensate Flite's internal F0 & duration for formant resampler scaling
    float formant = (p.formantScale > 0.1f) ? p.formantScale : 1.0f;
    float fliteF0 = clampf_range(p.pitchMeanHz / formant, 40.0f, 350.0f);
    float fliteStd = clampf_range(p.pitchStdDevHz / formant, 0.0f, 80.0f);
    float fliteStretch = clampf_range(p.durationStretch * formant, 0.4f, 2.5f);

    flite_feat_set_float(s_kalVoice->features, "int_f0_target_mean", fliteF0);
    flite_feat_set_float(s_kalVoice->features, "int_f0_target_stddev", fliteStd);
    flite_feat_set_float(s_kalVoice->features, "duration_stretch", fliteStretch);

    Serial.printf("[%lu] VoiceSynth [%s/%s]: \"%s\"\n",
                  millis(), p.name, s_useBritishRp ? "RP" : "US", phrase);

    // Trap Flite out-of-memory / internal errors via cst_errjmp so cst_error()
    // never calls exit(-1) and halts the MCU. Also call flite_synth_text()
    // directly instead of flite_text_to_wave() to avoid duplicating the wave buffer.
    jmp_buf fliteErrBuf;
    cst_errjmp = &fliteErrBuf;
    cst_utterance* u = nullptr;
    if (setjmp(fliteErrBuf) != 0) {
        cst_errjmp = nullptr;
        Serial.println("VoiceSynth ERROR: Flite synthesis aborted (out of SRAM)");
        return false;
    }
    u = flite_synth_text(phrase, s_kalVoice);
    cst_errjmp = nullptr;

    cst_wave* wave = u ? utt_wave(u) : nullptr;
    if (!wave || !wave->samples || wave->num_samples <= 0) {
        if (u) delete_utterance(u);
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
    s->sampleRate = (uint32_t)wave->sample_rate;
    s->volume = 1.0f;
    s->fileFinished = false;
    s->stopRequested = false;
    s->startTime = millis();
    strncpy(s->filename, phrase, sizeof(s->filename) - 1);
    s->filename[sizeof(s->filename) - 1] = '\0';
    s->active = true;

    // Configure DSP Stages
    const float outRate = (float)SAMPLE_RATE;
    BiquadFilter hpFilter, peakFilter, lpFilter;
    hpFilter.setHighPass(p.hpCutoffHz, 0.70710678f, outRate);
    peakFilter.setPeaking(p.peakFreqHz, p.peakGainDb, p.peakQ, outRate);
    lpFilter.setLowPass(p.lpCutoffHz, 0.70710678f, outRate);

    memset(s_combBuffer, 0, COMB_BUF_LEN * sizeof(float));
    int combWriteIdx = 0;
    int combDelaySamples = (int)(p.combDelayMs * (outRate / 1000.0f) + 0.5f);
    if (combDelaySamples < 1) combDelaySamples = 1;
    if (combDelaySamples >= COMB_BUF_LEN) combDelaySamples = COMB_BUF_LEN - 1;
    float combLpState = 0.0f;
    bool combEnabled = (p.combMix > 0.001f && p.combDelayMs > 0.05f);

    uint32_t rmPhase = 0;
    uint32_t rmInc = (uint32_t)(p.ringModHz * (4294967296.0 / (double)outRate));
    bool rmEnabled = (p.ringModMix > 0.001f);

    uint32_t wobblePhase = 0;
    uint32_t wobbleInc = (uint32_t)(p.wobbleHz * (4294967296.0 / (double)outRate));
    bool wobbleEnabled = (p.wobbleHz > 0.01f && p.wobbleDepth > 0.0001f);

    bool crushEnabled = (p.crushBits < 16);
    float crushLevels = (float)(1U << (p.crushBits - 1));
    float invCrushLevels = 1.0f / crushLevels;

    float srcRate = (wave->sample_rate > 0) ? (float)wave->sample_rate : 8000.0f;
    float baseStep = (srcRate * formant) / outRate;
    double srcPos = 0.0;
    int numSrcSamples = wave->num_samples;

    // Stream Flite samples with linear interpolation + ~50 ms decay tail
    const int tailOutSamples = (SAMPLE_RATE * 50) / 1000;
    int tailRemaining = tailOutSamples;

    while (s->active && !s->stopRequested) {
        float x = 0.0f;

        if (srcPos < (double)numSrcSamples) {
            int i0 = (int)srcPos;
            float frac = (float)(srcPos - (double)i0);
            float s0 = (float)wave->samples[i0] * (1.0f / 32768.0f);
            float s1 = (i0 + 1 < numSrcSamples)
                           ? ((float)wave->samples[i0 + 1] * (1.0f / 32768.0f))
                           : 0.0f;
            x = s0 + (s1 - s0) * frac;

            float step = baseStep;
            if (wobbleEnabled) {
                float lfo = (float)SINE_LUT[wobblePhase >> 24] * (1.0f / 127.0f);
                wobblePhase += wobbleInc;
                step *= (1.0f + p.wobbleDepth * lfo);
            }
            srcPos += (double)step;
        } else if (tailRemaining > 0) {
            tailRemaining--;
        } else {
            break;
        }

        // Stage 2: 3-band Biquad Formant / Comlink EQ
        x = hpFilter.process(x);
        x = peakFilter.process(x);
        x = lpFilter.process(x);

        // Stage 3: Ring Modulator
        if (rmEnabled) {
            float carrier = (float)SINE_LUT[rmPhase >> 24] * (1.0f / 127.0f);
            rmPhase += rmInc;
            x = x * (1.0f - p.ringModMix) + (x * carrier) * p.ringModMix;
        }

        // Stage 4: Metallic Chassis Comb Resonator
        if (combEnabled) {
            int readIdx = (combWriteIdx - combDelaySamples + COMB_BUF_LEN) & (COMB_BUF_LEN - 1);
            float delayed = s_combBuffer[readIdx];
            combLpState = delayed * (1.0f - p.combDamping) + combLpState * p.combDamping;
            s_combBuffer[combWriteIdx] = x + combLpState * p.combFeedback;
            combWriteIdx = (combWriteIdx + 1) & (COMB_BUF_LEN - 1);
            x = x * (1.0f - p.combMix) + delayed * p.combMix;
        }

        // Stage 5: Vocoder Drive, Bitcrush & Output Gain
        if (p.driveGain > 1.01f || p.driveGain < 0.99f) {
            x *= p.driveGain;
            x = x / (1.0f + fabsf(x) * 0.35f);
        }
        if (crushEnabled) {
            x = roundf(x * crushLevels) * invCrushLevels;
        }

        float scaled = x * p.outputGain * 32767.0f;
        if (scaled > 32767.0f) scaled = 32767.0f;
        else if (scaled < -32768.0f) scaled = -32768.0f;

        if (!pushStereoSampleWithWait(s->ringBuffer, (int16_t)scaled)) {
            break;
        }
    }

    delete_utterance(u);

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
