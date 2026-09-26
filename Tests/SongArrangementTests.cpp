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
    sonara::SongArrangement b;b.generate("emotional progressive house F minor energetic",128.0,123456789ULL);
    if(b.getLanes().size()!=a.getLanes().size()||b.getLanes()[0].notes.size()!=a.getLanes()[0].notes.size()){std::cerr<<"Determinism failed\n";return 6;}
    return 0;
}