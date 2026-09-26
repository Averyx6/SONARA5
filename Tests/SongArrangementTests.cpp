#include <JuceHeader.h>
#include "../Source/Generation/SongArrangement.h"
#include <cmath>
#include <iostream>

namespace {
const sonara::ArrangementLane* findLane(const sonara::SongArrangement& a, const juce::String& name)
{
    for (const auto& lane : a.getLanes()) if (lane.name == name) return &lane;
    return nullptr;
}

bool laneEventsDiffer(const sonara::ArrangementLane& a, const sonara::ArrangementLane& b)
{
    if (a.notes.size() != b.notes.size()) return true;
    const int n = juce::jmin(96, (int)a.notes.size());
    for (int i = 0; i < n; ++i)
    {
        const auto& x = a.notes[(size_t)i];
        const auto& y = b.notes[(size_t)i];
        if (x.note != y.note || x.velocity != y.velocity
            || std::abs(x.beat-y.beat) > .01 || std::abs(x.length-y.length) > .01)
            return true;
    }
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
    if(a.getSections().size()!=7){std::cerr<<"Expected 7 sections\n";return 3;}
    if(a.getLanes().size()<11){std::cerr<<"Expected at least 11 lanes\n";return 4;}

    size_t notes=0;bool hasDrums=false,hasLead=false;
    for(const auto& lane:a.getLanes()){notes+=lane.notes.size();hasDrums|=lane.drums;hasLead|=lane.name=="LEAD";}
    if(notes<700||!hasDrums||!hasLead){std::cerr<<"Arrangement too sparse or missing key lanes\n";return 5;}

    auto midi=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonara-arrangement-test",".mid");
    if(!a.writeMidiFile(midi)||!midi.existsAsFile()||midi.getSize()<512){std::cerr<<"MIDI export failed\n";return 6;}
    midi.deleteFile();

    sonara::SongArrangement fresh;
    fresh.generate(prompt,120.0,987654321ULL);

    const juce::String requiredFresh[]={
        "KICK","SNARE / CLAP","HATS","PERCUSSION","BASS","CHORDS","PLUCK","PAD","LEAD","COUNTER","FX / TRANSITIONS"
    };
    for(const auto& name:requiredFresh)
    {
        const auto* first=findLane(a,name);
        const auto* second=findLane(fresh,name);
        if(!first||!second){std::cerr<<"Missing lane "<<name<<"\n";return 7;}
        if(first->sound.seed==second->sound.seed){std::cerr<<"SoundDNA seed did not change for "<<name<<"\n";return 8;}
        if(!laneEventsDiffer(*first,*second)){std::cerr<<"Lane stayed effectively identical across new song seed: "<<name<<"\n";return 9;}
    }

    const auto* leadA=findLane(a,"LEAD");
    const auto* leadFresh=findLane(fresh,"LEAD");
    int compared=0,different=0;
    const int n=juce::jmin((int)leadA->notes.size(),(int)leadFresh->notes.size());
    for(int i=0;i<n&&i<64;++i)
    {
        ++compared;
        const auto& x=leadA->notes[(size_t)i];
        const auto& y=leadFresh->notes[(size_t)i];
        if(x.note!=y.note||std::abs(x.beat-y.beat)>.02||std::abs(x.length-y.length)>.02)++different;
    }
    if(compared<12||different<compared/2){std::cerr<<"Different seed produced lead melody that is too similar\n";return 10;}

    sonara::SongArrangement deterministic;
    deterministic.generate(prompt,120.0,123456789ULL);
    if(deterministic.getLanes().size()!=a.getLanes().size()){std::cerr<<"Determinism lane count failed\n";return 11;}
    for(size_t i=0;i<a.getLanes().size();++i)
        if(deterministic.getLanes()[i].notes.size()!=a.getLanes()[i].notes.size())
        {std::cerr<<"Determinism note count failed\n";return 12;}

    sonara::SongArrangement dnb;
    dnb.generate("energetic drum and bass 174 BPM D minor fast aggressive",128.0,4567ULL);
    if(std::abs(dnb.getBpm()-174.0)>.01){std::cerr<<"174 BPM prompt parse failed\n";return 13;}

    std::cout<<"SONARA full-lane freshness + melody + prompt BPM tests passed\n";
    return 0;
}
