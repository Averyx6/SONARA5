#include "DrumSynth.h"
namespace sonara {
float DrumSynth::noise() noexcept { noiseState ^= noiseState << 13; noiseState ^= noiseState >> 7; noiseState ^= noiseState << 17; return (float)((noiseState >> 40) & 0xffffffULL) * (2.f / 16777215.f) - 1.f; }
void DrumSynth::trigger(int note,float v) noexcept {
    v=juce::jlimit(0.f,1.f,v);
    if(note==36){kickEnv=1.f;kickVelocity=v;kickPhase=0.0;}
    else if(note==38){snareEnv=1.f;snareVelocity=v;}
    else if(note==42||note==46){hatEnv=note==46?1.35f:1.f;hatVelocity=v;}
    else if(note==39){clapEnv=1.f;clapVelocity=v;}
    else if(note==49||note==57){crashEnv=1.f;crashVelocity=v;}
}
void DrumSynth::render(juce::AudioBuffer<float>& b, const DrumTrigger* triggers, int triggerCount) noexcept {
    const int n=b.getNumSamples(); const int ch=b.getNumChannels(); int triggerIndex=0;
    for(int i=0;i<n;++i){
        while(triggerIndex<triggerCount && triggers[triggerIndex].sampleOffset<=i){trigger(triggers[triggerIndex].midiNote,triggers[triggerIndex].velocity);++triggerIndex;}
        float out=0.f;
        if(kickEnv>0.00005f){const float hz=46.f+115.f*kickEnv*kickEnv; kickPhase+=juce::MathConstants<double>::twoPi*hz/sr; if(kickPhase>=juce::MathConstants<double>::twoPi)kickPhase-=juce::MathConstants<double>::twoPi; const float body=(float)std::sin(kickPhase); const float click=noise()*kickEnv*kickEnv*.15f; out+=(body*.92f+click)*kickEnv*kickVelocity*.78f; kickEnv*=0.99935f;}
        if(snareEnv>0.00005f){const float tonal=std::sin((1.f-snareEnv)*40.f)*.18f; out+=(noise()*.70f+tonal)*snareEnv*snareVelocity*.48f; snareEnv*=0.9963f;}
        if(hatEnv>0.00005f){const float hp=noise()-(noise()*.55f); out+=hp*hatEnv*hatVelocity*.16f; hatEnv*=0.988f;}
        if(clapEnv>0.00005f){const float burst=noise()*(.6f+.4f*std::sin((1.f-clapEnv)*95.f)); out+=burst*clapEnv*clapVelocity*.34f; clapEnv*=0.9935f;}
        if(crashEnv>0.00005f){const float metal=noise()*.65f+(float)std::sin((1.f-crashEnv)*220.f)*.18f; out+=metal*crashEnv*crashVelocity*.22f; crashEnv*=0.9991f;}
        out=std::tanh(out*1.35f);
        for(int c=0;c<ch;++c)b.addSample(c,i,out*(c==0?.97f:1.03f));
    }
}
}