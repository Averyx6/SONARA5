#pragma once
#include "HostMidiLifecycleTests.h"

namespace sonara::realtimetests {
inline juce::String run() {
    const auto state=lifecycletests::fixture();
    SonaraAudioProcessor selected,switched;incomingtests::restore(selected,state);incomingtests::restore(switched,state);
    switched.generatePatch("dark sine pad slow attack");
    switched.setMidiRoutingMode(SonaraAudioProcessor::MidiRoutingMode::arrangementChannels);
    const float modeError=lifecycletests::difference(lifecycletests::snippet(selected),lifecycletests::snippet(switched));
    if(modeError>2e-6f)return "Explicit song MIDI mode still plays the standalone patch: "+juce::String(modeError,9);

    auto drums=state.createCopy();drums.setProperty("midiRoutingMode",1,nullptr);
    for(const auto pair:{std::pair<int,int>{35,36},{40,38},{44,42}}) {
        SonaraAudioProcessor alias,canonical;incomingtests::restore(alias,drums);incomingtests::restore(canonical,drums);
        float error=0.f;double energy=0;
        for(int block=0;block<16;++block) {
            juce::AudioBuffer<float> a(2,257),b(2,257);juce::MidiBuffer x,y;
            if(block==0){x.addEvent(juce::MidiMessage::noteOn(10,pair.first,(juce::uint8)100),17);y.addEvent(juce::MidiMessage::noteOn(10,pair.second,(juce::uint8)100),17);}
            alias.processBlock(a,x);canonical.processBlock(b,y);
            for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i) {
                energy+=(double)a.getSample(ch,i)*a.getSample(ch,i);
                error=std::max(error,std::abs(a.getSample(ch,i)-b.getSample(ch,i)));
            }
        }
        if(energy<1e-8||error>2e-6f)return "GM drum alias does not render its mapped lane: "+juce::String(pair.first);
    }

    SonaraAudioProcessor flood;incomingtests::restore(flood,state);
    juce::AudioBuffer<float> audio(2,257);juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1,65,(juce::uint8)100),0);flood.processBlock(audio,midi);
    midi.ensureSize(65536);
    for(int event=0;event<4097;++event)midi.addEvent(juce::MidiMessage::noteOn(1,48+event%36,(juce::uint8)100),event%257);
    const auto begin=std::chrono::steady_clock::now();flood.processBlock(audio,midi);
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    if(elapsed>.20||audio.getMagnitude(0,257)>1e-8f)return "Oversized host MIDI was not safely bounded";
    flood.processBlock(audio,midi);
    if(audio.getMagnitude(0,257)>1e-8f)return "Rejected host MIDI left stuck notes";
    midi.addEvent(juce::MidiMessage::noteOn(16,65,(juce::uint8)100),0);flood.processBlock(audio,midi);
    if(audio.getMagnitude(0,257)<1e-5f)return "Host MIDI did not recover after overload";

    SonaraAudioProcessor plain,sysex;incomingtests::restore(plain,state);incomingtests::restore(sysex,state);
    std::array<char,4096> payload{};
    float sysexError=0.f;
    for(int block=0;block<16;++block) {
        juce::AudioBuffer<float> a(2,257),b(2,257);juce::MidiBuffer x,y;
        if(block==0){x.addEvent(juce::MidiMessage::noteOn(10,65,(juce::uint8)100),17);y.addEvent(juce::MidiMessage::createSysExMessage(payload.data(),(int)payload.size()),0);y.addEvents(x,0,257,0);}
        plain.processBlock(a,x);sysex.processBlock(b,y);
        for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i)sysexError=std::max(sysexError,std::abs(a.getSample(ch,i)-b.getSample(ch,i)));
    }
    if(sysexError>2e-6f)return "Unsupported SysEx changed Piano Roll audio";
    std::cout<<"Explicit mode/GM aliases/SysEx parity passed; MIDI overload handled in "<<elapsed<<" s\n";
    return {};
}
}
