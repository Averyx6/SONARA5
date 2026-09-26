#include <JuceHeader.h>
#include "../Source/Generation/SongArrangement.h"
#include <iostream>

int main()
{
    sonara::SongArrangement a;
    a.generate("emotional progressive house F minor energetic",128.0,123456789ULL);
    if(a.getBars()!=72){std::cerr<<"Expected 72 bars\n";return 1;}
    if(a.getSections().size()!=7){std::cerr<<"Expected 7 sections\n";return 2;}
    if(a.getLanes().size()<10){std::cerr<<"Expected at least 10 lanes\n";return 3;}
    size_t notes=0;bool hasDrums=false,hasLead=false;
    for(const auto& lane:a.getLanes()){notes+=lane.notes.size();hasDrums|=lane.drums;hasLead|=lane.name=="LEAD";}
    if(notes<700||!hasDrums||!hasLead){std::cerr<<"Arrangement too sparse or missing key lanes\n";return 4;}
    auto f=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonara-arrangement-test",".mid");
    if(!a.writeMidiFile(f)||!f.existsAsFile()||f.getSize()<512){std::cerr<<"MIDI export failed\n";return 5;}
    f.deleteFile();
    // Same prompt with a different generation seed must create a genuinely different lead identity.
    sonara::SongArrangement fresh; fresh.generate("emotional progressive house F minor energetic",128.0,987654321ULL);
    const auto findLead=[](const sonara::SongArrangement& x)->const sonara::ArrangementLane*{
        for(const auto& lane:x.getLanes()) if(lane.name=="LEAD") return &lane;
        return nullptr;
    };
    const auto* leadA=findLead(a); const auto* leadFresh=findLead(fresh);
    if(!leadA||!leadFresh||leadA->notes.empty()||leadFresh->notes.empty()){std::cerr<<"Fresh melody test missing lead\n";return 9;}
    int compared=0,different=0;
    const int n=juce::jmin((int)leadA->notes.size(),(int)leadFresh->notes.size());
    for(int i=0;i<n&&i<64;++i){++compared;const auto& x=leadA->notes[(size_t)i];const auto& y=leadFresh->notes[(size_t)i];if(x.note!=y.note||std::abs(x.beat-y.beat)>.02||std::abs(x.length-y.length)>.02)++different;}
    if(compared<12||different<compared/2){std::cerr<<"Different seed produced melody that is too similar\n";return 10;}

    sonara::SongArrangement b;b.generate("emotional progressive house F minor energetic",128.0,123456789ULL);
    if(b.getLanes().size()!=a.getLanes().size()||b.getLanes()[0].notes.size()!=a.getLanes()[0].notes.size()){std::cerr<<"Determinism failed\n";return 6;}
    return 0;
}