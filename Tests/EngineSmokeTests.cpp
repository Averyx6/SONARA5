#include <JuceHeader.h>
#include "../Source/Engine/SonaraEngine.h"
#include <cmath>
#include <iostream>

namespace {
bool renderCase(double sampleRate, sonara::FilterMode mode, sonara::LfoShape lfoShape, bool lowCpu=false)
{
    constexpr int blockSize = 256;
    sonara::SonaraEngine engine;
    engine.setLowCpuMode(lowCpu);
    engine.prepare(sampleRate, blockSize, 2);

    sonara::SoundDNA patch;
    patch.seed = 0xFEDCBA9876543210ULL;
    patch.oscA = sonara::WaveShape::saw;
    patch.oscB = sonara::WaveShape::square;
    patch.oscMix = 0.55f;
    patch.oscAMorph = 0.65f;
    patch.oscBMorph = 0.4f;
    patch.unison = 9;
    patch.detune = 0.22f;
    patch.unisonBlend = 0.9f;
    patch.width = 1.0f;
    patch.subLevel = 0.45f;
    patch.noiseLevel = 0.18f;
    patch.filterMode = mode;
    patch.cutoff = 4200.0f;
    patch.resonance = 0.72f;
    patch.filterEnv = 0.5f;
    patch.lfoShape = lfoShape;
    patch.lfoRate = 6.5f;
    patch.lfoCutoff = 0.8f;
    patch.lfoPitch = 0.3f;
    patch.lfoMorphA = 0.8f;
    patch.lfoMorphB = -0.7f;
    patch.drive = 0.8f;
    patch.chorus = 0.7f;
    patch.chorusRate = 1.7f;
    patch.chorusDepth = 0.8f;
    patch.reverb = 0.65f;
    patch.delay = 0.6f;
    engine.setPatch(patch);

    double energy = 0.0;
    float peak = 0.0f;
    for (int block = 0; block < 32; ++block)
    {
        juce::AudioBuffer<float> audio(2, blockSize);
        audio.clear();
        juce::MidiBuffer midi;
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 0);
        // Exercise host/keyboard performance modulation while a dense voice is active.
        if (block == 8)
            midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);
        if (block == 12)
            midi.addEvent(juce::MidiMessage::pitchWheel(1, 0), 0);
        if (block == 16)
            midi.addEvent(juce::MidiMessage::pitchWheel(1, 8192), 0);
        if (block == 24)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);

        engine.render(audio, midi);
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                const float sample = audio.getSample(channel, i);
                if (!std::isfinite(sample))
                    return false;
                peak = juce::jmax(peak, std::abs(sample));
                energy += (double)sample * sample;
            }
    }

    return energy > 1.0e-7 && peak > 1.0e-5f && peak <= 1.0001f;
}
}

int main()
{
    const double sampleRates[] { 44100.0, 48000.0, 96000.0 };
    const sonara::FilterMode modes[] { sonara::FilterMode::lowpass, sonara::FilterMode::highpass, sonara::FilterMode::bandpass };
    const sonara::LfoShape shapes[] { sonara::LfoShape::sine, sonara::LfoShape::triangle, sonara::LfoShape::sawUp, sonara::LfoShape::sawDown, sonara::LfoShape::square };

    for (const auto sampleRate : sampleRates)
        for (const auto mode : modes)
            for (const auto shape : shapes)
                if (!renderCase(sampleRate, mode, shape))
                {
                    std::cerr << "Engine smoke test failed at " << sampleRate
                              << " Hz, filter " << static_cast<int>(mode)
                              << ", LFO " << static_cast<int>(shape) << '\n';
                    return 1;
                }

    for(const auto sampleRate:sampleRates)
        if(!renderCase(sampleRate,sonara::FilterMode::lowpass,sonara::LfoShape::triangle,true))
        {
            std::cerr<<"Low-CPU song engine smoke test failed at "<<sampleRate<<" Hz\n";
            return 2;
        }

    std::cout << "SONARA engine smoke tests passed\n";
    return 0;
}