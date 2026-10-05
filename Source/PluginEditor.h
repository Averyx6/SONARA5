#pragma once
#include <JuceHeader.h>
#include <array>
#include <memory>
#include <thread>
#include <map>
#include "PluginProcessor.h"

class SonaraAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         public juce::DragAndDropContainer,
                                         private juce::Timer {
public:
    explicit SonaraAudioProcessorEditor(SonaraAudioProcessor&);
    ~SonaraAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class SonaraLookAndFeel final : public juce::LookAndFeel_V4 {
    public:
        SonaraLookAndFeel();
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
        void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    } look;

    class ExternalDragButton final : public juce::TextButton {
    public:
        enum class Kind { previewMidi, fullMidi, laneMidi, leadMidi, referenceMidi, referenceAudio, fullMixAudio, laneAudio, leadAudio, stemsAudio };
        ExternalDragButton(SonaraAudioProcessorEditor& o, const juce::String& text, Kind k)
            : juce::TextButton(text), owner(o), kind(k) {}
        void mouseDown(const juce::MouseEvent& e) override {gestureStarted=false;juce::TextButton::mouseDown(e);}
        void mouseDrag(const juce::MouseEvent& e) override;
    private:
        SonaraAudioProcessorEditor& owner;
        Kind kind;bool gestureStarted=false;
    };

    class SoundDNAView final : public juce::Component {
    public:
        explicit SoundDNAView(SonaraAudioProcessor& p) : processor(p) { setInterceptsMouseClicks(false,false); }
        void paint(juce::Graphics&) override;
        float animation = 0.f;
    private:
        SonaraAudioProcessor& processor;
    };

    class TimelineView final : public juce::Component {
    public:
        explicit TimelineView(SonaraAudioProcessor& p) : processor(p) {}
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
    private:
        SonaraAudioProcessor& processor;
    };

    class PianoRollView final : public juce::Component {
    public:
        explicit PianoRollView(SonaraAudioProcessor& p) : processor(p) { setInterceptsMouseClicks(false,false); }
        void paint(juce::Graphics&) override;
    private:
        SonaraAudioProcessor& processor;
    };

    void timerCallback() override;
    void setTab(int);
    void styleButton(juce::Button&, bool accent=false);
    void configureMacro(juce::Slider&, const juce::String&);
    void configureMixSlider(juce::Slider&,double min,double max,const juce::String& suffix);
    void syncMixControls();
    void syncLockButtons();
    void beginExternalDrag(ExternalDragButton::Kind);
    juce::StringArray prepareDragFiles(ExternalDragButton::Kind);
    juce::String dragCacheKey(ExternalDragButton::Kind) const;
    void launchFileDrag(const juce::StringArray&);
    void runWork(std::function<void()>,std::function<void()> finished={});
    void chooseExportMidi(bool selected);
    void updateModeVisibility();
    void chooseReferenceAudio();
    void chooseMidiImport();
    void chooseLoadSound();
    void chooseSaveSound();
    void chooseLoadProject();
    void chooseSaveProject();
    void chooseExportMix();
    void chooseExportStems();
    void showStatus(const juce::String&);

    SonaraAudioProcessor& p;
    juce::TextEditor soundPrompt, songPrompt, laneSoundPrompt;
    juce::TextButton generateSound{"GENERATE SOUND"}, generateTrack{"GENERATE TRACK"}, generateDrums{"GENERATE DRUMS"}, randomizeEverythingButton{"RANDOMIZE EVERYTHING"}, surpriseMe{"SURPRISE ME"}, similar{"SIMILAR"}, mutate{"MUTATE"}, randomize{"RANDOMIZE"};
    juce::TextButton applyLaneSound{"APPLY SELECTED SOUND"}, autoLaneSound{"AUTO FIT SELECTED"};
    juce::TextButton undo{"UNDO"}, redo{"REDO"}, variation1{"V1"}, variation2{"V2"}, variation3{"V3"}, variation4{"V4"};
    juce::TextButton captureA{"CAPTURE A"}, captureB{"CAPTURE B"}, recallA{"A"}, recallB{"B"};
    juce::TextButton connect{"COPY CYANORYX BUNDLE"}, previewSound{"PREVIEW SOUND"}, playSong{"PLAY SONG"}, playChorus{"PLAY CHORUS"}, playDrop{"PLAY DROP"}, stop{"STOP"};

    juce::TextButton loadReference{"LOAD AUDIO"}, importMidi{"IMPORT MIDI"}, resound{"RESOUND"}, rebuildReference{"REBUILD TRACK"};
    juce::TextButton saveSoundButton{"SAVE SOUND"}, loadSoundButton{"LOAD SOUND"}, saveProjectButton{"SAVE PROJECT"}, loadProjectButton{"LOAD PROJECT"};
    juce::TextButton exportFullMidiButton{"EXPORT FULL MIDI"},exportLaneMidiButton{"EXPORT SELECTED MIDI"};
    juce::TextButton exportMixButton{"EXPORT MIX"}, exportStemsButton{"EXPORT STEMS"};

    ExternalDragButton dragPreviewMidi{*this,"SOUND MIDI",ExternalDragButton::Kind::previewMidi};
    ExternalDragButton dragFullMidi{*this,"FULL SONG MIDI",ExternalDragButton::Kind::fullMidi};
    ExternalDragButton dragLaneMidi{*this,"SELECTED MIDI",ExternalDragButton::Kind::laneMidi};
    ExternalDragButton dragLeadMidi{*this,"LEAD MIDI",ExternalDragButton::Kind::leadMidi};
    ExternalDragButton dragReferenceMidi{*this,"REFERENCE MIDI",ExternalDragButton::Kind::referenceMidi};
    ExternalDragButton dragReferenceAudio{*this,"RESOUND WAV",ExternalDragButton::Kind::referenceAudio};
    ExternalDragButton dragFullAudio{*this,"FULL MIX WAV",ExternalDragButton::Kind::fullMixAudio};
    ExternalDragButton dragLaneAudio{*this,"SELECTED WAV",ExternalDragButton::Kind::laneAudio};
    ExternalDragButton dragLeadAudio{*this,"LEAD WAV",ExternalDragButton::Kind::leadAudio};
    ExternalDragButton dragStems{*this,"STEMS",ExternalDragButton::Kind::stemsAudio};

    juce::TextButton tabInstrument{"INSTRUMENT"},tabSong{"SONG"},tabDrums{"DRUMS"},tabFx{"FX & MIX"},tabReference{"REFERENCE"},tabMidi{"MIDI"},tabExport{"EXPORT"};
    juce::ToggleButton lockOsc{"OSC"},lockUnison{"UNISON"},lockEnv{"ENV"},lockFilter{"FILTER"},lockMod{"MOD"},lockSources{"SUB/NOISE"},lockTone{"TONE"},lockFx{"FX"};
    juce::Slider macroBrightness,macroMovement,macroSpace,macroImpact,bpm;
    juce::Slider mixLevel,mixPan,mixWidth,mixFx;
    juce::Label patchName, statusLine, selectedLaneLabel, referenceSummary;

    std::array<juce::TextButton,8> presets {{
        juce::TextButton("Progressive Lead"), juce::TextButton("Future Rave"), juce::TextButton("Warm Pluck"), juce::TextButton("Deep Reese"),
        juce::TextButton("Dream Pad"), juce::TextButton("Tech House Bass"), juce::TextButton("Cinematic Bell"), juce::TextButton("Experimental")
    }};

    std::array<juce::TextButton,10> promptSuggestions {{
        juce::TextButton("STRONG HOOK"), juce::TextButton("SIMPLE MELODY"), juce::TextButton("SHORT INTRO"),
        juce::TextButton("BIG CHORUS"), juce::TextButton("LONG DROP"), juce::TextButton("DRUM BUILD"),
        juce::TextButton("RADIO STRUCTURE"), juce::TextButton("POWERFUL DROP"), juce::TextButton("MORE SPACE"),
        juce::TextButton("NO COUNTER")
    }};

    SoundDNAView soundView{p};
    TimelineView timeline{p};
    PianoRollView pianoRoll{p};
    double playbackProgress = 0.0;
    juce::ProgressBar playbackBar{playbackProgress};
    juce::File dragFile;
    std::unique_ptr<juce::FileChooser> fileChooser;
    float pulse = 0.f;
    int activeTab = 1;
    bool busy=false,dragActive=false;
    std::thread worker;
    struct PreparedDrag {juce::String key;juce::StringArray files;};
    std::map<int,PreparedDrag> dragCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SonaraAudioProcessorEditor)
};