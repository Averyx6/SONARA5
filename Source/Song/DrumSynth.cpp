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

    kickBaseHz=43.f+11.f*normCutoff(kick);
    const float kickImpact=juce::jlimit(0.f,1.f,.44f*kick.macroImpact+.34f*kick.transientLevel+.12f*kick.drive+.010f*juce::jmax(0.f,kick.pitchEnv));
    kickSweepHz=82.f+108.f*kickImpact;
    kickClick=juce::jlimit(.035f,.28f,.045f+.10f*normCutoff(kick)+.16f*kick.transientLevel);
    kickDecay=decayFor(.16f+.15f*normRelease(kick));
    kickGain=juce::jlimit(.72f,.98f,.74f+.14f*kickImpact+.08f*kick.drive);

    snareNoise=juce::jlimit(.55f,.90f,.58f+.20f*normCutoff(snare)+.12f*snare.noiseLevel);
    snareTone=juce::jlimit(.14f,.34f,.16f+.10f*snare.filterEnv+.08f*snare.macroImpact);
    snareDecay=decayFor(.105f+.12f*normRelease(snare));
    snareGain=juce::jlimit(.42f,.72f,.46f+.14f*snare.macroImpact+.09f*snare.drive+.08f*snare.transientLevel);

    hatDifference=juce::jlimit(.78f,.97f,.85f+.10f*normCutoff(hats));
    hatDecay=decayFor(.032f+.078f*normRelease(hats));
    hatGain=juce::jlimit(.065f,.18f,.078f+.075f*normCutoff(hats)+.025f*hats.macroImpact);

    clapTone=juce::jlimit(.15f,.52f,.20f+.22f*perc.macroBrightness+.10f*perc.macroMovement);
    clapDecay=decayFor(.070f+.085f*normRelease(perc));
    clapGain=juce::jlimit(.20f,.46f,.23f+.13f*perc.macroImpact+.05f*perc.drive);
    percDecay=decayFor(.024f+.045f*normRelease(perc));
    percGain=juce::jlimit(.060f,.16f,.072f+.050f*perc.macroImpact+.020f*perc.drive);
    percBaseToneHz=620.f+520.f*juce::jlimit(0.f,1.f,perc.macroBrightness);
    percToneHz=percBaseToneHz;

    crashNoise=juce::jlimit(.40f,.68f,.46f+.16f*normCutoff(hats)+.06f*hats.noiseLevel);
    crashTone=juce::jlimit(.06f,.18f,.08f+.08f*hats.macroBrightness);
    crashDecay=decayFor(.58f+.86f*normRelease(hats));
    crashGain=juce::jlimit(.11f,.28f,.13f+.10f*hats.macroImpact+.04f*hats.reverb);

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
    else if(note==38){snareEnv=1.f;snareVelocity=v;snarePhase=0.0;}
    else if(note==42||note==46){hatEnv=note==46?1.35f:1.f;hatVelocity=v;}
    else if(note==39){clapEnv=1.f;clapVelocity=v;}
    else if(note==37){percEnv=1.f;percVelocity=v;percToneHz=percBaseToneHz;percPhase=0.0;}
    else if(note==45||note==47||note==50)
    {
        percEnv=1.f;percVelocity=v;percPhase=0.0;
        const float semis=(float)(note-45);
        percToneHz=145.f*std::pow(2.f,semis/12.f);
    }
    else if(note==49){crashEnv=1.f;crashVelocity=v;}
    else if(note==57)
    {
        crashEnv=1.35f;crashVelocity=v;
        percEnv=.90f;percVelocity=juce::jmin(1.f,v*.90f);percToneHz=92.f;percPhase=0.0;
    }
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
            const float punch=(float)std::sin(kickPhase*2.0)*.13f*pitchEnv;
            const float click=noise()*kickClick*pitchEnv;
            out+=(body*.98f+punch+click)*kickEnv*kickVelocity*kickGain;
            kickEnv*=kickDecay;
        }

        if(snareEnv>0.00005f)
        {
            const float x=noise();
            const float hp=x-.58f*snareHpIn;
            snareHpIn=x;
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
            percPhase+=juce::MathConstants<double>::twoPi*percToneHz/sr;
            if(percPhase>juce::MathConstants<double>::twoPi)percPhase-=juce::MathConstants<double>::twoPi;
            const float tone=(float)std::sin(percPhase);
            const float click=noise()*.18f;
            out+=(tone*.82f+click)*percEnv*percVelocity*percGain;
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

        out=juce::jlimit(-.94f,.94f,std::tanh(out*1.18f));
        for(int channel=0;channel<ch;++channel)
            b.addSample(channel,i,out);
    }
}

} // namespace sonara
