#pragma once
#include <JuceHeader.h>
#include "../Engine/SoundDNA.h"
#include <vector>
#include <array>

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

struct SectionGoal {
    float energy = .5f;
    float density = .5f;
    float melodyActivity = .5f;
    float harmonicTension = .4f;
    float bassDrive = .5f;
    float drumDrive = .5f;
    float space = .5f;
    float development = .5f;
};

class SongArrangement {
public:
    static constexpr int beatsPerBar = 4;
    static constexpr int defaultBars = 72;
    static constexpr int melodyFingerprintVersion = 2301;
    static constexpr int melodyFingerprintStride = 11;
    static constexpr int melodyFingerprintSummarySize = 9;

    void generate(const juce::String& prompt, double bpm, uint64_t seed);
    void generateComposition(const juce::String& prompt, double bpm, uint64_t seed);
    void regenerateDrumsOnly(const juce::String& drumPrompt, uint64_t seed);
    void finalizeSoundPalette();
    void clear();
    bool writeMidiFile(const juce::File& destination) const;
    juce::ValueTree toValueTree() const;
    static SongArrangement fromValueTree(const juce::ValueTree&);

    const std::vector<ArrangementLane>& getLanes() const noexcept { return lanes; }
    std::vector<ArrangementLane>& editLanes() noexcept { return lanes; }
    const std::vector<ArrangementSection>& getSections() const noexcept { return sections; }
    const std::vector<SectionGoal>& getSectionGoals() const noexcept { return sectionGoals; }
    int getBars() const noexcept { return bars; }
    double getBpm() const noexcept { return tempo; }
    int getRootMidi() const noexcept { return rootMidi; }
    bool isMinor() const noexcept { return minor; }
    double getTotalBeats() const noexcept { return static_cast<double>(bars * beatsPerBar); }
    bool isEmpty() const noexcept { return lanes.empty(); }
    uint64_t getSongId() const noexcept { return masterSeed; }
    uint64_t getHarmonyId() const noexcept { return harmonyId; }
    uint64_t getMelodyId() const noexcept { return melodyId; }
    juce::String getHarmonySummary() const;
    juce::String getHarmonicRhythmSummary() const;
    juce::String getMelodyArchetypeName() const;
    juce::String getProducerPlanSummary() const;
    juce::String getSectionGoalSummary() const;
    juce::String getPromptIntentSummary() const;
    const juce::String& getSourcePrompt() const noexcept { return sourcePrompt; }
    juce::String getSoundPaletteSummary() const;
    std::vector<int> getSoundPaletteFingerprint() const;
    std::vector<int> getProgressionFingerprint() const;
    std::vector<int> getHarmonyFingerprint() const;
    std::vector<int> getMelodyFingerprint() const;
    std::vector<int> getStructureFingerprint() const;
    int getHarmonyProgressionLength() const noexcept { return harmonyPlan.progressionLength; }

private:
    struct PromptIntent {
        double tempo=128.0;
        int rootMidi=53;
        bool minor=true;
        bool hasExplicitKey=false;
        bool hasExplicitTempo=false;
        int genreFamily=0;
        int emotionProfile=0;
        int densityDirection=0; // -1 sparse, 0 natural, +1 dense
        int energyDirection=0;  // -1 restrained, 0 natural, +1 energetic
        int structureStyle=-1;  // auto, radio, festival, progressive, cinematic
        int targetBars=0;
        int hookShape=-1;
        unsigned exclusionMask=0;
        unsigned sectionDirections=0;
        int rhythmicFeel=0;          // -1 straight, 0 auto, 1 syncopated, 2 swung
        int brightnessDirection=0;   // -1 dark/muted, +1 bright/open
        int spaceDirection=0;        // -1 dry/intimate, +1 spacious/wide
        int aggressionDirection=0;   // -1 soft/controlled, +1 aggressive/heavy
        int dropCharacter=0;         // 0 auto, 1 melodic, 2 driving, 3 euphoric, 4 heavy
        int harmonicMotionDirection=0; // -1 simple/static, +1 moving/colourful
        int finalEvolutionDirection=0; // -1 faithful repeat, +1 evolved final statement
    };

    struct SongPlan {
        int structureStyle=0;
        int structureVariant=0;
        int targetBars=0;
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
        // v3.0 semantic producer decisions. These are chosen once before MIDI
        // generation and shared by composition, arrangement and SoundDNA.
        int genreFamily=0;
        int emotionProfile=0;
        int hookShape=0;
        int chordTexture=0;
        int padPolicy=0;
        int counterPolicy=1;
        float hookStrength=.72f;
        float dropIntensity=.90f;
        float drumDrive=.90f;
        float energyContrast=1.f;
        float energyBias=0.f;
        float transitionIntensity=1.f;
        float density=.65f;
        float syncopation=.35f;
        float restAmount=.18f;
        float development=.55f;
        int rhythmicFeel=0;          // 0 straight, 1 syncopated, 2 swung
        int dropCharacter=1;         // melodic/driving/euphoric/heavy
        float brightness=.5f;
        float space=.5f;
        float aggression=.5f;
        float harmonicMotion=.5f;
        float finalEvolution=.65f;
        float callResponse=.5f;
    };

    struct SeedDomains {
        uint64_t structure=0,harmony=0,voicing=0,drums=0,bass=0,sub=0,pluck=0,pad=0;
        uint64_t melody=0,counter=0,fx=0,soundPalette=0;
    };

    struct SoundPalettePlan {
        int character=0; // analog, digital, organic, hybrid
        float brightness=.5f;
        float movement=.5f;
        float space=.5f;
        float impact=.5f;
    };

    struct HarmonyPlan {
        int progressionLength=4;
        std::array<int,8> mainDegrees{0,5,2,6,0,3,4,6};
        std::array<int,8> alternateDegrees{0,3,5,4,0,6,5,4};
        std::array<double,8> rhythmBars{1,1,1,1,1,1,1,1};
        std::array<int,8> inversions{0,1,0,2,1,0,2,0};
        std::array<int,8> voicingStyles{0,1,2,0,3,1,4,2};
        int cadenceStyle=0;
        int rhythmMode=0;
        int registerBase=60;
        float tension=.35f;
        float borrowedProbability=.06f;
        float passingProbability=.10f;
        float suspensionProbability=.12f;
        float extensionProbability=.18f;
        bool pedalIntro=false;
        bool pedalVerse=false;
    };

    struct HarmonyEvent {
        double beat=0.0;
        double length=4.0;
        int scaleDegree=0;
        int inversion=0;
        int voicingStyle=0;
        int extension=0;
        int sectionIndex=0;
        bool borrowed=false;
    };

    static uint64_t mix64(uint64_t x) noexcept;
    static float random01(uint64_t seed, uint64_t salt) noexcept;
    static int parseRootMidi(const juce::String& prompt, bool& minorOut);
    static PromptIntent parsePromptIntent(const juce::String& prompt, double fallbackBpm);
    void buildSeedDomains(uint64_t master);
    void buildSongPlan(uint64_t seed);
    void buildSections(uint64_t seed);
    void buildSectionGoals(uint64_t seed);
    const SectionGoal& sectionGoalFor(const ArrangementSection* section) const noexcept;
    void buildHarmonyPlan(uint64_t seed);
    void buildHarmonyTimeline(uint64_t seed);
    bool sectionFlowsIntoImpact(const ArrangementSection* section) const noexcept;
    const HarmonyEvent* harmonyAtBeat(double beat) const noexcept;
    int scaleSemitoneForDegree(int degree) const noexcept;
    std::array<int,4> chordTonesFor(const HarmonyEvent& event) const noexcept;
    uint64_t computeHarmonyId() const noexcept;
    uint64_t computeMelodyId() const noexcept;
    void addDrums(uint64_t seed, bool energetic);
    void addHarmony(uint64_t seed);
    void addMelody(uint64_t seed, bool energetic);
    void alignPitchedLanesToLead();
    void addFx(uint64_t seed);

    juce::String sourcePrompt;
    PromptIntent promptIntent;
    double tempo = 128.0;
    int bars = defaultBars;
    int rootMidi = 53; // F3
    bool minor = true;
    std::vector<ArrangementLane> lanes;
    std::vector<ArrangementSection> sections;
    std::vector<SectionGoal> sectionGoals;
    SongPlan plan;
    SoundPalettePlan palettePlan;
    SeedDomains domains;
    HarmonyPlan harmonyPlan;
    std::vector<HarmonyEvent> harmonyEvents;
    uint64_t masterSeed=0;
    uint64_t harmonyId=0;
    uint64_t melodyId=0;
};

} // namespace sonara
