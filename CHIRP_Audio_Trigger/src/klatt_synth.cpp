#include "klatt_synth.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Klatt-80 Resonator & Anti-Resonator Primitives (at KLATT_SAMPLE_RATE)
// ============================================================================
struct KlattResonator {
    float a, b, c;
    float z1, z2;

    void reset() {
        a = 1.0f;
        b = 0.0f;
        c = 0.0f;
        z1 = 0.0f;
        z2 = 0.0f;
    }

    // Standard cascade 2-pole resonator (unity DC gain)
    void setCascade(float freqHz, float bwHz) {
        float fs = (float)KLATT_SAMPLE_RATE;
        if (freqHz < 60.0f) freqHz = 60.0f;
        if (freqHz > fs * 0.46f) freqHz = fs * 0.46f;
        if (bwHz < 25.0f) bwHz = 25.0f;
        if (bwHz > 2400.0f) bwHz = 2400.0f;

        float r = expf(-(float)M_PI * bwHz / fs);
        c = -(r * r);
        b = 2.0f * r * cosf(2.0f * (float)M_PI * freqHz / fs);
        a = 1.0f - b - c;
    }

    // Nasal anti-resonator (exact inverse of setCascade, unity DC gain)
    void setAntiResonator(float freqHz, float bwHz) {
        float fs = (float)KLATT_SAMPLE_RATE;
        if (freqHz < 200.0f) freqHz = 200.0f;
        if (freqHz > fs * 0.46f) freqHz = fs * 0.46f;
        if (bwHz < 30.0f) bwHz = 30.0f;
        if (bwHz > 2400.0f) bwHz = 2400.0f;

        float r = expf(-(float)M_PI * bwHz / fs);
        float c0 = -(r * r);
        float b0 = 2.0f * r * cosf(2.0f * (float)M_PI * freqHz / fs);
        float a0 = 1.0f - b0 - c0;
        if (a0 < 0.004f) a0 = 0.004f;
        a = 1.0f / a0;
        b = -b0 / a0;
        c = -c0 / a0;
    }

    // Constant-peak-gain (= 1.0) bandpass resonator for parallel frication branch
    void setBandpass(float freqHz, float bwHz) {
        float fs = (float)KLATT_SAMPLE_RATE;
        if (freqHz < 100.0f) freqHz = 100.0f;
        if (freqHz > fs * 0.46f) freqHz = fs * 0.46f;
        if (bwHz < 35.0f) bwHz = 35.0f;
        if (bwHz > 2400.0f) bwHz = 2400.0f;

        float r = expf(-(float)M_PI * bwHz / fs);
        c = -(r * r);
        b = 2.0f * r * cosf(2.0f * (float)M_PI * freqHz / fs);
        a = 1.0f - r;
    }

    inline float processPole(float x) {
        float y = a * x + b * z1 + c * z2;
        z2 = z1;
        z1 = y;
        return y;
    }

    inline float processZero(float x) {
        float y = a * x + b * z1 + c * z2;
        z2 = z1;
        z1 = x;
        return y;
    }
};

struct ParallelBandpass {
    float a, b, c;
    float x1, x2;
    float y1, y2;

    void reset() {
        a = b = c = 0.0f;
        x1 = x2 = y1 = y2 = 0.0f;
    }

    void set(float freqHz, float bwHz) {
        float fs = (float)KLATT_SAMPLE_RATE;
        if (freqHz < 100.0f) freqHz = 100.0f;
        if (freqHz > fs * 0.46f) freqHz = fs * 0.46f;
        if (bwHz < 35.0f) bwHz = 35.0f;
        if (bwHz > 2400.0f) bwHz = 2400.0f;

        float r = expf(-(float)M_PI * bwHz / fs);
        c = -(r * r);
        b = 2.0f * r * cosf(2.0f * (float)M_PI * freqHz / fs);
        a = 1.0f - r;
    }

    inline float process(float x) {
        float y = a * (x - x2) + b * y1 + c * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// ============================================================================
// Phoneme Acoustic Targets (41 entries matching KlattPhoneId)
// ============================================================================
enum PhoneClass : uint8_t {
    PC_SILENCE = 0,
    PC_VOWEL,
    PC_DIPHTHONG,
    PC_STOP_VOICELESS,
    PC_STOP_VOICED,
    PC_AFFRICATE,
    PC_FRICATIVE,
    PC_NASAL,
    PC_LIQUID
};

struct PhoneTarget {
    PhoneClass cls;
    float durMs;
    float f1, f2, f3;
    float f1End, f2End, f3End;
    float b1, b2, b3;
    float av, af, ah;
    float a2, a3, a4, a5, a6, ab;
    bool isNasal;
    float fnz;
};

static const PhoneTarget kPhoneTable[PH_COUNT] = {
    // PH_SIL
    {PC_SILENCE,        90.0f,  500, 1500, 2500,   500, 1500, 2500,  80, 100, 140, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_AA (father / calm)
    {PC_VOWEL,         145.0f,  730, 1090, 2440,   730, 1090, 2440,  60,  80, 120, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_AE (trap / bat)
    {PC_VOWEL,         140.0f,  660, 1720, 2410,   660, 1720, 2410,  65,  95, 130, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_AH (strut / cup)
    {PC_VOWEL,         105.0f,  630, 1210, 2390,   630, 1210, 2390,  60,  85, 120, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_AO (lot / thought)
    {PC_VOWEL,         135.0f,  570,  840, 2410,   570,  840, 2410,  55,  75, 110, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_AW (mouth)
    {PC_DIPHTHONG,     175.0f,  700, 1150, 2400,   380,  820, 2280,  60,  85, 120, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_AX (schwa / NURSE when lengthened)
    {PC_VOWEL,          75.0f,  500, 1420, 2450,   500, 1420, 2450,  65,  90, 130, 0.90f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_AY (price)
    {PC_DIPHTHONG,     180.0f,  720, 1180, 2420,   340, 2050, 2700,  60,  90, 130, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_EH (dress)
    {PC_VOWEL,         120.0f,  530, 1840, 2480,   530, 1840, 2480,  60,  95, 130, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_ER (rhotic nurse/letter)
    {PC_VOWEL,         135.0f,  490, 1350, 1690,   490, 1350, 1690,  60,  85, 110, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_EY (face)
    {PC_DIPHTHONG,     165.0f,  480, 1950, 2550,   320, 2220, 2820,  55,  95, 130, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_IH (kit)
    {PC_VOWEL,          95.0f,  390, 1990, 2550,   390, 1990, 2550,  55,  95, 135, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_IY (fleece)
    {PC_VOWEL,         135.0f,  280, 2280, 2950,   280, 2280, 2950,  50, 100, 150, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_OW (goat)
    {PC_DIPHTHONG,     165.0f,  480,  900, 2350,   340,  760, 2250,  55,  75, 110, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_OY (choice)
    {PC_DIPHTHONG,     180.0f,  560,  860, 2400,   330, 2050, 2720,  55,  90, 130, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_UH (foot)
    {PC_VOWEL,         100.0f,  440, 1020, 2240,   440, 1020, 2240,  55,  80, 110, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_UW (goose)
    {PC_VOWEL,         140.0f,  310,  870, 2240,   310,  870, 2240,  50,  75, 110, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},

    // PH_P
    {PC_STOP_VOICELESS, 85.0f,  220,  800, 2150,   220,  800, 2150,  80, 120, 160, 0.00f, 0.65f, 0.45f, 0.40f, 0.15f, 0.00f, 0.00f, 0.00f, 0.45f, false, 270},
    // PH_B
    {PC_STOP_VOICED,    60.0f,  220,  800, 2150,   220,  800, 2150,  75, 115, 155, 0.42f, 0.35f, 0.00f, 0.35f, 0.10f, 0.00f, 0.00f, 0.00f, 0.35f, false, 270},
    // PH_T
    {PC_STOP_VOICELESS, 80.0f,  240, 1650, 2650,   240, 1650, 2650,  80, 120, 160, 0.00f, 0.75f, 0.45f, 0.00f, 0.25f, 0.45f, 0.65f, 0.55f, 0.15f, false, 270},
    // PH_D
    {PC_STOP_VOICED,    58.0f,  240, 1650, 2650,   240, 1650, 2650,  75, 115, 155, 0.45f, 0.40f, 0.00f, 0.00f, 0.20f, 0.40f, 0.55f, 0.45f, 0.10f, false, 270},
    // PH_K
    {PC_STOP_VOICELESS, 85.0f,  240, 1850, 2350,   240, 1850, 2350,  80, 120, 160, 0.00f, 0.72f, 0.48f, 0.55f, 0.65f, 0.25f, 0.00f, 0.00f, 0.10f, false, 270},
    // PH_G
    {PC_STOP_VOICED,    62.0f,  240, 1850, 2350,   240, 1850, 2350,  75, 115, 155, 0.45f, 0.40f, 0.00f, 0.45f, 0.55f, 0.20f, 0.00f, 0.00f, 0.08f, false, 270},

    // PH_CH
    {PC_AFFRICATE,     112.0f,  280, 1850, 2550,   280, 1850, 2550,  80, 120, 160, 0.00f, 0.72f, 0.15f, 0.00f, 0.55f, 0.70f, 0.45f, 0.20f, 0.00f, false, 270},
    // PH_JH
    {PC_AFFRICATE,     100.0f,  280, 1850, 2550,   280, 1850, 2550,  80, 120, 160, 0.52f, 0.52f, 0.00f, 0.00f, 0.50f, 0.65f, 0.40f, 0.15f, 0.00f, false, 270},

    // PH_F
    {PC_FRICATIVE,     100.0f,  350, 1150, 2350,   350, 1150, 2350,  80, 130, 180, 0.00f, 0.35f, 0.00f, 0.10f, 0.12f, 0.20f, 0.30f, 0.25f, 0.75f, false, 270},
    // PH_V
    {PC_FRICATIVE,      75.0f,  320, 1100, 2300,   320, 1100, 2300,  75, 120, 170, 0.60f, 0.25f, 0.00f, 0.08f, 0.10f, 0.15f, 0.22f, 0.20f, 0.55f, false, 270},
    // PH_TH
    {PC_FRICATIVE,      95.0f,  350, 1350, 2500,   350, 1350, 2500,  80, 130, 180, 0.00f, 0.34f, 0.00f, 0.08f, 0.15f, 0.28f, 0.35f, 0.30f, 0.60f, false, 270},
    // PH_DH
    {PC_FRICATIVE,      60.0f,  320, 1350, 2500,   320, 1350, 2500,  75, 120, 170, 0.65f, 0.20f, 0.00f, 0.05f, 0.10f, 0.20f, 0.25f, 0.20f, 0.45f, false, 270},
    // PH_S
    {PC_FRICATIVE,     115.0f,  320, 1550, 2700,   320, 1550, 2700,  80, 120, 170, 0.00f, 0.60f, 0.00f, 0.00f, 0.05f, 0.18f, 0.55f, 0.90f, 0.05f, false, 270},
    // PH_Z
    {PC_FRICATIVE,      90.0f,  300, 1550, 2700,   300, 1550, 2700,  75, 115, 165, 0.58f, 0.42f, 0.00f, 0.00f, 0.05f, 0.15f, 0.48f, 0.78f, 0.04f, false, 270},
    // PH_SH
    {PC_FRICATIVE,     115.0f,  300, 1850, 2550,   300, 1850, 2550,  80, 120, 170, 0.00f, 0.58f, 0.00f, 0.00f, 0.58f, 0.80f, 0.45f, 0.18f, 0.00f, false, 270},
    // PH_ZH
    {PC_FRICATIVE,      85.0f,  290, 1850, 2550,   290, 1850, 2550,  75, 115, 165, 0.58f, 0.40f, 0.00f, 0.00f, 0.48f, 0.70f, 0.38f, 0.15f, 0.00f, false, 270},
    // PH_HH
    {PC_FRICATIVE,      75.0f,  500, 1500, 2500,   500, 1500, 2500,  90, 140, 190, 0.00f, 0.00f, 0.62f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},

    // PH_M
    {PC_NASAL,          80.0f,  280,  900, 2200,   280,  900, 2200,  85, 110, 140, 0.72f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, true,  450},
    // PH_N
    {PC_NASAL,          72.0f,  280, 1500, 2550,   280, 1500, 2550,  85, 110, 140, 0.72f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, true,  650},
    // PH_NG
    {PC_NASAL,          82.0f,  290, 1850, 2300,   290, 1850, 2300,  85, 110, 140, 0.72f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, true,  950},

    // PH_L
    {PC_LIQUID,         75.0f,  330, 1050, 2850,   330, 1050, 2850,  65,  90, 130, 0.90f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_R
    {PC_LIQUID,         75.0f,  350, 1200, 1650,   350, 1200, 1650,  65,  90, 120, 0.90f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_W
    {PC_LIQUID,         75.0f,  310,  680, 2200,   310,  680, 2200,  65,  85, 120, 0.90f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
    // PH_Y
    {PC_LIQUID,         72.0f,  280, 2200, 2950,   280, 2200, 2950,  60,  95, 135, 0.90f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, 270},
};

static_assert(sizeof(kPhoneTable) / sizeof(kPhoneTable[0]) == PH_COUNT, "Phone table mismatch");

// ============================================================================
// Source Waveform Helpers
// ============================================================================
const char* klattSourceTypeName(uint8_t src) {
    switch (src) {
        case KLATT_SRC_GLOTTAL: return "GLOTTAL";
        case KLATT_SRC_SAW:     return "SAW";
        case KLATT_SRC_PULSE:   return "PULSE";
        case KLATT_SRC_SQUARE:  return "SQUARE";
        case KLATT_SRC_CHORD:   return "CHORD";
        case KLATT_SRC_WHISPER: return "WHISPER";
        default:                return "GLOTTAL";
    }
}

int klattParseSourceType(const char* str) {
    if (!str || !str[0]) return -1;
    while (*str == ' ') str++;
    if (isdigit((unsigned char)str[0])) {
        int val = atoi(str);
        return (val >= 0 && val < KLATT_SRC_COUNT) ? val : -1;
    }
    if (strcasecmp(str, "glottal") == 0) return KLATT_SRC_GLOTTAL;
    if (strcasecmp(str, "saw") == 0)     return KLATT_SRC_SAW;
    if (strcasecmp(str, "pulse") == 0)   return KLATT_SRC_PULSE;
    if (strcasecmp(str, "square") == 0)  return KLATT_SRC_SQUARE;
    if (strcasecmp(str, "chord") == 0)   return KLATT_SRC_CHORD;
    if (strcasecmp(str, "whisper") == 0) return KLATT_SRC_WHISPER;
    return -1;
}

static inline float polyBlep(float t, float dt) {
    if (dt <= 0.0f) return 0.0f;
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static inline float evalSawPolyBlep(float phase, float dt) {
    float s = 2.0f * phase - 1.0f;
    return s - polyBlep(phase, dt);
}

static inline float evalPulsePolyBlep(float phase, float dt, float duty) {
    float s = (phase < duty) ? 1.0f : -1.0f;
    s += polyBlep(phase, dt);
    float p2 = phase - duty;
    if (p2 < 0.0f) p2 += 1.0f;
    s -= polyBlep(p2, dt);
    return s;
}

static inline float evalRosenbergGlottalDeriv(float phase) {
    // Smooth differentiated Rosenberg glottal pulse (Tp = 0.42, Tn = 0.16)
    const float Tp = 0.42f;
    const float Tn = 0.16f;
    if (phase < Tp) {
        return 0.56f * sinf((float)M_PI * phase / Tp);
    }
    float tClose = phase - Tp;
    if (tClose < Tn) {
        return -1.45f * sinf(((float)M_PI * 0.5f) * tClose / Tn);
    }
    return 0.0f;
}

struct KlattOscState {
    float p1, p2, p3;   // Phases for root, fifth, octave
    uint32_t rng;       // Fast xorshift32 noise
    float prevNoise;
    float tiltLp;

    void init() {
        p1 = p2 = p3 = 0.0f;
        rng = 0x13579BDFu;
        prevNoise = 0.0f;
        tiltLp = 0.0f;
    }

    inline float nextWhiteNoise() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        // Convert to [-1, +1]
        return ((int32_t)(rng) >> 8) * (1.0f / 8388608.0f);
    }

    inline float sampleSource(uint8_t srcType, float f0Hz, float& outWhiteNoise, float& outDiffNoise) {
        outWhiteNoise = nextWhiteNoise();
        outDiffNoise = (outWhiteNoise - prevNoise) * 0.5f;
        prevNoise = outWhiteNoise;

        float fs = (float)KLATT_SAMPLE_RATE;
        if (f0Hz < 45.0f) f0Hz = 45.0f;
        if (f0Hz > 500.0f) f0Hz = 500.0f;

        float dt1 = f0Hz / fs;
        p1 += dt1;
        if (p1 >= 1.0f) p1 -= 1.0f;

        float raw = 0.0f;
        switch (srcType) {
            case KLATT_SRC_SAW:
                raw = evalSawPolyBlep(p1, dt1) * 0.75f;
                tiltLp += 0.42f * (raw - tiltLp); // Gentle spectral tilt like talkbox driver
                return tiltLp;

            case KLATT_SRC_PULSE:
                raw = evalPulsePolyBlep(p1, dt1, 0.25f) * 0.72f;
                tiltLp += 0.46f * (raw - tiltLp);
                return tiltLp;

            case KLATT_SRC_SQUARE:
                raw = evalPulsePolyBlep(p1, dt1, 0.50f) * 0.72f;
                tiltLp += 0.44f * (raw - tiltLp);
                return tiltLp;

            case KLATT_SRC_CHORD: {
                // Daft Punk vocoder chord: Root + Perfect 5th (1.4983x) + Octave (2.0x)
                float dt2 = dt1 * 1.498307f;
                float dt3 = dt1 * 2.0f;
                p2 += dt2;
                if (p2 >= 1.0f) p2 -= 1.0f;
                p3 += dt3;
                if (p3 >= 1.0f) p3 -= 1.0f;

                raw = 0.52f * evalSawPolyBlep(p1, dt1) +
                      0.30f * evalSawPolyBlep(p2, dt2) +
                      0.26f * evalSawPolyBlep(p3, dt3);
                tiltLp += 0.48f * (raw - tiltLp);
                return tiltLp;
            }

            case KLATT_SRC_WHISPER:
                return outWhiteNoise * 0.45f;

            case KLATT_SRC_GLOTTAL:
            default:
                // Warm glottal derivative + subtle natural breathiness
                return evalRosenbergGlottalDeriv(p1) + 0.025f * outWhiteNoise;
        }
    }
};

// ============================================================================
// Prosody & Acoustic Target Helpers
// ============================================================================
struct ClauseTiming {
    int clauseStartIdx;
    int clauseEndIdx;
    int totalVowels;
    int firstStressedVowelIdx;
    bool endsWithQuestion;
};

static void scanNextClause(const KlattPhoneme* phones, int numPhones, int fromIdx, ClauseTiming& ct) {
    ct.clauseStartIdx = fromIdx;
    ct.clauseEndIdx = numPhones - 1;
    ct.totalVowels = 0;
    ct.firstStressedVowelIdx = -1;
    ct.endsWithQuestion = false;

    for (int i = fromIdx; i < numPhones; i++) {
        uint8_t id = phones[i].id;
        if (id >= PH_AA && id <= PH_UW) {
            ct.totalVowels++;
            if (ct.firstStressedVowelIdx < 0 && phones[i].stress > 0) {
                ct.firstStressedVowelIdx = i;
            }
        }
        if (i > fromIdx && id == PH_SIL &&
            (phones[i].flags & (KLATT_FL_CLAUSE_END | KLATT_FL_COMMA))) {
            ct.clauseEndIdx = i;
            ct.endsWithQuestion = (phones[i].flags & KLATT_FL_QUESTION) != 0;
            break;
        }
    }
}

static float computeSegmentDurationMs(const KlattPhoneme* phones, int numPhones, int idx, const KlattConfig& cfg) {
    const KlattPhoneme& ph = phones[idx];
    const PhoneTarget& pt = kPhoneTable[ph.id];

    if (ph.id == PH_SIL) {
        if (idx == 0) return 35.0f;
        if (idx == numPhones - 1) {
            return (ph.flags & KLATT_FL_CLAUSE_END) ? 90.0f : 65.0f;
        }
        if (ph.flags & KLATT_FL_CLAUSE_END) return 210.0f * cfg.durationStretch;
        if (ph.flags & KLATT_FL_COMMA)      return 135.0f * cfg.durationStretch;
        return 60.0f * cfg.durationStretch;
    }

    float dur = pt.durMs * cfg.durationStretch;

    if (ph.stress > 0 && pt.cls != PC_SILENCE) {
        dur *= 1.15f;
    }

    if (cfg.britishRp) {
        if (ph.flags & KLATT_FL_LONG) {
            dur *= 1.32f;
        } else if (ph.flags & KLATT_FL_GLIDE) {
            dur *= 0.68f;
        } else if (ph.id == PH_AX) {
            dur *= 0.86f;
        } else if (ph.id == PH_IH || ph.id == PH_EH || ph.id == PH_AE || ph.id == PH_UH || ph.id == PH_AH || ph.id == PH_AO) {
            dur *= 0.90f; // Brisk clipped British checked vowels
        }
        if (ph.id == PH_T) {
            dur *= 1.08f; // Crisp /t/
        }
    }

    // Phrase-final vowel lengthening before terminal pause
    if (idx + 1 < numPhones && phones[idx + 1].id == PH_SIL &&
        (phones[idx + 1].flags & (KLATT_FL_CLAUSE_END | KLATT_FL_COMMA))) {
        if (pt.cls == PC_VOWEL || pt.cls == PC_DIPHTHONG || pt.cls == PC_NASAL) {
            dur *= 1.22f;
        }
    }

    if (dur < 24.0f) dur = 24.0f;
    return dur;
}

static inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

// Evaluate instantaneous acoustic target for phoneme `idx` at normalized fraction u in [0, 1)
static void evalPhoneFrame(const KlattPhoneme* phones, int numPhones, int idx, float u,
                           const KlattConfig& cfg, float& outF1, float& outF2, float& outF3,
                           float& outB1, float& outB2, float& outB3,
                           float& outAv, float& outAf, float& outAh,
                           bool& outNasal, float& outFnz,
                           float& outA2, float& outA3, float& outA4, float& outA5, float& outA6, float& outAb) {
    const KlattPhoneme& ph = phones[idx];
    const PhoneTarget& cur = kPhoneTable[ph.id];

    float f1 = cur.f1;
    float f2 = cur.f2;
    float f3 = cur.f3;
    float f1e = cur.f1End;
    float f2e = cur.f2End;
    float f3e = cur.f3End;

    // Fronted British RP GOAT diphthong [əʊ]
    if (ph.id == PH_OW && (ph.flags & KLATT_FL_BRITISH_OW)) {
        f1 = 510.0f; f2 = 1420.0f; f3 = 2450.0f;
        f1e = 350.0f; f2e = 920.0f; f3e = 2280.0f;
    }

    // Diphthong internal glide from 20% to 85%
    if (cur.cls == PC_DIPHTHONG) {
        float dt = (u - 0.20f) / 0.65f;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 1.0f) dt = 1.0f;
        f1 = lerpf(f1, f1e, dt);
        f2 = lerpf(f2, f2e, dt);
        f3 = lerpf(f3, f3e, dt);
    }

    // Smooth vowel/liquid coarticulation toward neighbors in first/last 22% of segment
    if (cur.cls == PC_VOWEL || cur.cls == PC_DIPHTHONG || cur.cls == PC_LIQUID || cur.cls == PC_NASAL) {
        if (u < 0.22f && idx > 0 && phones[idx - 1].id != PH_SIL) {
            const PhoneTarget& prev = kPhoneTable[phones[idx - 1].id];
            float blend = 0.35f * (1.0f - u / 0.22f);
            float pf1 = (prev.cls == PC_DIPHTHONG) ? prev.f1End : prev.f1;
            float pf2 = (prev.cls == PC_DIPHTHONG) ? prev.f2End : prev.f2;
            float pf3 = (prev.cls == PC_DIPHTHONG) ? prev.f3End : prev.f3;
            f1 = lerpf(f1, pf1, blend);
            f2 = lerpf(f2, pf2, blend);
            f3 = lerpf(f3, pf3, blend);
        } else if (u > 0.78f && idx + 1 < numPhones && phones[idx + 1].id != PH_SIL) {
            const PhoneTarget& nxt = kPhoneTable[phones[idx + 1].id];
            float blend = 0.35f * ((u - 0.78f) / 0.22f);
            f1 = lerpf(f1, nxt.f1, blend);
            f2 = lerpf(f2, nxt.f2, blend);
            f3 = lerpf(f3, nxt.f3, blend);
        }
    }

    // Look-ahead formant transition for stops & H into following vowel
    if ((cur.cls == PC_STOP_VOICELESS || cur.cls == PC_STOP_VOICED || cur.cls == PC_AFFRICATE || ph.id == PH_HH) &&
        idx + 1 < numPhones && phones[idx + 1].id != PH_SIL) {
        const PhoneTarget& nxt = kPhoneTable[phones[idx + 1].id];
        float blend = (ph.id == PH_HH) ? (0.45f + 0.55f * u) : (0.15f + 0.55f * u);
        f1 = lerpf(f1, nxt.f1, blend);
        f2 = lerpf(f2, nxt.f2, blend);
        f3 = lerpf(f3, nxt.f3, blend);
    }

    // Scale formants & bandwidths for voice persona
    float fScale = (cfg.formantScale > 0.3f) ? cfg.formantScale : 1.0f;
    float bwScale = (cfg.bwScale > 0.25f) ? cfg.bwScale : 1.0f;
    outF1 = f1 * fScale;
    outF2 = f2 * fScale;
    outF3 = f3 * fScale;
    outB1 = cur.b1 * bwScale;
    outB2 = cur.b2 * bwScale;
    outB3 = cur.b3 * bwScale;

    outAv = cur.av;
    outAf = cur.af;
    outAh = cur.ah;
    outNasal = cur.isNasal;
    outFnz = cur.fnz;
    outA2 = cur.a2;
    outA3 = cur.a3;
    outA4 = cur.a4;
    outA5 = cur.a5;
    outA6 = cur.a6;
    outAb = cur.ab;

    // Two-phase envelopes for stops, affricates, fricatives, and vowels
    if (cur.cls == PC_STOP_VOICELESS) {
        if (u < 0.55f) {
            // Silent closure
            outAf = 0.0f;
            outAh = 0.0f;
        } else if (u < 0.74f) {
            // Sharp release burst
            outAh = cur.ah * 0.35f;
        } else {
            // Aspiration VOT tail into next vowel
            float t = (u - 0.74f) / 0.26f;
            outAf = 0.0f;
            outAh = cur.ah * (1.0f - 0.5f * t);
        }
    } else if (cur.cls == PC_STOP_VOICED) {
        if (u < 0.76f) {
            // Low voicebar murmur
            outAf = 0.0f;
            outAv = cur.av;
        } else {
            // Voiced release burst
            outAv = cur.av * 0.75f;
            outAf = cur.af;
        }
    } else if (cur.cls == PC_AFFRICATE) {
        if (u < 0.35f) {
            outAf = 0.0f;
            outAv = cur.av * 0.4f;
        } else {
            float env = sinf((float)M_PI * (u - 0.35f) / 0.65f);
            outAf = cur.af * (0.4f + 0.6f * env);
        }
    } else if (cur.cls == PC_FRICATIVE) {
        float env = 1.0f;
        if (u < 0.15f) env = u / 0.15f;
        else if (u > 0.85f) env = (1.0f - u) / 0.15f;
        outAf *= env;
        outAh *= env;
    } else if (cur.cls == PC_VOWEL || cur.cls == PC_DIPHTHONG || cur.cls == PC_NASAL || cur.cls == PC_LIQUID) {
        // Gentle onset/offset envelope across adjacent silences
        if (u < 0.12f && (idx == 0 || phones[idx - 1].id == PH_SIL)) {
            outAv *= (0.25f + 0.75f * (u / 0.12f));
        }
        if (u > 0.85f && (idx + 1 >= numPhones || phones[idx + 1].id == PH_SIL)) {
            outAv *= (1.0f - 0.70f * ((u - 0.85f) / 0.15f));
        }
    }

    if (cfg.sourceType == KLATT_SRC_WHISPER) {
        // Pure whisper mode swaps glottal voicing for cascade aspiration
        if (outAv > 0.0f) {
            outAh = outAh + outAv * 0.62f;
            outAv = 0.0f;
        }
    }
}

// Quantized musical intervals (in semitones) for stepped Daft Punk vocoder mode
static const float kVocoderMelodicSemitones[] = {
    0.0f, 3.0f, 5.0f, 3.0f, 0.0f, -2.0f, 3.0f, 0.0f
};

static float computeTargetF0(int phoneIdx, float u, const KlattPhoneme* phones,
                             const ClauseTiming& ct, int wordIndexInClause, int vowelCountSeen,
                             const KlattConfig& cfg) {
    float mean = cfg.pitchMeanHz;
    if (mean < 45.0f) mean = 45.0f;
    if (mean > 450.0f) mean = 450.0f;

    if (cfg.steppedPitch) {
        // Musical vocoder note progression: hold crisp pitch per word/clause step
        int steps = (int)(sizeof(kVocoderMelodicSemitones) / sizeof(kVocoderMelodicSemitones[0]));
        float semi = kVocoderMelodicSemitones[wordIndexInClause % steps];
        // Terminal downward cadence on final stressed word of a statement (or +5 on question)
        if (ct.totalVowels >= 2 && vowelCountSeen >= ct.totalVowels - 1) {
            semi = ct.endsWithQuestion ? 5.0f : -2.0f;
        }
        float scale = powf(2.0f, semi / 12.0f);
        // Very slight expressive vibrato if pitchStdDevHz > 10
        return mean * scale;
    }

    float dev = cfg.pitchStdDevHz;
    float clauseSpan = (float)((ct.clauseEndIdx > ct.clauseStartIdx) ? (ct.clauseEndIdx - ct.clauseStartIdx) : 1);
    float p = ((float)(phoneIdx - ct.clauseStartIdx) + u) / clauseSpan;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;

    // Gradual baseline declination across clause
    float f0 = mean + dev * (0.35f - 0.70f * p);

    const KlattPhoneme& ph = phones[phoneIdx];
    bool isStressed = (ph.stress > 0);

    if (cfg.britishRp) {
        // British RP "High Head" on first stressed syllable + stepped high-falling accents
        if (isStressed) {
            if (phoneIdx == ct.firstStressedVowelIdx) {
                f0 += dev * (0.85f * sinf((float)M_PI * (0.25f + 0.55f * u)));
            } else {
                // High fall on stressed syllable
                f0 += dev * (0.65f - 0.85f * u);
            }
        } else {
            // Unstressed syllable sits dry & flat slightly below baseline
            f0 -= dev * 0.25f;
        }
    } else {
        // Standard smooth stress hump
        if (isStressed) {
            f0 += dev * 0.65f * sinf((float)M_PI * u);
        }
    }

    // Clause-final boundary tone (before terminal silence)
    if (ct.totalVowels >= 1 && vowelCountSeen >= ct.totalVowels) {
        if (ct.endsWithQuestion) {
            f0 += dev * (0.95f * u);
        } else {
            f0 -= dev * (cfg.britishRp ? (0.45f + 0.65f * u) : (0.55f * u));
        }
    }

    if (f0 < 45.0f) f0 = 45.0f;
    if (f0 > 480.0f) f0 = 480.0f;
    return f0;
}

// ============================================================================
// Streaming 16 kHz Klatt-80 Synthesis Entry Point
// ============================================================================
bool klattSynthesize(const KlattPhoneme* phones, int numPhones, const KlattConfig& cfg,
                     KlattSampleCallback cb, void* userData) {
    if (!phones || numPhones <= 0 || !cb) return false;

    KlattResonator rnp, rnz, r1, r2, r3, r4, r5;
    ParallelBandpass p2, p3, p4, p5, p6;
    rnp.reset(); rnz.reset();
    r1.reset(); r2.reset(); r3.reset(); r4.reset(); r5.reset();
    p2.reset(); p3.reset(); p4.reset(); p5.reset(); p6.reset();

    rnp.setCascade(270.0f, 100.0f);
    float fScale = (cfg.formantScale > 0.3f) ? cfg.formantScale : 1.0f;
    r4.setCascade(3300.0f * fScale, 250.0f);
    r5.setCascade(3850.0f * fScale, 300.0f);

    KlattOscState osc;
    osc.init();

    // Smoothed parameter state across 5 ms frames (80 samples at 16 kHz)
    static constexpr int FRAME_SAMPLES = 80;
    float curAv = 0.0f, curAf = 0.0f, curAh = 0.0f;
    float curA2 = 0.0f, curA3 = 0.0f, curA4 = 0.0f, curA5 = 0.0f, curA6 = 0.0f, curAb = 0.0f;
    float curF0 = cfg.pitchMeanHz;

    ClauseTiming ct;
    scanNextClause(phones, numPhones, 0, ct);
    int wordIdxInClause = 0;
    int vowelsSeenInClause = 0;

    // Normalize cascade amplitude gently when bandwidths are tightened (prevents resonant overload)
    float bwNorm = sqrtf((cfg.bwScale > 0.25f) ? cfg.bwScale : 1.0f);
    float cascadeMasterScale = 0.24f * bwNorm;
    if (cfg.sourceType == KLATT_SRC_CHORD) {
        cascadeMasterScale *= 0.85f;
    }

    for (int i = 0; i < numPhones; i++) {
        const KlattPhoneme& ph = phones[i];

        if (i > ct.clauseEndIdx) {
            scanNextClause(phones, numPhones, i, ct);
            wordIdxInClause = 0;
            vowelsSeenInClause = 0;
        }
        if (ph.flags & KLATT_FL_WORD_START) {
            wordIdxInClause++;
        }
        if (ph.id >= PH_AA && ph.id <= PH_UW) {
            vowelsSeenInClause++;
        }

        float durMs = computeSegmentDurationMs(phones, numPhones, i, cfg);
        int totalSamples = (int)(durMs * (float)KLATT_SAMPLE_RATE / 1000.0f + 0.5f);
        if (totalSamples < FRAME_SAMPLES) totalSamples = FRAME_SAMPLES;

        int pos = 0;
        while (pos < totalSamples) {
            int chunk = totalSamples - pos;
            if (chunk > FRAME_SAMPLES) chunk = FRAME_SAMPLES;

            float uMid = ((float)pos + 0.5f * (float)chunk) / (float)totalSamples;

            float f1, f2, f3, b1, b2, b3;
            float tAv, tAf, tAh, tFnz;
            float tA2, tA3, tA4, tA5, tA6, tAb;
            bool isNasal = false;

            evalPhoneFrame(phones, numPhones, i, uMid, cfg,
                           f1, f2, f3, b1, b2, b3,
                           tAv, tAf, tAh, isNasal, tFnz,
                           tA2, tA3, tA4, tA5, tA6, tAb);

            float tF0 = computeTargetF0(i, uMid, phones, ct, wordIdxInClause, vowelsSeenInClause, cfg);

            // Configure resonators for this 5 ms frame
            r1.setCascade(f1, b1);
            r2.setCascade(f2, b2);
            r3.setCascade(f3, b3);
            if (isNasal) {
                rnz.setAntiResonator(tFnz, 120.0f);
            } else {
                rnz.setAntiResonator(270.0f, 100.0f); // Cancels rnp cleanly
            }

            p2.set(f2, b2 * 1.4f);
            p3.set(f3, b3 * 1.4f);
            p4.set(3300.0f * fScale, 280.0f);
            p5.set(3850.0f * fScale, 360.0f);
            p6.set(4900.0f, 650.0f);

            // Smooth amplitude and F0 ramps within the 5 ms frame
            float invChunk = 1.0f / (float)chunk;
            float dAv = (tAv - curAv) * invChunk;
            float dAf = (tAf - curAf) * invChunk;
            float dAh = (tAh - curAh) * invChunk;
            float dA2 = (tA2 - curA2) * invChunk;
            float dA3 = (tA3 - curA3) * invChunk;
            float dA4 = (tA4 - curA4) * invChunk;
            float dA5 = (tA5 - curA5) * invChunk;
            float dA6 = (tA6 - curA6) * invChunk;
            float dAb = (tAb - curAb) * invChunk;
            float dF0 = (tF0 - curF0) * invChunk;
            if (cfg.steppedPitch) {
                curF0 = tF0; // Crisp quantized musical pitch in vocoder mode
                dF0 = 0.0f;
            }

            for (int n = 0; n < chunk; n++) {
                curAv += dAv;
                curAf += dAf;
                curAh += dAh;
                curA2 += dA2;
                curA3 += dA3;
                curA4 += dA4;
                curA5 += dA5;
                curA6 += dA6;
                curAb += dAb;
                curF0 += dF0;

                float whiteNoise = 0.0f, diffNoise = 0.0f;
                float glottal = osc.sampleSource(cfg.sourceType, curF0, whiteNoise, diffNoise);

                // Cascade branch: voicing + aspiration -> RNP -> RNZ -> R1 -> R2 -> R3 -> R4 -> R5
                float casIn = (glottal * curAv + whiteNoise * curAh) * cascadeMasterScale;
                float cas = rnp.processPole(casIn);
                cas = rnz.processZero(cas);
                cas = r1.processPole(cas);
                cas = r2.processPole(cas);
                cas = r3.processPole(cas);
                cas = r4.processPole(cas);
                cas = r5.processPole(cas);

                // Parallel frication branch (Klatt alternating signs to minimize inter-formant zeros)
                float fric = diffNoise * curAf * 0.42f;
                float par = 0.0f;
                if (curAf > 0.001f) {
                    par += curA2 * p2.process(fric);
                    par -= curA3 * p3.process(fric);
                    par += curA4 * p4.process(fric);
                    par -= curA5 * p5.process(fric);
                    par += curA6 * p6.process(fric);
                    par -= curAb * fric * 0.55f;
                } else {
                    // Decay parallel filter tails smoothly
                    p2.process(0.0f);
                    p3.process(0.0f);
                    p4.process(0.0f);
                    p5.process(0.0f);
                    p6.process(0.0f);
                }

                float out = (cas + par) * 0.90f;
                // Gentle safety soft-clipping before passing to 44.1 kHz Droid DSP
                out = out / (1.0f + fabsf(out) * 0.25f);

                if (!cb(out, userData)) {
                    return false;
                }
            }

            pos += chunk;
        }
    }

    return true;
}
