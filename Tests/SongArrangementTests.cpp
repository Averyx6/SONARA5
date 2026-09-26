#include <JuceHeader.h>
#include "../Source/Generation/SongArrangement.h"
#include <cmath>
#include <iostream>
#include <set>
#include <cstdint>

namespace {
const sonara::ArrangementLane* findLane(const sonara::SongArrangement& a,const juce::String& name)
{
    for(const auto& lane:a.getLanes())if(lane.name==name)return &lane;
    return nullptr;
}

double structuralDifference(const sonara::ArrangementLane& a,const sonara::ArrangementLane& b)
{
    const int na=(int)a.notes.size(),nb=(int)b.notes.size();
    const int n=juce::jmin(96,juce::jmin(na,nb));
    int diff=std::abs(na-nb)>0?juce::jmin(24,std::abs(na-nb)):0;
    for(int i=0;i<n;++i)
    {
        const auto& x=a.notes[(size_t)i];
        const auto& y=b.notes[(size_t)i];
        if(x.note!=y.note||std::abs(x.beat-y.beat)>.01||std::abs(x.length-y.length)>.01)++diff;
    }
    return diff/(double)juce::jmax(1,n+juce::jmin(24,std::abs(na-nb)));
}

bool hasSection(const sonara::SongArrangement& a,const juce::String& name)
{
    for(const auto& s:a.getSections())if(s.name==name)return true;
    return false;
}

uint64_t leadFingerprintHash(const sonara::SongArrangement& a)
{
    const auto* lead=findLane(a,"LEAD");
    if(!lead)return 0;
    uint64_t h=1469598103934665603ULL;
    int prev=0;
    const int n=juce::jmin(128,(int)lead->notes.size());
    for(int i=0;i<n;++i)
    {
        const auto& note=lead->notes[(size_t)i];
        const int interval=i==0?0:juce::jlimit(-24,24,note.note-prev);
        const int values[]={
            ((note.note%12)+12)%12,
            interval+24,
            (int)std::llround(std::fmod(note.beat,16.0)*8.0),
            (int)std::llround(note.length*16.0),
            note.note/12
        };
        for(const int v:values){h^=(uint64_t)(v+257);h*=1099511628211ULL;}
        prev=note.note;
    }
    h^=(uint64_t)lead->notes.size();h*=1099511628211ULL;
    return h;
}
}

int main()
{
    const juce::String prompt="emotional progressive house 128 BPM F minor energetic powerful";
    sonara::SongArrangement a;
    a.generate(prompt,120.0,123456789ULL);

    if(a.getBars()!=72){std::cerr<<"Expected 72 bars\n";return 1;}
    if(std::abs(a.getBpm()-128.0)>.01){std::cerr<<"Prompt BPM was not applied\n";return 2;}
    if(a.getSections().size()!=8){std::cerr<<"Expected 8 named song sections\n";return 3;}

    const juce::String expectedSections[]={"INTRO","VERSE","BUILD","DROP","BREAKDOWN","CHORUS","BUILD 2","FINAL HOOK"};
    for(const auto& name:expectedSections)
        if(!hasSection(a,name)){std::cerr<<"Missing section "<<name<<"\n";return 4;}

    if(a.getLanes().size()!=12){std::cerr<<"Expected exactly 12 required lanes\n";return 5;}
    size_t notes=0;bool hasDrums=false,hasLead=false;
    for(const auto& lane:a.getLanes()){notes+=lane.notes.size();hasDrums|=lane.drums;hasLead|=lane.name=="LEAD";}
    if(notes<650||!hasDrums||!hasLead){std::cerr<<"Arrangement too sparse or missing key lanes\n";return 6;}
    const auto* subLane=findLane(a,"SUB");
    const auto* bassLane=findLane(a,"BASS");
    if(!subLane||subLane->notes.empty()||!bassLane||bassLane->notes.empty())
    {std::cerr<<"Bass/Sub lane missing real MIDI\n";return 22;}

    auto midi=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonara-arrangement-test",".mid");
    if(!a.writeMidiFile(midi)||!midi.existsAsFile()||midi.getSize()<512){std::cerr<<"MIDI export failed\n";return 7;}
    midi.deleteFile();

    sonara::SongArrangement fresh;
    fresh.generate(prompt,120.0,987654321ULL);

    bool structureChanged=false;
    if(a.getSections().size()==fresh.getSections().size())
        for(size_t i=0;i<a.getSections().size();++i)
            structureChanged|=a.getSections()[i].bars!=fresh.getSections()[i].bars;
    if(!structureChanged){std::cerr<<"Fresh seed did not change song structure\n";return 8;}

    const juce::String requiredFresh[]={
        "KICK","SNARE / CLAP","HATS","PERCUSSION","BASS","SUB","CHORDS","PLUCK","PAD","LEAD","COUNTER","FX / TRANSITIONS"
    };
    int substantiallyDifferent=0;
    for(const auto& name:requiredFresh)
    {
        const auto* first=findLane(a,name);
        const auto* second=findLane(fresh,name);
        if(!first||!second){std::cerr<<"Missing lane "<<name<<"\n";return 9;}
        if(first->sound.seed==second->sound.seed){std::cerr<<"SoundDNA seed did not change for "<<name<<"\n";return 10;}

        const double difference=structuralDifference(*first,*second);
        if(difference<=0.0){std::cerr<<"Lane stayed structurally identical: "<<name<<"\n";return 11;}
        if(difference>=.20)++substantiallyDifferent;
    }
    if(substantiallyDifferent<7){std::cerr<<"Too few lanes changed substantially across fresh song seed\n";return 12;}

    const auto* leadA=findLane(a,"LEAD");
    const auto* leadFresh=findLane(fresh,"LEAD");
    if(structuralDifference(*leadA,*leadFresh)<.55){std::cerr<<"Lead phrase family still too similar\n";return 13;}

    // TEST A: same prompt, 10 seeds, every lead pair must be materially different.
    const uint64_t freshnessSeeds[]={10101ULL,20202ULL,30303ULL,40404ULL,50505ULL,60606ULL,70707ULL,80808ULL,90909ULL,100010ULL};
    std::array<sonara::SongArrangement,10> variants;
    for(size_t i=0;i<variants.size();++i)variants[i].generate(prompt,120.0,freshnessSeeds[i]);
    for(size_t i=0;i<variants.size();++i)
        for(size_t j=i+1;j<variants.size();++j)
        {
            const auto* li=findLane(variants[i],"LEAD");
            const auto* lj=findLane(variants[j],"LEAD");
            if(!li||!lj){std::cerr<<"Freshness variant missing lead\n";return 14;}
            if(structuralDifference(*li,*lj)<.34)
            {
                std::cerr<<"Same-prompt melody variants collapsed toward one phrase\n";
                return 15;
            }
        }

    // TEST C: 20 generated songs may not produce an identical lead fingerprint.
    std::set<uint64_t> fingerprints;
    for(uint64_t i=0;i<20;++i)
    {
        sonara::SongArrangement x;
        x.generate(prompt,120.0,0xabc000ULL+i*0x10203ULL);
        const auto hash=leadFingerprintHash(x);
        if(hash==0||!fingerprints.insert(hash).second)
        {std::cerr<<"Duplicate lead fingerprint in 20-song run\n";return 21;}
    }

    // TEST B: genre language must alter melody grammar, not only SoundDNA.
    std::array<sonara::SongArrangement,4> genreSongs;
    const juce::String genrePrompts[4]={
        "dark minimal tech house 126 BPM F minor sparse short hook",
        "emotional progressive house 126 BPM F minor memorable evolving hook",
        "cinematic emotional EDM 126 BPM F minor long expressive melody",
        "drum and bass 126 BPM F minor fast syncopated call response melody"
    };
    for(size_t i=0;i<genreSongs.size();++i)genreSongs[i].generate(genrePrompts[i],126.0,0x5555ULL);

    std::array<const sonara::ArrangementLane*,4> genreLeads{};
    for(size_t i=0;i<genreSongs.size();++i)
    {
        genreLeads[i]=findLane(genreSongs[i],"LEAD");
        if(!genreLeads[i]){std::cerr<<"Genre test missing lead\n";return 16;}
    }
    for(size_t i=0;i<genreLeads.size();++i)
        for(size_t j=i+1;j<genreLeads.size();++j)
            if(structuralDifference(*genreLeads[i],*genreLeads[j])<.20)
            {std::cerr<<"Genre prompts did not alter melody grammar\n";return 17;}

    auto avgLength=[](const sonara::ArrangementLane& lane)
    {
        double sum=0.0;for(const auto& n:lane.notes)sum+=n.length;
        return lane.notes.empty()?0.0:sum/(double)lane.notes.size();
    };
    if(!(avgLength(*genreLeads[2])>avgLength(*genreLeads[0])*1.25))
    {std::cerr<<"Cinematic/tech melody articulation not distinct enough\n";return 23;}

    sonara::SongArrangement deterministic;
    deterministic.generate(prompt,120.0,123456789ULL);
    if(deterministic.getLanes().size()!=a.getLanes().size()){std::cerr<<"Determinism lane count failed\n";return 18;}
    for(size_t i=0;i<a.getLanes().size();++i)
        if(deterministic.getLanes()[i].notes.size()!=a.getLanes()[i].notes.size())
        {std::cerr<<"Determinism note count failed\n";return 19;}

    sonara::SongArrangement dnb;
    dnb.generate("energetic drum and bass 174 BPM D minor fast aggressive",128.0,4567ULL);
    if(std::abs(dnb.getBpm()-174.0)>.01){std::cerr<<"174 BPM prompt parse failed\n";return 20;}

    std::cout<<"SONARA structure + full-lane freshness + phrase-family + BPM tests passed\n";
    return 0;
}
