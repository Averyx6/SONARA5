#pragma once
#include <JuceHeader.h>
#include "../Generation/SongArrangement.h"
#include "../Engine/SongRenderEngine.h"
#include "../Song/DrumSynth.h"
#include "../Reference/ReferenceAnalyzer.h"
#include <functional>
#include <array>

namespace sonara {

class AudioExporter {
public:
    void setCancelFlag(const std::atomic<bool>* value) noexcept {cancel=value;}
    static SongArrangement makeReferenceSong(const ReferenceAnalysis&,const SoundDNA&);
    using Progress = std::function<void(float,const juce::String&)>;
    using MixState=SongMixState;
    using MixArray=SongMixArray;

    bool renderFullMix(const SongArrangement&, const juce::File& destination, double sampleRate = 44100.0, Progress = {}, const MixArray* mix = nullptr) const;
    bool renderSelectedLane(const SongArrangement&, int laneIndex, const juce::File& destination, double sampleRate = 44100.0, Progress = {}, const MixArray* mix = nullptr) const;
    bool renderReferenceMelody(const ReferenceAnalysis&, const SoundDNA&, const juce::File& destination, double sampleRate = 44100.0, Progress = {}) const;
    bool renderAllStems(const SongArrangement&, const juce::File& directory, double sampleRate = 44100.0, Progress = {}, const MixArray* mix = nullptr) const;

private:
    const std::atomic<bool>* cancel=nullptr;
    bool renderSong(const SongArrangement&,const juce::File&,double,Progress,const MixArray*,int) const;
    static bool createWavWriter(const juce::File&, double, std::unique_ptr<juce::AudioFormatWriter>&);
    static juce::String safeFileName(const juce::String&);
};

} // namespace sonara