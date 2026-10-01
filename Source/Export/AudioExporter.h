#pragma once
#include <JuceHeader.h>
#include "../Generation/SongArrangement.h"
#include "../Engine/SonaraEngine.h"
#include "../Song/DrumSynth.h"
#include "../Reference/ReferenceAnalyzer.h"
#include <functional>
#include <array>

namespace sonara {

class AudioExporter {
public:
    using Progress = std::function<void(float,const juce::String&)>;
    struct MixState { float level=1.f,pan=0.f,width=1.f,fxSend=1.f; };
    using MixArray = std::array<MixState,12>;

    bool renderFullMix(const SongArrangement&, const juce::File& destination, double sampleRate = 44100.0, Progress = {}, const MixArray* mix = nullptr) const;
    bool renderSelectedLane(const SongArrangement&, int laneIndex, const juce::File& destination, double sampleRate = 44100.0, Progress = {}) const;
    bool renderReferenceMelody(const ReferenceAnalysis&, const SoundDNA&, const juce::File& destination, double sampleRate = 44100.0, Progress = {}) const;
    bool renderAllStems(const SongArrangement&, const juce::File& directory, double sampleRate = 44100.0, Progress = {}) const;

private:
    static bool createWavWriter(const juce::File&, double, std::unique_ptr<juce::AudioFormatWriter>&);
    static void injectLaneMidi(const ArrangementLane&, juce::MidiBuffer&, int64_t startSample, int numSamples, double bpm, double sampleRate);
    static int collectDrumTriggers(const ArrangementLane&, int64_t startSample, int numSamples, double bpm, double sampleRate, DrumTrigger* out, int capacity);
    static juce::String safeFileName(const juce::String&);
};

} // namespace sonara