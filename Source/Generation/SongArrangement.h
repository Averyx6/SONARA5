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
    struct SongPlan {
        int structureStyle=0;
        int drumGroove=0;
        int hatMode=0;
        int progressionIndex=0;
        int alternateProgressionIndex=1;
        int bassMode=0;
        int chordMode=0;
        int arpMode=0;
        int melodyArchetype=0;
        int rhythmFamily=0;
        int startingDegree=0;
        int cadenceStyle=0;
        int motifLength=8;
        int phraseBars=4;
        int octaveRange=2;
        float density=.65f;
        float syncopation=.35f;
        float restAmount=.18f;
        float development=.55f;
    };

    static uint64_t mix64(uint64_t x) noexcept;
    static float random01(uint64_t seed, uint64_t salt) noexcept;
    static int parseRootMidi(const juce::String& prompt, bool& minorOut);
    void buildSongPlan(uint64_t seed);
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
    SongPlan plan;
};

} // namespace sonara