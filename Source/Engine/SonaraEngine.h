#pragma once
#include <JuceHeader.h>
#include "SoundDNA.h"
#include <array>
#include <cmath>
namespace sonara {
class SonaraSound final : public juce::SynthesiserSound { public: bool appliesToNote(int) override{return true;} bool appliesToChannel(int) override{return true;} };
class SonaraVoice final : public juce::SynthesiserVoice {
public:
    bool canPlaySound(juce::SynthesiserSound* s) override{return dynamic_cast<SonaraSound*>(s)!=nullptr;}
    void prepare(double sampleRate, int maximumBlockSize, int numChannels) noexcept;
    void setDNA(const SoundDNA& d) noexcept { dna.copyDSPFrom(d); }
    void startNote(int,float,juce::SynthesiserSound*,int) override; void stopNote(float,bool) override;
    void pitchWheelMoved(int value) override
    {
        const int bounded = juce::jlimit(0, 16383, value);
        const float semitones = 2.0f * (static_cast<float>(bounded) - 8192.0f) / 8192.0f;
        const float nextRatio = std::pow(2.0f, semitones / 12.0f);
        const float relative = nextRatio / juce::jmax(0.0001f, pitchBendRatio);
        for (int i = 0; i < voices; ++i) { incA[i] *= relative; incB[i] *= relative; }
        subInc *= relative;
        pitchBendRatio = nextRatio;
    }
    void controllerMoved(int,int) override {}
    void renderNextBlock(juce::AudioBuffer<float>&,int,int) override;
private:
    static constexpr int maxUnison=9; static float polyBlep(float,float) noexcept; static float wave(WaveShape,double,double) noexcept; static float morphedWave(WaveShape,float,double,double) noexcept;
    float nextNoise() noexcept; SoundDNA dna; juce::ADSR adsr; juce::ADSR::Parameters env; std::array<double,maxUnison> phaseA{},phaseB{},incA{},incB{}; std::array<float,maxUnison> panL{},panR{}; int voices=1; float level=0.f,lfoPhase=0.f; double noteHz=440.0,subPhase=0.0,subInc=0.0; uint64_t noiseState=1; juce::dsp::StateVariableTPTFilter<float> filterL,filterR;
    // Voice-local and allocation-free. The conventional +/-2 semitone range keeps
    // host/keyboard pitch-wheel performance responsive without touching patch state.
    float pitchBendRatio=1.f;
    int64_t ageSamples=0; int holdCounter=0; float heldL=0.f,heldR=0.f;
};
class SonaraEngine {
public: SonaraEngine(); void setLowCpuMode(bool enabled); void setVoiceLimit(int voices); void prepare(double,int,int); void render(juce::AudioBuffer<float>&,juce::MidiBuffer&); void setPatch(const SoundDNA&); void allNotesOff() noexcept { synth.allNotesOff(0, false); } bool hasActiveVoices() noexcept; const SoundDNA& patch()const noexcept{return dna;}
private:
    static float readFractional(const juce::AudioBuffer<float>&,int,int,float) noexcept; float readDelay(int,float) const noexcept; void applyPendingPatch() noexcept; void processChorus(juce::AudioBuffer<float>&) noexcept; void processDelay(juce::AudioBuffer<float>&) noexcept;
    juce::Synthesiser synth; SoundDNA dna,pendingDNA,audioDNA; juce::SpinLock pendingLock; bool patchPending=false; double sr=44100.0; juce::Reverb reverb; juce::Reverb::Parameters reverbParams; juce::AudioBuffer<float> chorusBuffer,delayBuffer; int chorusWrite=0,delayWrite=0; float chorusPhase=0.f; bool lowCpuMode=false; int requestedVoices=8; void rebuildVoices();
};
}