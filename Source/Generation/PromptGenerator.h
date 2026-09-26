#pragma once
#include "../Engine/SoundDNA.h"
#include <functional>
#include <utility>

namespace sonara {
struct MutationLocks {
    static constexpr int schemaVersion = 2;
    bool oscillators=false, unison=false, ampEnvelope=false, filter=false, modulation=false, sources=false, tone=false, spatialFx=false;
    [[nodiscard]] bool any() const noexcept { return oscillators||unison||ampEnvelope||filter||modulation||sources||tone||spatialFx; }
    void apply(const SoundDNA& source, SoundDNA& candidate) const noexcept {
        if(oscillators){candidate.oscA=source.oscA;candidate.oscB=source.oscB;candidate.oscMix=source.oscMix;candidate.oscBTranspose=source.oscBTranspose;candidate.oscAMorph=source.oscAMorph;candidate.oscBMorph=source.oscBMorph;}
        if(unison){candidate.unison=source.unison;candidate.detune=source.detune;candidate.unisonBlend=source.unisonBlend;candidate.phaseRandom=source.phaseRandom;}
        if(ampEnvelope){candidate.attack=source.attack;candidate.decay=source.decay;candidate.sustain=source.sustain;candidate.release=source.release;}
        if(filter){candidate.filterMode=source.filterMode;candidate.cutoff=source.cutoff;candidate.resonance=source.resonance;candidate.filterEnv=source.filterEnv;}
        if(modulation){candidate.lfoRate=source.lfoRate;candidate.lfoCutoff=source.lfoCutoff;candidate.lfoPitch=source.lfoPitch;candidate.lfoMorphA=source.lfoMorphA;candidate.lfoMorphB=source.lfoMorphB;}
        if(sources){candidate.subLevel=source.subLevel;candidate.subOctave=source.subOctave;candidate.noiseLevel=source.noiseLevel;candidate.transientLevel=source.transientLevel;candidate.transientDecay=source.transientDecay;}
        if(tone){candidate.drive=source.drive;candidate.width=source.width;candidate.pitchEnv=source.pitchEnv;candidate.pitchEnvDecay=source.pitchEnvDecay;candidate.fmAmount=source.fmAmount;candidate.fmRatio=source.fmRatio;candidate.ringMod=source.ringMod;candidate.bitCrush=source.bitCrush;candidate.downsample=source.downsample;}
        if(spatialFx){candidate.chorus=source.chorus;candidate.chorusRate=source.chorusRate;candidate.chorusDepth=source.chorusDepth;candidate.reverb=source.reverb;candidate.delay=source.delay;}
    }
    [[nodiscard]] juce::ValueTree toValueTree() const {juce::ValueTree t("MUTATION_LOCKS");t.setProperty("schema",schemaVersion,nullptr);t.setProperty("oscillators",oscillators,nullptr);t.setProperty("unison",unison,nullptr);t.setProperty("ampEnvelope",ampEnvelope,nullptr);t.setProperty("filter",filter,nullptr);t.setProperty("modulation",modulation,nullptr);t.setProperty("sources",sources,nullptr);t.setProperty("tone",tone,nullptr);t.setProperty("spatialFx",spatialFx,nullptr);return t;}
    [[nodiscard]] static MutationLocks fromValueTree(const juce::ValueTree& t) noexcept {
        MutationLocks r;
        if(!t.isValid()||t.getType().toString()!="MUTATION_LOCKS") return r;
        // Schema 0 represents early lock trees that pre-date explicit versioning.
        // Reject future/corrupt schemas instead of silently interpreting fields with
        // semantics this build does not understand.
        const int schema=static_cast<int>(t.getProperty("schema",0));
        if(schema<0||schema>schemaVersion) return r;
        r.oscillators=(bool)t.getProperty("oscillators",false);r.unison=(bool)t.getProperty("unison",false);r.ampEnvelope=(bool)t.getProperty("ampEnvelope",false);r.filter=(bool)t.getProperty("filter",false);r.modulation=(bool)t.getProperty("modulation",false);r.sources=(bool)t.getProperty("sources",false);r.tone=(bool)t.getProperty("tone",false);r.spatialFx=(bool)t.getProperty("spatialFx",false);return r;
    }
};
class PromptGenerator {
public:
    using Progress=std::function<void(float,const juce::String&)>;
    SoundDNA generate(const juce::String&,uint64_t,Progress={}) const;
    SoundDNA mutate(const SoundDNA&,uint64_t,float,Progress={}) const;
    SoundDNA mutate(const SoundDNA& source,uint64_t seed,float amount,const MutationLocks& locks,Progress progress={}) const {
        auto result=mutate(source,seed,amount,std::move(progress));
        // Macros are live performer controls, not random design dimensions. Keep their
        // positions stable through SIMILAR/MUTATE while locks protect the underlying groups.
        result.macroBrightness=source.macroBrightness; result.macroMovement=source.macroMovement;
        result.macroSpace=source.macroSpace; result.macroImpact=source.macroImpact;
        locks.apply(source,result); return result;
    }
private: static float hash01(uint64_t,uint64_t);
};
}