#include "SoundDNA.h"
#include <cmath>
#include <limits>

namespace sonara {
namespace {
float finiteClamped(const juce::ValueTree& v, const char* key, float fallback, float lo, float hi) noexcept {
    const float value = static_cast<float>(v.getProperty(key, fallback));
    return std::isfinite(value) ? juce::jlimit(lo, hi, value) : fallback;
}

uint64_t parseSeed(const juce::var& value) noexcept {
    const auto text = value.toString().trim();
    if (text.isEmpty()) return 1;
    uint64_t result = 0;
    constexpr uint64_t maxValue = std::numeric_limits<uint64_t>::max();
    for (int i = 0; i < text.length(); ++i) {
        const auto c = text[i];
        if (c < '0' || c > '9') return 1;
        const auto digit = static_cast<uint64_t>(c - '0');
        if (result > (maxValue - digit) / 10ULL) return 1;
        result = result * 10ULL + digit;
    }
    return result;
}
}

void SoundDNA::copyDSPFrom(const SoundDNA& d) noexcept {
    seed=d.seed; oscA=d.oscA; oscB=d.oscB; oscMix=d.oscMix; oscBTranspose=d.oscBTranspose;
    oscAMorph=d.oscAMorph; oscBMorph=d.oscBMorph; unison=d.unison; detune=d.detune;
    unisonBlend=d.unisonBlend; phaseRandom=d.phaseRandom; subLevel=d.subLevel; subOctave=d.subOctave; noiseLevel=d.noiseLevel;
    pitchEnv=d.pitchEnv; pitchEnvDecay=d.pitchEnvDecay; transientLevel=d.transientLevel; transientDecay=d.transientDecay;
    fmAmount=d.fmAmount; fmRatio=d.fmRatio; ringMod=d.ringMod; bitCrush=d.bitCrush; downsample=d.downsample;
    attack=d.attack; decay=d.decay; sustain=d.sustain; release=d.release; filterMode=d.filterMode;
    cutoff=d.cutoff; resonance=d.resonance; filterEnv=d.filterEnv; lfoShape=d.lfoShape; lfoRate=d.lfoRate; lfoCutoff=d.lfoCutoff;
    lfoPitch=d.lfoPitch; lfoMorphA=d.lfoMorphA; lfoMorphB=d.lfoMorphB; drive=d.drive; width=d.width;
    chorus=d.chorus; chorusRate=d.chorusRate; chorusDepth=d.chorusDepth; reverb=d.reverb; delay=d.delay;
    macroBrightness=d.macroBrightness; macroMovement=d.macroMovement; macroSpace=d.macroSpace; macroImpact=d.macroImpact;
}

juce::ValueTree SoundDNA::toValueTree() const {
    juce::ValueTree v("SoundDNA");
    v.setProperty("schema", 10, nullptr); v.setProperty("seed", juce::String(seed), nullptr);
    v.setProperty("oscA", static_cast<int>(oscA), nullptr); v.setProperty("oscB", static_cast<int>(oscB), nullptr);
    v.setProperty("oscMix", oscMix, nullptr); v.setProperty("oscBTranspose", oscBTranspose, nullptr); v.setProperty("oscAMorph", oscAMorph, nullptr); v.setProperty("oscBMorph", oscBMorph, nullptr);
    v.setProperty("unison", unison, nullptr); v.setProperty("detune", detune, nullptr); v.setProperty("unisonBlend", unisonBlend, nullptr); v.setProperty("phaseRandom", phaseRandom, nullptr);
    v.setProperty("subLevel", subLevel, nullptr); v.setProperty("subOctave", subOctave, nullptr); v.setProperty("noiseLevel", noiseLevel, nullptr);
    v.setProperty("pitchEnv", pitchEnv, nullptr); v.setProperty("pitchEnvDecay", pitchEnvDecay, nullptr); v.setProperty("transientLevel", transientLevel, nullptr); v.setProperty("transientDecay", transientDecay, nullptr);
    v.setProperty("fmAmount", fmAmount, nullptr); v.setProperty("fmRatio", fmRatio, nullptr); v.setProperty("ringMod", ringMod, nullptr); v.setProperty("bitCrush", bitCrush, nullptr); v.setProperty("downsample", downsample, nullptr);
    v.setProperty("attack", attack, nullptr); v.setProperty("decay", decay, nullptr); v.setProperty("sustain", sustain, nullptr); v.setProperty("release", release, nullptr);
    v.setProperty("filterMode", static_cast<int>(filterMode), nullptr); v.setProperty("cutoff", cutoff, nullptr); v.setProperty("resonance", resonance, nullptr); v.setProperty("filterEnv", filterEnv, nullptr);
    v.setProperty("lfoShape", static_cast<int>(lfoShape), nullptr); v.setProperty("lfoRate", lfoRate, nullptr); v.setProperty("lfoCutoff", lfoCutoff, nullptr); v.setProperty("lfoPitch", lfoPitch, nullptr); v.setProperty("lfoMorphA", lfoMorphA, nullptr); v.setProperty("lfoMorphB", lfoMorphB, nullptr);
    v.setProperty("drive", drive, nullptr); v.setProperty("width", width, nullptr); v.setProperty("chorus", chorus, nullptr); v.setProperty("chorusRate", chorusRate, nullptr); v.setProperty("chorusDepth", chorusDepth, nullptr); v.setProperty("reverb", reverb, nullptr); v.setProperty("delay", delay, nullptr);
    v.setProperty("macroBrightness", macroBrightness, nullptr); v.setProperty("macroMovement", macroMovement, nullptr); v.setProperty("macroSpace", macroSpace, nullptr); v.setProperty("macroImpact", macroImpact, nullptr);
    v.setProperty("name", name, nullptr); v.setProperty("prompt", sourcePrompt, nullptr); return v;
}

SoundDNA SoundDNA::fromValueTree(const juce::ValueTree& v) {
    SoundDNA d; const int schema=static_cast<int>(v.getProperty("schema",0));
    d.seed=parseSeed(v.getProperty("seed","1"));
    d.oscA=static_cast<WaveShape>(juce::jlimit(0,4,(int)v.getProperty("oscA",(int)d.oscA))); d.oscB=static_cast<WaveShape>(juce::jlimit(0,4,(int)v.getProperty("oscB",(int)d.oscB)));
    d.oscMix=finiteClamped(v,"oscMix",d.oscMix,0.f,1.f); d.oscBTranspose=finiteClamped(v,"oscBTranspose",d.oscBTranspose,-36.f,36.f); d.oscAMorph=finiteClamped(v,"oscAMorph",d.oscAMorph,0.f,1.f); d.oscBMorph=finiteClamped(v,"oscBMorph",d.oscBMorph,0.f,1.f);
    d.unison=juce::jlimit(1,9,(int)v.getProperty("unison",d.unison)); d.detune=finiteClamped(v,"detune",d.detune,0.f,1.f); d.unisonBlend=finiteClamped(v,"unisonBlend",d.unisonBlend,0.f,1.f); d.phaseRandom=finiteClamped(v,"phaseRandom",d.phaseRandom,0.f,1.f);
    d.subLevel=finiteClamped(v,"subLevel",d.subLevel,0.f,1.f); d.subOctave=finiteClamped(v,"subOctave",d.subOctave,-2.f,0.f); d.noiseLevel=finiteClamped(v,"noiseLevel",d.noiseLevel,0.f,1.f);
    d.pitchEnv=finiteClamped(v,"pitchEnv",d.pitchEnv,-48.f,48.f); d.pitchEnvDecay=finiteClamped(v,"pitchEnvDecay",d.pitchEnvDecay,.005f,4.f); d.transientLevel=finiteClamped(v,"transientLevel",d.transientLevel,0.f,1.f); d.transientDecay=finiteClamped(v,"transientDecay",d.transientDecay,.001f,1.f);
    d.fmAmount=finiteClamped(v,"fmAmount",d.fmAmount,0.f,1.f); d.fmRatio=finiteClamped(v,"fmRatio",d.fmRatio,.25f,16.f); d.ringMod=finiteClamped(v,"ringMod",d.ringMod,0.f,1.f); d.bitCrush=finiteClamped(v,"bitCrush",d.bitCrush,0.f,1.f); d.downsample=finiteClamped(v,"downsample",d.downsample,0.f,1.f);
    d.attack=finiteClamped(v,"attack",d.attack,.001f,30.f); d.decay=finiteClamped(v,"decay",d.decay,.001f,30.f); d.sustain=finiteClamped(v,"sustain",d.sustain,0.f,1.f); d.release=finiteClamped(v,"release",d.release,.001f,60.f);
    d.filterMode=static_cast<FilterMode>(juce::jlimit(0,2,(int)v.getProperty("filterMode",0))); d.cutoff=finiteClamped(v,"cutoff",d.cutoff,20.f,22000.f); d.resonance=finiteClamped(v,"resonance",d.resonance,.01f,.95f); d.filterEnv=finiteClamped(v,"filterEnv",d.filterEnv,-1.f,1.f);
    d.lfoShape=schema>=9?static_cast<LfoShape>(juce::jlimit(0,4,(int)v.getProperty("lfoShape",0))):LfoShape::sine; d.lfoRate=finiteClamped(v,"lfoRate",d.lfoRate,.01f,40.f); d.lfoCutoff=finiteClamped(v,"lfoCutoff",d.lfoCutoff,-1.f,1.f); d.lfoPitch=finiteClamped(v,"lfoPitch",d.lfoPitch,-1.f,1.f); d.lfoMorphA=finiteClamped(v,"lfoMorphA",d.lfoMorphA,-1.f,1.f); d.lfoMorphB=finiteClamped(v,"lfoMorphB",d.lfoMorphB,-1.f,1.f);
    d.drive=finiteClamped(v,"drive",d.drive,0.f,1.f); d.width=finiteClamped(v,"width",d.width,0.f,1.f); d.chorus=finiteClamped(v,"chorus",d.chorus,0.f,1.f); d.chorusRate=finiteClamped(v,"chorusRate",d.chorusRate,.02f,8.f); d.chorusDepth=finiteClamped(v,"chorusDepth",d.chorusDepth,0.f,1.f); d.reverb=finiteClamped(v,"reverb",d.reverb,0.f,1.f); d.delay=finiteClamped(v,"delay",d.delay,0.f,1.f);
    d.macroBrightness=finiteClamped(v,"macroBrightness",.5f,0.f,1.f); d.macroMovement=finiteClamped(v,"macroMovement",.5f,0.f,1.f); d.macroSpace=finiteClamped(v,"macroSpace",.5f,0.f,1.f); d.macroImpact=finiteClamped(v,"macroImpact",.5f,0.f,1.f);
    d.name=v.getProperty("name",d.name); d.sourcePrompt=v.getProperty("prompt",""); return d;
}
}