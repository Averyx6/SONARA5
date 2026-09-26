#include "DrumSynth.h"

namespace sonara {

namespace {
float normCutoff(const SoundDNA& d) noexcept { return juce::jlimit(0.f,1.f,d.cutoff/18000.f); }
float normRelease(const SoundDNA& d) noexcept { return juce::jlimit(0.f,1.f,d.release/1.8f); }
}

void DrumSynth::configureKit(const SoundDNA& kick,const SoundDNA& snare,const SoundDNA& hats,const SoundDNA& perc) noexcept
{
    kitSeed=kick.seed^(snare.seed<<1)^(hats.seed<<7)^(perc.seed<<13)^0x9e3779b97f4a7c15ULL;

    const auto decayFor=[this](float seconds)
    {
        return std::exp(-1.f/(float)(sr*juce::jmax(.004f,seconds)));
    };

    kickBaseHz=42.f+10.f*normCutoff(kick);
    kickSweepHz=68.f+76.f*juce::jlimit(0.f,1.f,kick.macroImpact+.12f*kick.drive);
    kickClick=juce::jlimit(.018f,.16f,.025f+.08f*normCutoff(kick)+.07f*kick.transientLevel);
    kickDecay=decayFor(.18f+.17f*normRelease(kick));
    kickGain=juce::jlimit(.56f,.82f,.62f+.13f*kick.macroImpact);

    snareNoise=juce::jlimit(.42f,.72f,.48f+.18f*normCutoff(snare)+.08f*snare.noiseLevel);
    snareTone=juce::jlimit(.10f,.24f,.12f+.08f*snare.filterEnv+.04f*snare.macroImpact);
    snareDecay=decayFor(.11f+.11f*normRelease(snare));
    snareGain=juce::jlimit(.28f,.50f,.34f+.10f*snare.macroImpact+.04f*snare.drive);

    hatDifference=juce::jlimit(.76f,.96f,.84f+.10f*normCutoff(hats));
    hatDecay=decayFor(.035f+.075f*normRelease(hats));
    hatGain=juce::jlimit(.055f,.16f,.065f+.07f*normCutoff(hats)+.02f*hats.macroImpact);

    clapTone=juce::jlimit(.15f,.50f,.20f+.22f*perc.macroBrightness+.08f*perc.macroMovement);
    clapDecay=decayFor(.075f+.075f*normRelease(perc));
    clapGain=juce::jlimit(.14f,.34f,.18f+.11f*perc.macroImpact+.03f*perc.drive);
    percDecay=decayFor(.035f+.055f*normRelease(perc));
    percGain=juce::jlimit(.08f,.22f,.10f+.08f*perc.macroImpact+.03f*perc.drive);
    percToneHz=520.f+680.f*juce::jlimit(0.f,1.f,perc.macroBrightness);

    crashNoise=juce::jlimit(.36f,.62f,.42f+.16f*normCutoff(hats)+.05f*hats.noiseLevel);
    crashTone=juce::jlimit(.05f,.16f,.07f+.08f*hats.macroBrightness);
    crashDecay=decayFor(.55f+.80f*normRelease(hats));
    crashGain=juce::jlimit(.08f,.22f,.10f+.08f*hats.macroImpact+.03f*hats.reverb);

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
    else if(note==37){percEnv=1.f;percVelocity=v;}
    else if(note==49||note==57){crashEnv=1.f;crashVelocity=v;}
}

void DrumSynth::render(juce::AudioBuffer<float>& b,const DrumTrigger* triggers,int triggerCount) noexcept
{
    const int n=b.getNumSamples();
    const int ch=b.getNumChannels();
    int triggerIndex=0;

    const float snareInc=(float)(juce::MathConstants<double>::twoPi*184.0/sr);
    const float crashInc=(float)(juce::MathConstants<double>::twoPi*5200.0/sr);
    const float hatAlpha=.86f;
    const float crashAlpha=.94f;

    for(int i=0;i<n;++i)
    {
        while(triggerIndex<triggerCount&&triggers[triggerIndex].sampleOffset<=i)
        {
            trigger(triggers[triggerIndex].midiNote,triggers[triggerIndex].velocity);
            ++triggerIndex;
        }

        float out=0.f;

        if(kickEnv>0.00005f)
        {
            const float pitchEnv=kickEnv*kickEnv;
            const float hz=kickBaseHz+kickSweepHz*pitchEnv;
            kickPhase+=juce::MathConstants<double>::twoPi*hz/sr;
            if(kickPhase>=juce::MathConstants<double>::twoPi)kickPhase-=juce::MathConstants<double>::twoPi;
            const float body=(float)std::sin(kickPhase);
            const float click=noise()*kickClick*pitchEnv;
            out+=(body*.95f+click)*kickEnv*kickVelocity*kickGain;
            kickEnv*=kickDecay;
        }

        if(snareEnv>0.00005f)
        {
            const float x=noise();
            const float hp=x-.58f*hatHpIn;
            hatHpIn=x;
            snarePhase+=snareInc;
            if(snarePhase>juce::MathConstants<double>::twoPi)snarePhase-=juce::MathConstants<double>::twoPi;
            const float tone=(float)std::sin(snarePhase)*snareTone*snareEnv;
            out+=(hp*snareNoise+tone)*snareEnv*snareVelocity*snareGain;
            snareEnv*=snareDecay;
        }

        if(hatEnv>0.00005f)
        {
            const float x=noise();
            const float hp=hatAlpha*(hatHpOut+x-hatHpIn);
            hatHpIn=x;hatHpOut=hp;
            const float metallic=hp*(.82f+.18f*(float)std::sin(crashPhase*.37));
            out+=metallic*hatEnv*hatVelocity*hatGain;
            hatEnv*=hatDecay;
        }

        if(clapEnv>0.00005f)
        {
            const float phase=(1.f-clapEnv)*38.f;
            const float burstGate=.58f+.42f*std::abs(std::sin(phase));
            const float burst=noise()*burstGate*(1.f-.38f*clapTone);
            out+=burst*clapEnv*clapVelocity*clapGain;
            clapEnv*=clapDecay;
        }

        if(percEnv>0.00005f)
        {
            const float t=1.f-percEnv;
            const float tone=(float)std::sin(t*juce::MathConstants<float>::twoPi*percToneHz*.010f);
            const float click=noise()*.22f;
            out+=(tone*.78f+click)*percEnv*percVelocity*percGain;
            percEnv*=percDecay;
        }

        if(crashEnv>0.00005f)
        {
            const float x=noise();
            const float hp=crashAlpha*(crashHpOut+x-crashHpIn);
            crashHpIn=x;crashHpOut=hp;
            crashPhase+=crashInc;
            if(crashPhase>juce::MathConstants<double>::twoPi)crashPhase-=juce::MathConstants<double>::twoPi;
            const float metal=(float)std::sin(crashPhase)+(float)std::sin(crashPhase*1.37)*.55f;
            out+=(hp*crashNoise+metal*crashTone)*crashEnv*crashVelocity*crashGain;
            crashEnv*=crashDecay;
        }

        out=std::tanh(out*1.08f);
        for(int channel=0;channel<ch;++channel)
            b.addSample(channel,i,out);
    }
}

} // namespace sonara
