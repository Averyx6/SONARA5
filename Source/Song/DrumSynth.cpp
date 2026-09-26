#include "DrumSynth.h"

namespace sonara {

namespace {
float normCutoff(const SoundDNA& d) noexcept { return juce::jlimit(0.f,1.f,d.cutoff/18000.f); }
float normRelease(const SoundDNA& d) noexcept { return juce::jlimit(0.f,1.f,d.release/1.8f); }
}

void DrumSynth::configureKit(const SoundDNA& kick,const SoundDNA& snare,const SoundDNA& hats,const SoundDNA& perc) noexcept
{
    kitSeed = kick.seed ^ (snare.seed<<1) ^ (hats.seed<<7) ^ (perc.seed<<13) ^ 0x9e3779b97f4a7c15ULL;

    kickBaseHz = 38.f + 16.f*normCutoff(kick);
    kickSweepHz = 82.f + 92.f*juce::jlimit(0.f,1.f,kick.macroImpact+.18f*kick.drive);
    kickClick = juce::jlimit(.035f,.30f,.055f+.17f*normCutoff(kick)+.16f*kick.transientLevel);
    kickDecay = juce::jlimit(.99875f,.99972f,.99892f+.00070f*normRelease(kick));
    kickGain = juce::jlimit(.58f,.90f,.66f+.18f*kick.macroImpact);

    snareNoise = juce::jlimit(.45f,.88f,.50f+.30f*normCutoff(snare)+.12f*snare.noiseLevel);
    snareTone = juce::jlimit(.08f,.30f,.10f+.15f*snare.filterEnv+.06f*snare.macroImpact);
    snareDecay = juce::jlimit(.9948f,.9980f,.9950f+.0024f*normRelease(snare));
    snareGain = juce::jlimit(.32f,.62f,.38f+.16f*snare.macroImpact+.06f*snare.drive);

    hatDifference = juce::jlimit(.25f,.78f,.72f-.40f*normCutoff(hats));
    hatDecay = juce::jlimit(.982f,.9948f,.983f+.010f*normRelease(hats));
    hatGain = juce::jlimit(.08f,.23f,.09f+.12f*normCutoff(hats)+.03f*hats.macroImpact);

    clapTone = juce::jlimit(.12f,.72f,.18f+.42f*perc.macroBrightness+.12f*perc.macroMovement);
    clapDecay = juce::jlimit(.990f,.9972f,.9908f+.0055f*normRelease(perc));
    clapGain = juce::jlimit(.18f,.48f,.22f+.18f*perc.macroImpact+.05f*perc.drive);

    crashNoise = juce::jlimit(.42f,.82f,.48f+.28f*normCutoff(hats)+.08f*hats.noiseLevel);
    crashTone = juce::jlimit(.08f,.28f,.10f+.14f*hats.macroBrightness);
    crashDecay = juce::jlimit(.9978f,.99965f,.9980f+.00145f*normRelease(hats));
    crashGain = juce::jlimit(.12f,.31f,.14f+.12f*hats.macroImpact+.04f*hats.reverb);

    reset();
}

float DrumSynth::noise() noexcept
{
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 7;
    noiseState ^= noiseState << 17;
    return (float)((noiseState >> 40) & 0xffffffULL) * (2.f / 16777215.f) - 1.f;
}

void DrumSynth::trigger(int note,float v) noexcept
{
    v=juce::jlimit(0.f,1.f,v);
    if(note==36){kickEnv=1.f;kickVelocity=v;kickPhase=0.0;}
    else if(note==38){snareEnv=1.f;snareVelocity=v;}
    else if(note==42||note==46){hatEnv=note==46?1.35f:1.f;hatVelocity=v;}
    else if(note==39){clapEnv=1.f;clapVelocity=v;}
    else if(note==49||note==57||note==37){crashEnv=1.f;crashVelocity=v;}
}

void DrumSynth::render(juce::AudioBuffer<float>& b,const DrumTrigger* triggers,int triggerCount) noexcept
{
    const int n=b.getNumSamples();
    const int ch=b.getNumChannels();
    int triggerIndex=0;

    for(int i=0;i<n;++i)
    {
        while(triggerIndex<triggerCount && triggers[triggerIndex].sampleOffset<=i)
        {
            trigger(triggers[triggerIndex].midiNote,triggers[triggerIndex].velocity);
            ++triggerIndex;
        }

        float out=0.f;
        if(kickEnv>0.00005f)
        {
            const float hz=kickBaseHz+kickSweepHz*kickEnv*kickEnv;
            kickPhase+=juce::MathConstants<double>::twoPi*hz/sr;
            if(kickPhase>=juce::MathConstants<double>::twoPi)kickPhase-=juce::MathConstants<double>::twoPi;
            const float body=(float)std::sin(kickPhase);
            const float click=noise()*kickEnv*kickEnv*kickClick;
            out+=(body*.92f+click)*kickEnv*kickVelocity*kickGain;
            kickEnv*=kickDecay;
        }

        if(snareEnv>0.00005f)
        {
            const float tonal=(float)std::sin((1.f-snareEnv)*40.f)*snareTone;
            out+=(noise()*snareNoise+tonal)*snareEnv*snareVelocity*snareGain;
            snareEnv*=snareDecay;
        }

        if(hatEnv>0.00005f)
        {
            const float hp=noise()-(noise()*hatDifference);
            out+=hp*hatEnv*hatVelocity*hatGain;
            hatEnv*=hatDecay;
        }

        if(clapEnv>0.00005f)
        {
            const float burst=noise()*((1.f-clapTone)+clapTone*std::sin((1.f-clapEnv)*95.f));
            out+=burst*clapEnv*clapVelocity*clapGain;
            clapEnv*=clapDecay;
        }

        if(crashEnv>0.00005f)
        {
            const float metal=noise()*crashNoise+(float)std::sin((1.f-crashEnv)*220.f)*crashTone;
            out+=metal*crashEnv*crashVelocity*crashGain;
            crashEnv*=crashDecay;
        }

        out=std::tanh(out*1.30f);
        for(int c=0;c<ch;++c) b.addSample(c,i,out*(c==0?.97f:1.03f));
    }
}

} // namespace sonara
