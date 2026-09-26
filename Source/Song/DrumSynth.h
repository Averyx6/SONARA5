#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <cstdint>
#include "../Engine/SoundDNA.h"

namespace sonara {

struct DrumTrigger {
    int sampleOffset=0;
    int midiNote=36;
    float velocity=1.f;
};

class DrumSynth {
public:
    void prepare(double sampleRate) noexcept { sr=juce::jmax(8000.0,sampleRate); reset(); }
    void configureKit(const SoundDNA& kick, const SoundDNA& snare, const SoundDNA& hats, const SoundDNA& perc) noexcept;
    void reset() noexcept {
        kickEnv=snareEnv=hatEnv=clapEnv=percEnv=crashEnv=0.f;
        kickPhase=snarePhase=percPhase=crashPhase=0.0;
        snareHpIn=hatHpIn=hatHpOut=crashHpIn=crashHpOut=0.f;
        noiseState=kitSeed;
    }
    void trigger(int midiNote,float velocity) noexcept;
    void render(juce::AudioBuffer<float>& buffer,const DrumTrigger* triggers=nullptr,int triggerCount=0) noexcept;

private:
    float noise() noexcept;

    double sr=44100.0,kickPhase=0.0,snarePhase=0.0,percPhase=0.0,crashPhase=0.0;
    float kickEnv=0.f,snareEnv=0.f,hatEnv=0.f,clapEnv=0.f,percEnv=0.f,crashEnv=0.f;
    float snareHpIn=0.f,hatHpIn=0.f,hatHpOut=0.f,crashHpIn=0.f,crashHpOut=0.f;
    float kickVelocity=1.f,snareVelocity=1.f,hatVelocity=1.f,clapVelocity=1.f,percVelocity=1.f,crashVelocity=1.f;

    uint64_t kitSeed=0x12345678abcdefULL,noiseState=1;

    float kickBaseHz=46.f,kickSweepHz=115.f,kickClick=.15f,kickDecay=.99935f,kickGain=.78f;
    float snareNoise=.70f,snareTone=.18f,snareDecay=.9963f,snareGain=.48f;
    float hatDifference=.55f,hatDecay=.988f,hatGain=.16f;
    float clapTone=.40f,clapDecay=.9935f,clapGain=.34f;
    float percDecay=.990f,percGain=.18f,percToneHz=760.f;
    float crashNoise=.65f,crashTone=.18f,crashDecay=.9991f,crashGain=.22f;
};

} // namespace sonara
