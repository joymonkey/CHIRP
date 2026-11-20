#ifndef VOICE_SYNTH_H
#define VOICE_SYNTH_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include "klatt_synth.h"

// ============================================================================
// HOW TO DEFINE A NEW DROID VOICE PIPELINE:
// ============================================================================
// Each VoicePipeline struct defines:
//   1. Klatt Synthesis Prosody (pitchMeanHz, pitchStdDevHz, durationStretch)
//   2. Vocal Tract Formant Scaling (formantScale: <1.0 = deeper/larger head,
//      >1.0 = smaller/nasal throat; pitch & speed are automatically compensated)
//   3. Pitch Wobble LFO (wobbleHz, wobbleDepth: subtle vocoder instability)
//   4. 3-Band Biquad EQ (High-Pass cutoff, Peaking formant freq/dB/Q, Low-Pass cutoff)
//   5. Ring Modulator (ringModHz, ringModMix: metallic inharmonic sidebands)
//   6. Metallic Chassis Comb Resonator (combDelayMs, combFeedback, combDamping, combMix)
//   7. Vocoder Grit & Output (crushBits 8..16, driveGain, outputGain)
//
// Add a row to kBuiltInVoicePipelines[] in voice_synth.cpp or customize the
// "CUSTOM" slot at runtime via CHIRP.INI (#VOICE_CUSTOM) or Serial (VCFG:...).
// ============================================================================

struct VoicePipeline {
    const char* name;           // Mode identifier (e.g. "K2SO", "DAFT", "B1", "DROID", "CUSTOM", "MP3")
    bool useLegacyMp3;          // If true, play from sysAudioFiles[] MP3s first (fallback to synth)
    bool britishRp;             // If true, apply British RP phoneme & stepped high-falling inflection rules
    uint8_t sourceWave;         // Klatt excitation source (0=GLOTTAL, 1=SAW, 2=PULSE, 3=SQUARE, 4=CHORD, 5=WHISPER)
    float bwScale;              // Formant bandwidth multiplier (0.35..2.0; lower = sharper vocoder resonance)
    bool steppedPitch;          // If true, quantize pitch into crisp musical vocoder steps (DAFT mode)

    // Stage 0: Klatt Prosody & Formant Shift
    float pitchMeanHz;          // Target fundamental pitch F0 in Hz (e.g. 86 = deep, 175 = high)
    float pitchStdDevHz;        // Pitch intonation variance in Hz (e.g. 4.5 = deadpan, 22 = lively)
    float durationStretch;      // Tempo multiplier (<1.0 = faster, 1.0 = normal, >1.0 = slower)
    float formantScale;         // Vocal tract formant multiplier (0.88 = large skull, 1.22 = small nasal)

    // Stage 1: Pitch Wobble LFO
    float wobbleHz;             // LFO rate in Hz (0 = off)
    float wobbleDepth;          // Fractional pitch modulation depth (0.0 to 0.05)

    // Stage 2: 3-Stage Biquad Formant/Comlink EQ (at 44.1 kHz)
    float hpCutoffHz;           // High-pass cutoff in Hz (cuts bass rumble / chest warmth)
    float lpCutoffHz;           // Low-pass cutoff in Hz (limits bandwidth / smooths imaging)
    float peakFreqHz;           // Resonant throat/grille peak frequency in Hz
    float peakGainDb;           // Peak boost/cut in dB
    float peakQ;                // Peak selectivity Q (0.7 to 4.0)

    // Stage 3: Ring Modulator (Metallic Sidebands)
    float ringModHz;            // Carrier sine wave frequency in Hz
    float ringModMix;           // Wet mix 0.0 (off) to 1.0 (full ring mod)

    // Stage 4: Metallic Chassis Comb Resonator (Hollow Enclosure)
    float combDelayMs;          // Delay length in ms (e.g. 6.8 ms for K-2SO skull, 2.4 ms for B1)
    float combFeedback;         // Feedback gain (-0.85 to +0.85)
    float combDamping;          // One-pole lowpass damping in feedback loop (0.0 to 0.9)
    float combMix;              // Wet mix 0.0 (off) to 1.0

    // Stage 5: Vocoder Grit & Output Stage
    uint8_t crushBits;          // Bit depth quantization (16 = clean, 8..12 = lo-fi vocoder grit)
    float driveGain;            // Pre-saturation drive (1.0 = linear, >1.0 = warm saturation)
    float outputGain;           // Post-DSP makeup gain multiplier
};

void initVoiceSynth();
bool setVoiceModeByName(const char* modeName);
const char* getVoiceModeName();
const VoicePipeline& getActiveVoicePipeline();
VoicePipeline& getCustomVoicePipeline();
bool isCustomVoiceModified();
int getVoicePipelineCount();
const VoicePipeline* getVoicePipelineByIndex(int idx);

// Parse/format custom pipeline string for CHIRP.INI (#VOICE_CUSTOM) and VCFG command
bool parseCustomVoiceConfig(const char* configLine);
void formatCustomVoiceConfig(char* outBuf, size_t outSize);
bool applyCustomVoiceParam(const char* keyVal);
bool resetCustomVoiceFromPreset(const char* presetName);

// Normalize legacy PVOICE token or arbitrary text into speakable English phrase
#define VOICE_SYNTH_MAX_PHRASE_LEN 112
void normalizeVoiceToken(const char* rawInput, char* outPhrase, size_t outSize);

// Synthesize normalized text through Klatt + active VoicePipeline DSP into Stream 0
bool playSynthesizedVoice(const char* rawTokenOrPhrase);

#endif // VOICE_SYNTH_H
