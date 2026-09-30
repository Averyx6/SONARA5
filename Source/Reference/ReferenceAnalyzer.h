#pragma once
#include <JuceHeader.h>
#include <vector>

namespace sonara {

struct ReferenceNote {
    double beat = 0.0;
    double length = 0.5;
    int midiNote = 60;
    int velocity = 96;
};

struct ReferenceAnalysis {
    juce::String fileName;
    juce::String keyName { "Unknown" };
    double sampleRate = 0.0;
    double durationSeconds = 0.0;
    double estimatedBpm = 120.0;
    float rmsDb = -100.f;
    float peakDb = -100.f;
    std::vector<ReferenceNote> melody;

    bool valid() const noexcept { return sampleRate > 0.0 && durationSeconds > 0.0 && !melody.empty(); }
    double melodyBeats() const noexcept;
    juce::String summary() const;
};

class ReferenceAnalyzer {
public:
    ReferenceAnalysis analyseAudio(const juce::File& file) const;
    ReferenceAnalysis importMidi(const juce::File& file) const;
    bool writeMelodyMidi(const ReferenceAnalysis&, const juce::File& destination) const;

private:
    static double estimateTempo(const std::vector<float>& mono, double sampleRate);
    static std::vector<ReferenceNote> extractPitchContour(const std::vector<float>& mono, double sampleRate, double bpm, float globalRms);
    static juce::String estimateKey(const std::vector<ReferenceNote>& notes);
    static int estimateMidiPitch(const float* data, int size, double sampleRate, float& confidence) noexcept;
};

} // namespace sonara