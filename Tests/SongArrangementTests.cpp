#include <JuceHeader.h>
#include "../Source/Generation/SongArrangement.h"
#include <cmath>
#include <iostream>

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

    if(a.getLanes().size()<11){std::cerr<<"Expected at least 11 lanes\n";return 5;}
    size_t notes=0;bool hasDrums=false,hasLead=false;
    for(const auto& lane:a.getLanes()){notes+=lane.notes.size();hasDrums|=lane.drums;hasLead|=lane.name=="LEAD";}
    if(notes<650||!hasDrums||!hasLead){std::cerr<<"Arrangement too sparse or missing key lanes\n";return 6;}

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
        "KICK","SNARE / CLAP","HATS","PERCUSSION","BASS","CHORDS","PLUCK","PAD","LEAD","COUNTER","FX / TRANSITIONS"
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

    // Repeated generation from the same prompt must keep producing distinct lead identities.
    const uint64_t freshnessSeeds[]={10101ULL,20202ULL,30303ULL,40404ULL,50505ULL};
    std::array<sonara::SongArrangement,5> variants;
    for(size_t i=0;i<variants.size();++i)variants[i].generate(prompt,120.0,freshnessSeeds[i]);
    for(size_t i=0;i<variants.size();++i)
        for(size_t j=i+1;j<variants.size();++j)
        {
            const auto* li=findLane(variants[i],"LEAD");
            const auto* lj=findLane(variants[j],"LEAD");
            if(!li||!lj){std::cerr<<"Freshness variant missing lead\n";return 14;}
            if(structuralDifference(*li,*lj)<.42)
            {
                std::cerr<<"Same-prompt melody variants collapsed toward one phrase\n";
                return 15;
            }
        }

    // Prompt language must alter melody grammar, not only the sound palette.
    sonara::SongArrangement techMelody,cinematicMelody;
    techMelody.generate("dark minimal tech house 126 BPM F minor sparse short hook",126.0,0x5555ULL);
    cinematicMelody.generate("cinematic emotional EDM 126 BPM F minor long expressive melody",126.0,0x5555ULL);
    const auto* techLead=findLane(techMelody,"LEAD");
    const auto* cineLead=findLane(cinematicMelody,"LEAD");
    if(!techLead||!cineLead||structuralDifference(*techLead,*cineLead)<.30)
    {std::cerr<<"Prompt genres did not alter melody grammar\n";return 16;}

    auto avgLength=[](const sonara::ArrangementLane& lane)
    {
        double sum=0.0;for(const auto& n:lane.notes)sum+=n.length;
        return lane.notes.empty()?0.0:sum/(double)lane.notes.size();
    };
    if(!(avgLength(*cineLead)>avgLength(*techLead)*1.25))
    {std::cerr<<"Cinematic/tech melody articulation not distinct enough\n";return 17;}

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
