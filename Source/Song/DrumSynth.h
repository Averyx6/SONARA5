#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <cstdint>

namespace sonara {
struct DrumTrigger { int sampleOffset=0; int midiNote=36; float velocity=1.f; };
class DrumSynth {
public:
    void prepare(double sampleRate) noexcept { sr = juce::jmax(8000.0, sampleRate); reset(); }
    void reset() noexcept { kickEnv=snareEnv=hatEnv=clapEnv=crashEnv=0.f; kickPhase=0.0; noiseState=0x12345678abcdefULL; }
    void trigger(int midiNote, float velocity) noexcept;
    void render(juce::AudioBuffer<float>& buffer, const DrumTrigger* triggers=nullptr, int triggerCount=0) noexcept;
private:
    float noise() noexcept;
    double sr=44100.0, kickPhase=0.0;
    float kickEnv=0.f, snareEnv=0.f, hatEnv=0.f, clapEnv=0.f, crashEnv=0.f;
    float kickVelocity=1.f, snareVelocity=1.f, hatVelocity=1.f, clapVelocity=1.f, crashVelocity=1.f;
    uint64_t noiseState=1;
};
}