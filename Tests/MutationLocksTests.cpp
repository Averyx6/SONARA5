#include <JuceHeader.h>
#include "../Source/Generation/PromptGenerator.h"
#include <cmath>
#include <iostream>
#include <limits>
namespace { bool same(float a,float b){return std::abs(a-b)<1.0e-7f;} int fail(const char* m){std::cerr<<"SONARA test failure: "<<m<<'\n';return 1;} }
int main(){
 sonara::PromptGenerator generator; auto source=generator.generate("lush evolving reese pad",0x12345678ULL); source.filterMode=sonara::FilterMode::bandpass; source.lfoShape=sonara::LfoShape::sawDown;
 source.macroBrightness=.17f; source.macroMovement=.31f; source.macroSpace=.73f; source.macroImpact=.88f;
 sonara::MutationLocks locks; locks.oscillators=locks.unison=locks.ampEnvelope=locks.filter=true; locks.modulation=locks.sources=locks.tone=locks.spatialFx=true;
 const auto locked=generator.mutate(source,0xabcdefULL,1.f,locks);
 if(locked.oscA!=source.oscA||locked.oscB!=source.oscB||!same(locked.oscMix,source.oscMix)||!same(locked.oscBTranspose,source.oscBTranspose)||!same(locked.oscAMorph,source.oscAMorph)||!same(locked.oscBMorph,source.oscBMorph))return fail("oscillator lock");
 if(locked.unison!=source.unison||!same(locked.detune,source.detune)||!same(locked.unisonBlend,source.unisonBlend)||!same(locked.phaseRandom,source.phaseRandom))return fail("unison lock");
 if(!same(locked.attack,source.attack)||!same(locked.decay,source.decay)||!same(locked.sustain,source.sustain)||!same(locked.release,source.release))return fail("envelope lock");
 if(locked.filterMode!=source.filterMode||!same(locked.cutoff,source.cutoff)||!same(locked.resonance,source.resonance)||!same(locked.filterEnv,source.filterEnv))return fail("filter lock/topology");
 if(locked.lfoShape!=source.lfoShape||!same(locked.lfoRate,source.lfoRate)||!same(locked.lfoCutoff,source.lfoCutoff)||!same(locked.lfoPitch,source.lfoPitch)||!same(locked.lfoMorphA,source.lfoMorphA)||!same(locked.lfoMorphB,source.lfoMorphB))return fail("modulation lock");
 if(!same(locked.subLevel,source.subLevel)||!same(locked.subOctave,source.subOctave)||!same(locked.noiseLevel,source.noiseLevel))return fail("source lock");
 if(!same(locked.drive,source.drive)||!same(locked.width,source.width))return fail("tone lock");
 if(!same(locked.chorus,source.chorus)||!same(locked.chorusRate,source.chorusRate)||!same(locked.chorusDepth,source.chorusDepth)||!same(locked.reverb,source.reverb)||!same(locked.delay,source.delay))return fail("spatial FX lock");
 const auto state=source.toValueTree(); if((int)state.getProperty("schema",0)!=10)return fail("SoundDNA schema 10 missing"); const auto restoredDNA=sonara::SoundDNA::fromValueTree(state); if(restoredDNA.filterMode!=sonara::FilterMode::bandpass)return fail("filter topology round-trip"); if(restoredDNA.lfoShape!=sonara::LfoShape::sawDown)return fail("LFO shape round-trip");
 if(!same(restoredDNA.macroBrightness,.17f)||!same(restoredDNA.macroMovement,.31f)||!same(restoredDNA.macroSpace,.73f)||!same(restoredDNA.macroImpact,.88f))return fail("macro state round-trip");
 auto hostile=state.createCopy(); const float nan=std::numeric_limits<float>::quiet_NaN(), inf=std::numeric_limits<float>::infinity(); hostile.setProperty("cutoff",nan,nullptr); hostile.setProperty("resonance",inf,nullptr); hostile.setProperty("attack",nan,nullptr); hostile.setProperty("lfoRate",-inf,nullptr); hostile.setProperty("drive",nan,nullptr); hostile.setProperty("chorusDepth",inf,nullptr); hostile.setProperty("macroMovement",nan,nullptr); const auto sanitized=sonara::SoundDNA::fromValueTree(hostile); if(!std::isfinite(sanitized.cutoff)||!std::isfinite(sanitized.resonance)||!std::isfinite(sanitized.attack)||!std::isfinite(sanitized.lfoRate)||!std::isfinite(sanitized.drive)||!std::isfinite(sanitized.chorusDepth)||!std::isfinite(sanitized.macroMovement))return fail("non-finite state reached DSP"); if(!same(sanitized.cutoff,12000.f)||!same(sanitized.lfoRate,.35f)||!same(sanitized.macroMovement,.5f))return fail("non-finite state fallback");
 auto preLfo=state.createCopy(); preLfo.removeProperty("lfoShape",nullptr); preLfo.setProperty("schema",8,nullptr); if(sonara::SoundDNA::fromValueTree(preLfo).lfoShape!=sonara::LfoShape::sine)return fail("legacy LFO migration");
 auto badLfo=state.createCopy(); badLfo.setProperty("lfoShape",999,nullptr); if(sonara::SoundDNA::fromValueTree(badLfo).lfoShape!=sonara::LfoShape::square)return fail("LFO shape clamp");
 auto preMacro=state.createCopy(); preMacro.removeProperty("macroBrightness",nullptr); preMacro.removeProperty("macroMovement",nullptr); preMacro.removeProperty("macroSpace",nullptr); preMacro.removeProperty("macroImpact",nullptr); preMacro.setProperty("schema",7,nullptr); const auto migratedMacro=sonara::SoundDNA::fromValueTree(preMacro); if(!same(migratedMacro.macroBrightness,.5f)||!same(migratedMacro.macroMovement,.5f)||!same(migratedMacro.macroSpace,.5f)||!same(migratedMacro.macroImpact,.5f))return fail("legacy macro migration");
 auto badMacro=state.createCopy(); badMacro.setProperty("macroSpace",9.f,nullptr); badMacro.setProperty("macroImpact",-3.f,nullptr); const auto clampedMacro=sonara::SoundDNA::fromValueTree(badMacro); if(!same(clampedMacro.macroSpace,1.f)||!same(clampedMacro.macroImpact,0.f))return fail("macro clamp");
 auto legacy=state.createCopy(); legacy.removeProperty("filterMode",nullptr); legacy.setProperty("schema",6,nullptr); if(sonara::SoundDNA::fromValueTree(legacy).filterMode!=sonara::FilterMode::lowpass)return fail("legacy filter migration");
 auto corrupt=state.createCopy(); corrupt.setProperty("filterMode",999,nullptr); if(sonara::SoundDNA::fromValueTree(corrupt).filterMode!=sonara::FilterMode::bandpass)return fail("filter topology clamp");
 if(generator.generate("airy high-pass atmospheric layer",11).filterMode!=sonara::FilterMode::highpass)return fail("high-pass prompt intent"); if(generator.generate("nasal band-pass vocal texture",12).filterMode!=sonara::FilterMode::bandpass)return fail("band-pass prompt intent"); if(generator.generate("warm low-pass bass",13).filterMode!=sonara::FilterMode::lowpass)return fail("low-pass prompt intent");
 const auto analog=generator.generate("warm vintage analog lead",301); const auto glassy=generator.generate("glassy crystal lead",301); const auto organic=generator.generate("organic woody pluck",301);
 if(analog.oscA==glassy.oscA&&analog.oscB==glassy.oscB&&same(analog.fmAmount,glassy.fmAmount))return fail("analog/glassy SoundDNA identity collapsed");
 if(!(glassy.fmAmount>analog.fmAmount))return fail("glassy SoundDNA should use stronger FM character");
 if(!(organic.transientLevel>.08f&&organic.noiseLevel>.005f))return fail("organic SoundDNA transient/texture missing");
 const auto repeatedA=generator.generate("wide emotional lead",777); const auto repeatedB=generator.generate("wide emotional lead",778);
 if(repeatedA.oscA==repeatedB.oscA&&repeatedA.oscB==repeatedB.oscB&&same(repeatedA.oscMix,repeatedB.oscMix)&&same(repeatedA.oscAMorph,repeatedB.oscAMorph)&&same(repeatedA.fmAmount,repeatedB.fmAmount))return fail("seeded SoundDNA character did not vary");

 const auto bell=generator.generate("cinematic bell",401); const auto metal=generator.generate("metallic digital hit",401);
 if(bell.name==metal.name||same(bell.ringMod,metal.ringMod)&&same(bell.fmRatio,metal.fmRatio))return fail("bell/metallic identity collapsed");
 const auto digital=generator.generate("digital FM lead",402); if(digital.fmAmount<.25f)return fail("digital/FM identity too weak");
 const auto cleanBass=generator.generate("clean mono bass",403); const auto reese=generator.generate("dark moving reese bass",403);
 if(!(reese.unison>cleanBass.unison&&reese.width>cleanBass.width))return fail("reese identity not wider/more complex than clean bass");
 const auto pureSub=generator.generate("pure clean sine sub mono lowpass dry",404);
 if(pureSub.unison!=1||pureSub.width>.06f||pureSub.cutoff>180.f||pureSub.reverb>.02f||pureSub.delay>.02f||pureSub.chorus>.02f)return fail("pure sub is not mono/dry/low-passed");

 const auto festivalLead=generator.generate("huge mainstage festival supersaw lead",501);
 const auto festivalPluck=generator.generate("bright festival pluck",501);
 const auto synthBrass=generator.generate("powerful brass horn stab",501);
 const auto strings=generator.generate("emotional wide string ensemble",501);
 const auto flute=generator.generate("airy expressive flute",501);
 const auto growl=generator.generate("aggressive neuro growl bass",501);
 if(festivalLead.name!="Generated Festival Lead"||festivalLead.unison<8||festivalLead.width<.9f||festivalLead.transientLevel<.10f)return fail("festival lead semantic design missing");
 if(festivalPluck.name!="Generated Festival Pluck"||festivalPluck.decay>.24f||festivalPluck.transientLevel<.15f)return fail("festival pluck semantic design missing");
 if(synthBrass.name!="Generated Synth Brass"||synthBrass.filterEnv<.25f)return fail("brass semantic design missing");
 if(strings.name!="Generated String Ensemble"||strings.attack<.10f||strings.release<.70f)return fail("string semantic design missing");
 if(flute.name!="Generated Air Flute"||flute.unison!=1||flute.noiseLevel<.02f)return fail("flute semantic design missing");
 if(growl.name!="Generated Growl Bass"||growl.fmAmount<.30f||growl.drive<.25f)return fail("growl semantic design missing");
 const auto festivalKick=generator.generate("festival mainstage punchy kick",502);
 if(festivalKick.transientLevel<.70f||festivalKick.pitchEnv<38.f||festivalKick.reverb>.01f)return fail("festival kick impact missing");

 const auto acid=generator.generate("resonant acid 303 bass staccato",601);
 const auto psy=generator.generate("tight psytrance psy bass fast attack",602);
 const auto screech=generator.generate("wide hardstyle screech aggressive",603);
 const auto reed=generator.generate("expressive saxophone reed vibrato",604);
 const auto harp=generator.generate("soft harp picked string",605);
 const auto kalimba=generator.generate("organic kalimba thumb piano",606);
 const auto formant=generator.generate("moving formant lead vocal synth",607);
 const auto chip=generator.generate("retro game chiptune chip lead",608);
 if(acid.name!="Generated Acid"||acid.resonance<.45f||acid.filterEnv<.60f||acid.width>.10f)return fail("acid/303 semantic design missing");
 if(psy.name!="Generated Psy Bass"||psy.width>.08f||psy.decay>.15f||psy.subLevel<.20f)return fail("psy bass semantic design missing");
 if(screech.name!="Generated Screech"||screech.drive<.30f||screech.filterMode!=sonara::FilterMode::bandpass)return fail("hardstyle screech semantic design missing");
 if(reed.name!="Generated Reed"||reed.filterMode!=sonara::FilterMode::bandpass||reed.lfoPitch<.03f)return fail("reed/sax semantic design missing");
 if(harp.name!="Generated Harp"||harp.transientLevel<.15f||harp.sustain>.12f)return fail("harp semantic design missing");
 if(kalimba.name!="Generated Kalimba"||kalimba.fmAmount<.15f||kalimba.transientLevel<.30f)return fail("kalimba semantic design missing");
 if(formant.name!="Generated Formant"||formant.filterMode!=sonara::FilterMode::bandpass||formant.resonance<.34f)return fail("formant semantic design missing");
 if(chip.name!="Generated Chip"||chip.unison!=1||chip.bitCrush<.25f)return fail("chiptune semantic design missing");
 const auto legato=generator.generate("warm lead legato sustained",609);
 const auto staccato=generator.generate("warm lead staccato short decay",609);
 if(!(legato.release>staccato.release*2.f&&legato.sustain>staccato.sustain))return fail("articulation language did not alter envelope");

 if(generator.generate("gated square lfo bass",21).lfoShape!=sonara::LfoShape::square)return fail("square LFO prompt intent"); if(generator.generate("rising lfo animated pad",22).lfoShape!=sonara::LfoShape::sawUp)return fail("rising LFO prompt intent"); if(generator.generate("falling lfo texture",23).lfoShape!=sonara::LfoShape::sawDown)return fail("falling LFO prompt intent"); if(generator.generate("triangle lfo evolving texture",24).lfoShape!=sonara::LfoShape::triangle)return fail("triangle LFO prompt intent"); if(generator.generate("vibrato gated lead",25).lfoShape!=sonara::LfoShape::sine)return fail("vibrato must use smooth sine LFO");
 auto similarSource=generator.generate("high-pass animated texture",14); similarSource.filterMode=sonara::FilterMode::bandpass; similarSource.lfoShape=sonara::LfoShape::sawUp; const auto similar=generator.mutate(similarSource,15,.18f); if(similar.filterMode!=sonara::FilterMode::bandpass)return fail("similar generation changed discrete filter topology"); if(similar.lfoShape!=sonara::LfoShape::sawUp)return fail("similar generation changed discrete LFO shape");
 const sonara::MutationLocks none; const auto mutated=generator.mutate(source,0xabcdefULL,1.f,none); if(same(mutated.attack,source.attack)&&same(mutated.cutoff,source.cutoff)&&same(mutated.oscAMorph,source.oscAMorph))return fail("unlocked mutation did not evolve"); const auto repeated=generator.mutate(source,0xabcdefULL,1.f,none); if(!same(repeated.attack,mutated.attack)||!same(repeated.lfoRate,mutated.lfoRate)||!same(repeated.chorus,mutated.chorus)||repeated.filterMode!=mutated.filterMode||repeated.lfoShape!=mutated.lfoShape)return fail("mutation is not deterministic");
 const auto tree=locks.toValueTree(); if((int)tree.getProperty("schema",0)!=sonara::MutationLocks::schemaVersion)return fail("lock schema missing"); const auto restored=sonara::MutationLocks::fromValueTree(tree); if(!restored.oscillators||!restored.unison||!restored.ampEnvelope||!restored.filter||!restored.modulation||!restored.sources||!restored.tone||!restored.spatialFx)return fail("lock state round-trip"); if(sonara::MutationLocks::fromValueTree(juce::ValueTree("UNKNOWN")).any())return fail("invalid lock state was accepted");
 auto futureLocks=tree.createCopy(); futureLocks.setProperty("schema",sonara::MutationLocks::schemaVersion+1,nullptr); if(sonara::MutationLocks::fromValueTree(futureLocks).any())return fail("future lock schema was accepted"); auto corruptLocks=tree.createCopy(); corruptLocks.setProperty("schema",-7,nullptr); if(sonara::MutationLocks::fromValueTree(corruptLocks).any())return fail("corrupt lock schema was accepted"); auto legacyLocks=tree.createCopy(); legacyLocks.removeProperty("schema",nullptr); if(!sonara::MutationLocks::fromValueTree(legacyLocks).any())return fail("legacy lock state migration failed");
 std::cout<<"SONARA mutation/state/filter/LFO/prompt/macro/sanitization tests passed\n"; return 0;
}