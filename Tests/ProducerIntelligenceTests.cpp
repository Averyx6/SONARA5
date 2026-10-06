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
    for(const int bars:{4,8,16,17,32,48,70,73,512})
    {
        duration.generate("progressive house 128 BPM F minor, "+juce::String(bars)+" bars",120,4);
        if(duration.getBars()!=bars||!duration.validate())return fail("Explicit short/non-grid bars were expanded or invalid");
    }
    for(const juce::String brief:{"only lead and bass", "lead and bass only", "only use lead and bass"})
    {
        sonara::SongArrangement only;only.generate("progressive house 128 BPM F minor, "+brief,128,0x470047);
        if(!only.validate()||lane(only,"LEAD").notes.empty()||lane(only,"BASS").notes.empty())return fail("Instrument-only brief lost an allowed lane");
        for(const auto& l:only.getLanes())if(l.name!="LEAD"&&l.name!="BASS"&&!l.notes.empty())return fail("Instrument whitelist generated another lane");
    }
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
        if(sonara::SongArrangement::fromValueTree(song.toValueTree()).toValueTree().createXml()->toString()!=song.toValueTree().createXml()->toString())
        {
            const auto temp=juce::File::getSpecialLocation(juce::File::tempDirectory);
            temp.getChildFile("sonara-producer-original.xml").replaceWithText(song.toValueTree().createXml()->toString());
            temp.getChildFile("sonara-producer-restored.xml").replaceWithText(sonara::SongArrangement::fromValueTree(song.toValueTree()).toValueTree().createXml()->toString());
            std::cerr<<"Round-trip seed "<<seed<<"\n";return fail("Restoring a generated plan changed MIDI, SoundDNA or section goals");
        }
        melodies.insert(song.getMelodyFingerprint());harmonies.insert(song.getProgressionFingerprint());
        palettes.insert(song.getSoundPaletteFingerprint());structures.insert(song.getStructureFingerprint());
    }
    if(melodies.size()<14||harmonies.size()<12||palettes.size()!=16||structures.size()<12)
        return fail("Seeds did not change the composition and timbral world");
    const auto& sections=first.getSections();
    if(sections.size()!=11||sections.front().name!="INTRO"||sections.back().name!="OUTRO")
        return fail("Developed arrangement has no complete beginning/ending");
    const auto& hook=lane(first,"LEAD");
    auto signature=[&](const juce::String& sectionName,int bars)
    {
        std::vector<int> result;
        for(const auto& section:sections)if(section.name==sectionName)
            for(const auto& n:hook.notes)if(n.beat>=section.startBar*4.0&&n.beat<(section.startBar+bars)*4.0)
            {result.push_back(n.note);result.push_back((int)std::llround((n.beat-section.startBar*4.0)*8));}
        return result;
    };
    if(signature("CHORUS",2).empty()||signature("CHORUS",2)!=signature("DROP",2)
       ||signature("DROP",2)!=signature("DROP 2",2))return fail("Drops discarded the established hook");
    if(signature("FINAL HOOK",8)==signature("DROP",8))return fail("Final hook did not develop");
    for(const auto& l:first.getLanes())for(const auto& n:l.notes)
        if(n.beat>=(first.getBars()-1)*4.0)return fail("Outro failed to leave decay space");
    std::set<int> leadMaterials;
    for(uint64_t seed=0;seed<32;++seed)
    {
        sonara::SongArrangement dark,bright;
        dark.generate("progressive house, dark dry soft 64 bars",128,seed);
        bright.generate("progressive house, bright spacious aggressive 64 bars",128,seed);
        const auto& a=lane(dark,"LEAD").sound;const auto& b=lane(bright,"LEAD").sound;
        leadMaterials.insert((int)a.oscA);
        if(a.cutoff>4800.f||b.cutoff<7800.f||a.drive>.10f||b.drive<=a.drive||b.reverb<a.reverb)
            return fail("SoundDNA flavour overrode explicit tone/space/aggression");
        const auto& sub=lane(dark,"SUB").sound;
        if(sub.oscMix!=0.f||sub.fmAmount!=0.f||sub.pitchEnv!=0.f||sub.oscAMorph!=0.f
           ||sub.transientLevel!=0.f||sub.width!=0.f)return fail("Pure sub contains hidden modulation/upper layers");
    }
    if(leadMaterials.size()<4)return fail("Lead seeds did not produce different oscillator materials");
    auto restored=sonara::SongArrangement::fromValueTree(first.toValueTree());restored.finalizeSoundPalette();
    if(restored.getSoundPaletteFingerprint()!=first.getSoundPaletteFingerprint())return fail("Restored seed domains changed AUTO FIT SoundDNA");
    auto dense=first;auto& events=dense.editLanes()[9].notes;events.clear();
    for(int i=0;i<400;++i)events.push_back({i%128,100,i*.001,.0005});
    if(dense.validate())return fail("Pathological imported event density was accepted for realtime playback");
    auto drumOnly=first;const auto backing=drumOnly.getLanes()[9].notes;
    drumOnly.regenerateDrumsOnly("drum and bass, no hats",0x490049);
    if(!drumOnly.validate()||!lane(drumOnly,"HATS").notes.empty()||lane(drumOnly,"LEAD").notes.size()!=backing.size())return fail("Drum regeneration ignored exclusions or changed backing notes");
    for(size_t i=0;i<backing.size();++i)if(backing[i].note!=lane(drumOnly,"LEAD").notes[i].note||backing[i].beat!=lane(drumOnly,"LEAD").notes[i].beat)return fail("Drum regeneration rewrote the lead");
    std::cout<<"Producer prompt constraints, complete deterministic plans and seed diversity passed\n";
    return 0;
}
