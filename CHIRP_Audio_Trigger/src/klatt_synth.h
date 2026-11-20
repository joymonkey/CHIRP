#ifndef KLATT_SYNTH_H
#define KLATT_SYNTH_H

#include <stdint.h>
#include <stddef.h>

#define KLATT_SAMPLE_RATE 16000
#define KLATT_MAX_PHONEMES 512

enum KlattSourceType : uint8_t {
    KLATT_SRC_GLOTTAL = 0, // Smooth differentiated Rosenberg glottal pulse
    KLATT_SRC_SAW     = 1, // PolyBLEP sawtooth (talkbox / vocoder)
    KLATT_SRC_PULSE   = 2, // 25% duty PolyBLEP pulse (metallic nasal B1 buzz)
    KLATT_SRC_SQUARE  = 3, // 50% duty PolyBLEP square (retro robot)
    KLATT_SRC_CHORD   = 4, // Harmonized root + 5th + octave saw chord (DAFT vocoder)
    KLATT_SRC_WHISPER = 5, // Pure aspiration noise
    KLATT_SRC_COUNT   = 6
};

enum KlattPhoneId : uint8_t {
    PH_SIL = 0,
    // Vowels & diphthongs
    PH_AA, PH_AE, PH_AH, PH_AO, PH_AW, PH_AX, PH_AY,
    PH_EH, PH_ER, PH_EY, PH_IH, PH_IY, PH_OW, PH_OY,
    PH_UH, PH_UW,
    // Stops
    PH_P, PH_B, PH_T, PH_D, PH_K, PH_G,
    // Affricates
    PH_CH, PH_JH,
    // Fricatives
    PH_F, PH_V, PH_TH, PH_DH, PH_S, PH_Z, PH_SH, PH_ZH, PH_HH,
    // Nasals
    PH_M, PH_N, PH_NG,
    // Liquids & glides
    PH_L, PH_R, PH_W, PH_Y,
    PH_COUNT
};

enum KlattPhoneFlags : uint8_t {
    KLATT_FL_NONE       = 0,
    KLATT_FL_LONG       = 1 << 0,
    KLATT_FL_GLIDE      = 1 << 1,
    KLATT_FL_CLAUSE_END = 1 << 2,
    KLATT_FL_QUESTION   = 1 << 3,
    KLATT_FL_COMMA      = 1 << 4,
    KLATT_FL_WORD_START = 1 << 5,
    KLATT_FL_BRITISH_OW = 1 << 6
};

struct KlattPhoneme {
    uint8_t id;
    uint8_t stress; // 0 = unstressed, 1 = primary/secondary stress
    uint8_t flags;  // KlattPhoneFlags bitmask
};

struct KlattConfig {
    float pitchMeanHz;
    float pitchStdDevHz;
    float durationStretch;
    float formantScale;
    float bwScale;        // Formant bandwidth multiplier (<1.0 sharper/resonant, 1.0 natural)
    uint8_t sourceType;   // KlattSourceType
    bool britishRp;       // British RP vowel lengthening & High-Head/terminal-drop cadence
    bool steppedPitch;    // Musical quantized vocoder pitch contour (DAFT mode)
};

// Return false from callback to abort synthesis early (e.g., stream stopped)
typedef bool (*KlattSampleCallback)(float sample, void* userData);

/**
 * Translate ASCII English text (words + punctuation) into KlattPhoneme sequence.
 * Optionally applies British RP post-lexical rules. Returns number of phonemes written.
 */
int klattTextToPhonemes(const char* text, bool britishRp, KlattPhoneme* outPhones, int maxPhones);

/**
 * Convert a non-negative integer into English cardinal words (up to 999,999,999,999;
 * larger values are spoken digit-by-digit).
 */
void klattNumberToWords(uint64_t n, char* out, size_t outSize);

/**
 * Convert a non-negative integer into English ordinal words (e.g. "first", "twenty second").
 */
void klattOrdinalToWords(uint64_t n, char* out, size_t outSize);

/**
 * Stream 16 kHz normalized mono samples (-1..+1) through Klatt-80 cascade/parallel synthesis.
 */
bool klattSynthesize(const KlattPhoneme* phones, int numPhones, const KlattConfig& cfg,
                     KlattSampleCallback cb, void* userData);

const char* klattSourceTypeName(uint8_t src);
int klattParseSourceType(const char* str);

#endif // KLATT_SYNTH_H
