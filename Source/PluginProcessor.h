#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <deque>
#include "Engine/SonaraEngine.h"
#include "Generation/PromptGenerator.h"
#include "Generation/SongArrangement.h"
#include "Integration/CyanoryxBridge.h"
#include "Song/DrumSynth.h"
#include "Reference/ReferenceAnalyzer.h"
#include "Export/AudioExporter.h"

class SonaraAudioProcessor final : public juce::AudioProcessor {
public:
    enum class Macro : int { brightness=0, movement, space, impact };
    enum class LaneMixParameter : int { level=0, pan, width, fxSend };
    struct LaneMixState { float level=1.f,pan=0.f,width=1.f,fxSend=1.f; };
    SonaraAudioProcessor();
    ~SonaraAudioProcessor() override;
    void prepareToPlay(double,int) override; void releaseResources() override{}; bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override; bool hasEditor() const override{return true;}
    const juce::String getName() const override{return "SONARA";} bool acceptsMidi() const override{return true;} bool producesMidi() const override{return false;} bool isMidiEffect() const override{return false;}
    double getTailLengthSeconds() const override{return 4.0;} int getNumPrograms() override{return 1;} int getCurrentProgram() override{return 0;} void setCurrentProgram(int) override{}; const juce::String getProgramName(int) override{return {};}; void changeProgramName(int,const juce::String&) override{}
    void getStateInformation(juce::MemoryBlock&) override; void setStateInformation(const void*,int) override;

    void generatePatch(const juce::String&); void mutatePatch(); void generateSimilarPatch(); void generateVariation(int index); void randomizePatch();
    void undoPatch(); void redoPatch(); void captureA(); void captureB(); void recallA(); void recallB();
    void startPreview(); void stopPreview(); bool isPreviewPlaying() const noexcept { return previewPlaying.load(); }
    double previewPosition01() const noexcept { return previewLengthSamples > 0 ? juce::jlimit(0.0,1.0,(double)previewSample.load()/(double)previewLengthSamples) : 0.0; }
    bool writePreviewMidiFile(const juce::File&) const;

    void generateTrack(const juce::String& prompt);
    void randomizeEverything(const juce::String& prompt);
    juce::String makeSurprisePrompt();
    void regenerateDrums(const juce::String& prompt);
    void startSongPreview();
    void startSongPreviewAtBar(int bar);
    void startDropPreview();
    void pauseSongPreview();
    void resumeSongPreview();
    void stopSongPreview();
    bool isSongPlaying() const noexcept { return songPlaying.load(); }
    double songPosition01() const noexcept;
    int currentSongBar() const noexcept;
    juce::String currentSectionName() const;
    std::shared_ptr<const sonara::SongArrangement> arrangementSnapshot() const noexcept { return std::atomic_load_explicit(&arrangement,std::memory_order_acquire); }
    bool writeArrangementMidiFile(const juce::File&) const;
    bool writeSelectedLaneMidiFile(const juce::File&) const;
    bool writeLeadMidiFile(const juce::File&) const;
    void setSelectedLane(int i);
    int getSelectedLane() const noexcept { return selectedLane.load(); }


    // Reference / RESOUND / MIDI import
    bool analyseReferenceFile(const juce::File&);
    bool importMidiFile(const juce::File&);
    bool hasReference() const noexcept { return referenceLoaded; }
    juce::String getReferenceSummary() const { return reference.summary(); }
    void resoundReference(const juce::String& prompt);
    void rebuildInstrumentalFromReference(const juce::String& prompt);
    bool writeReferenceMidiFile(const juce::File&) const;

    // Real project/preset persistence and audio export.
    bool saveSound(const juce::File&) const;
    bool loadSound(const juce::File&);
    bool saveProject(const juce::File&) const;
    bool loadProject(const juce::File&);
    bool exportFullMix(const juce::File&);
    bool exportSelectedLaneAudio(const juce::File&);
    bool exportLeadAudio(const juce::File&);
    bool exportAllStems(const juce::File& directory);

    void setPreviewBpm(double bpm) noexcept { previewBpm=juce::jlimit(60.0,200.0,bpm); }
    double getPreviewBpm() const noexcept { return previewBpm; }
    uint64_t getSongGenerationSeed() const noexcept { return lastSongSeed.load(std::memory_order_relaxed); }
    float getMelodyNovelty() const noexcept { return lastMelodyNovelty.load(std::memory_order_relaxed); }
    float getHarmonyNovelty() const noexcept { return lastHarmonyNovelty.load(std::memory_order_relaxed); }
    float getSongNovelty() const noexcept { return lastSongNovelty.load(std::memory_order_relaxed); }
    void setMacro(Macro,float normalized);
    void setLaneMix(int laneIndex,LaneMixParameter,float value) noexcept;
    LaneMixState getLaneMix(int laneIndex) const noexcept;
    bool setSelectedLaneSound(const juce::String& prompt,bool automatic=false);
    const sonara::SoundDNA& currentPatch()const{return engine.patch();}
    sonara::MutationLocks& mutationLocks() noexcept { return locks; }
    const sonara::MutationLocks& mutationLocks() const noexcept { return locks; }

    void connectCyanoryx() noexcept { cyanoryx.connect(); }
    void disconnectCyanoryx() noexcept { cyanoryx.disconnect(); }
    bool isCyanoryxConnected() const noexcept { return cyanoryx.isConnected(); }
    juce::String exportPatchForCyanoryx() const { return cyanoryx.serializePatch(engine.patch()); }
    juce::String exportProjectForCyanoryx() const;
    bool importPatchFromCyanoryx(const juce::String& payload);
    juce::String makeCyanoryxRequest(const sonara::CyanoryxSoundRequest& request) const { return cyanoryx.serializeRequest(request); }

    std::atomic<float> generationProgress{0}; juce::String generationStatus{"Ready"};

private:
    static constexpr int firstMusicalLane=4, musicalLaneCount=8;
    void injectPreviewMidi(juce::MidiBuffer&,int);
    void injectSongLaneMidi(const sonara::ArrangementLane&, juce::MidiBuffer&, int64_t startSample, int numSamples, double bpm) noexcept;
    int collectDrumTriggers(const sonara::SongArrangement&, int64_t startSample, int numSamples) noexcept;
    void renderSongBlock(juce::AudioBuffer<float>&, int numSamples);
    void setPatchWithHistory(const sonara::SoundDNA&);

    sonara::SonaraEngine engine;
    std::array<sonara::SonaraEngine,musicalLaneCount> songEngines;
    juce::Reverb songReverb;
    sonara::PromptGenerator generator; sonara::CyanoryxBridge cyanoryx; sonara::MutationLocks locks; sonara::DrumSynth drumSynth;
    sonara::ReferenceAnalyzer referenceAnalyzer; sonara::ReferenceAnalysis reference; bool referenceLoaded=false, referenceMelodyPreview=false;
    sonara::AudioExporter audioExporter;
    uint64_t generationCounter=1;
    uint64_t sessionSalt=0;
    std::atomic<uint64_t> lastSongSeed{0};
    std::atomic<float> lastMelodyNovelty{1.f};
    std::atomic<float> lastHarmonyNovelty{1.f};
    std::atomic<float> lastSongNovelty{1.f};
    std::deque<std::vector<int>> melodyHistory;
    std::deque<std::vector<int>> harmonyHistory,bassHistory,drumHistory,pluckHistory,structureHistory;
    std::atomic<bool> previewPlaying{false}; std::atomic<int64_t> previewSample{0}; std::atomic<bool> songPlaying{false}; std::atomic<int64_t> songSample{0}; std::atomic<int> songFadeRemaining{0}; std::atomic<int> selectedLane{9};
    double previewSampleRate=44100.0,previewBpm=128.0; int64_t previewLengthSamples=1; int maximumBlockSize=512;
    std::shared_ptr<const sonara::SongArrangement> arrangement;
    std::array<std::atomic<float>,12> laneMixLevel{},laneMixPan{},laneMixWidth{},laneMixFx{};
    std::array<juce::AudioBuffer<float>,musicalLaneCount> songScratch;
    juce::AudioBuffer<float> songFxBus,songDrumBus;
    std::array<juce::MidiBuffer,musicalLaneCount> songMidi;
    std::array<std::array<float,2>,musicalLaneCount> laneHpX{},laneHpY{};
    std::array<std::array<float,2>,musicalLaneCount> laneLpState{};
    std::array<float,2> masterHpX{},masterHpY{};
    std::vector<float> songDuckEnvelope;
    float songDuckState=0.f;
    std::array<sonara::DrumTrigger,256> drumTriggers{};
    std::vector<sonara::SoundDNA> patchHistory; int historyIndex=-1; sonara::SoundDNA patchA,patchB; bool hasA=false,hasB=false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SonaraAudioProcessor)
};
