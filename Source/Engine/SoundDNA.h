#pragma once
#include <JuceHeader.h>

namespace sonara {
enum class WaveShape : int { sine=0, triangle, saw, square, softSaw };
enum class FilterMode : int { lowpass=0, highpass, bandpass };
enum class LfoShape : int { sine=0, triangle, sawUp, sawDown, square };
struct SoundDNA {
    uint64_t seed = 1;
    WaveShape oscA = WaveShape::saw, oscB = WaveShape::softSaw;
    float oscMix = 0.5f, oscBTranspose = 0.0f;
    float oscAMorph = 0.0f, oscBMorph = 0.0f;
    int unison = 5;
    float detune = 0.12f, unisonBlend = 0.72f, phaseRandom = 0.65f;
    float subLevel = 0.0f, subOctave = -1.0f, noiseLevel = 0.0f;
    float pitchEnv = 0.0f, pitchEnvDecay = 0.12f;
    float transientLevel = 0.0f, transientDecay = 0.025f;
    float fmAmount = 0.0f, fmRatio = 2.0f, ringMod = 0.0f;
    float bitCrush = 0.0f, downsample = 0.0f;
    float attack = 0.01f, decay = 0.25f, sustain = 0.75f, release = 0.45f;
    FilterMode filterMode = FilterMode::lowpass;
    float cutoff = 12000.0f, resonance = 0.15f, filterEnv = 0.15f;
    LfoShape lfoShape = LfoShape::sine;
    float lfoRate = 0.35f, lfoCutoff = 0.0f, lfoPitch = 0.0f;
    float lfoMorphA = 0.0f, lfoMorphB = 0.0f;
    float drive = 0.08f, width = 0.7f;
    float chorus = 0.0f, chorusRate = 0.32f, chorusDepth = 0.45f;
    float reverb = 0.2f, delay = 0.12f;
    float macroBrightness = 0.5f, macroMovement = 0.5f, macroSpace = 0.5f, macroImpact = 0.5f;
    juce::String name { "Init" }, sourcePrompt;

    void copyDSPFrom(const SoundDNA&) noexcept;
    juce::ValueTree toValueTree() const;
    static SoundDNA fromValueTree(const juce::ValueTree&);
};
}