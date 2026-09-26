#pragma once
#include <JuceHeader.h>
#include "../Engine/SoundDNA.h"
#include <vector>

namespace sonara {

struct ArrangementNote {
    int note = 60;
    int velocity = 100;
    double beat = 0.0;
    double length = 0.5;
};

struct ArrangementLane {
    juce::String name;
    int midiChannel = 1;
    bool drums = false;
    std::vector<ArrangementNote> notes;
    SoundDNA sound;
};

struct ArrangementSection {
    juce::String name;
    int startBar = 0;
    int bars = 8;
    float energy = 0.5f;
};

class SongArrangement {
public:
    static constexpr int beatsPerBar = 4;
    static constexpr int defaultBars = 72;

    void generate(const juce::String& prompt, double bpm, uint64_t seed);
    void clear();
    bool writeMidiFile(const juce::File& destination) const;
    juce::ValueTree toValueTree() const;
    static SongArrangement fromValueTree(const juce::ValueTree&);

    const std::vector<ArrangementLane>& getLanes() const noexcept { return lanes; }
    std::vector<ArrangementLane>& editLanes() noexcept { return lanes; }
    const std::vector<ArrangementSection>& getSections() const noexcept { return sections; }
    int getBars() const noexcept { return bars; }
    double getBpm() const noexcept { return tempo; }
    int getRootMidi() const noexcept { return rootMidi; }
    bool isMinor() const noexcept { return minor; }
    double getTotalBeats() const noexcept { return static_cast<double>(bars * beatsPerBar); }
    bool isEmpty() const noexcept { return lanes.empty(); }

private:
    static uint64_t mix64(uint64_t x) noexcept;
    static float random01(uint64_t seed, uint64_t salt) noexcept;
    static int parseRootMidi(const juce::String& prompt, bool& minorOut);
    void buildSections(uint64_t seed);
    void addDrums(uint64_t seed, bool energetic);
    void addHarmony(uint64_t seed);
    void addMelody(uint64_t seed, bool energetic);
    void addFx(uint64_t seed);

    juce::String sourcePrompt;
    double tempo = 128.0;
    int bars = defaultBars;
    int rootMidi = 53; // F3
    bool minor = true;
    std::vector<ArrangementLane> lanes;
    std::vector<ArrangementSection> sections;
};

} // namespace sonara