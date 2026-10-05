#include "../Source/Generation/SongArrangement.h"
#include "../Source/Generation/ProducerPrompt.h"
#include <iostream>
#include <set>

namespace {
int fail(const char* reason){std::cerr<<"Producer intelligence: "<<reason<<'\n';return 1;}
const sonara::ArrangementLane& lane(const sonara::SongArrangement& song,const juce::String& name)
{for(const auto& l:song.getLanes())if(l.name==name)return l;throw std::runtime_error("Missing lane");}
}

int main()
{
    using sonara::ProducerPrompt;
    const auto negated=ProducerPrompt::parse("emotional progressive house, not dark, without pads and hats, avoid counter melody; dry melodic drop");
    if(negated.positive.contains("dark")||negated.positive.contains("pads")||negated.positive.contains("hats")
       ||(negated.exclusions&(ProducerPrompt::pad|ProducerPrompt::hats|ProducerPrompt::counter))
          !=(ProducerPrompt::pad|ProducerPrompt::hats|ProducerPrompt::counter))return fail("Negation leaked into positive direction");
    for(const juce::String prompt:{"no pads, no hats", "without pads and hi-hats", "exclude: pads / hats", "avoid pads and hats"})
    {
        sonara::SongArrangement song;song.generate(prompt+", progressive house 128 BPM F minor",128.0,0x410041);
        if(!song.validate()||!lane(song,"PAD").notes.empty()||!lane(song,"HATS").notes.empty())
            return fail("An exclusion alias generated forbidden notes");
    }
    sonara::SongArrangement reduced;
    reduced.generate("EDM, no drums, no bass, without sub, no chords, no counter, no pads, no arps, no fx",128,42);
    if(!reduced.validate())return fail("Lead-only arrangement is invalid");
    for(const auto& l:reduced.getLanes())if(l.name!="LEAD"&&!l.notes.empty())return fail("Excluded lane survived");
    sonara::SongArrangement duration;
    duration.generateComposition("progressive house, 128 BPM F minor, 120 seconds",120,1);
    if(duration.getBars()!=64||duration.getBpm()!=128)return fail("Seconds did not control duration");
    duration.generateComposition("progressive house, 128 BPM F minor, 2 minutes, 80 bars",120,1);
    if(duration.getBars()!=80)return fail("Explicit bars did not take precedence over duration");
    const juce::String prompt="progressive house 128 BPM F minor, sparse, dry, soft melodic drop";
    sonara::SongArrangement first,again;
    first.generate(prompt,128,0xfedcba9876543210ULL);again.generate(prompt,128,0xfedcba9876543210ULL);
    if(!first.validate()||first.toValueTree().createXml()->toString()!=again.toValueTree().createXml()->toString())
        return fail("Full plan, notes or SoundDNA are not seed deterministic");
    const auto plan=first.toValueTree().getChildWithName("COMPOSITION_DNA");
    if((float)plan.getProperty("density")>.49f||(float)plan.getProperty("space")>.23f
       ||(float)plan.getProperty("aggression")>.31f)return fail("Genre defaults overwrote explicit production intent");
    std::set<std::vector<int>> melodies,harmonies,palettes,structures;
    for(uint64_t seed=100;seed<116;++seed)
    {
        sonara::SongArrangement song;song.generate(prompt,128,seed);
        if(!song.validate())return fail("Generated arrangement violates event/timing budget");
        melodies.insert(song.getMelodyFingerprint());harmonies.insert(song.getProgressionFingerprint());
        palettes.insert(song.getSoundPaletteFingerprint());structures.insert(song.getStructureFingerprint());
    }
    if(melodies.size()<14||harmonies.size()<12||palettes.size()!=16||structures.size()<12)
        return fail("Seeds did not change the composition and timbral world");
    std::cout<<"Producer prompt constraints, complete deterministic plans and seed diversity passed\n";
    return 0;
}
