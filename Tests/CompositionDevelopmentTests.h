#pragma once
#include "../Source/PluginProcessor.h"
#include <set>
#include <iostream>

namespace sonara::compositiontests {
inline std::vector<int> phrase(const SongArrangement& song,const ArrangementSection& section,int bars) {
    std::vector<int> result;
    const auto& lead=song.getLanes()[9];
    const double begin=section.startBar*4.0,end=begin+std::min(bars,section.bars)*4.0;
    for(const auto& note:lead.notes)if(note.beat>=begin&&note.beat<end) {
        result.push_back(note.note-song.getRootMidi());
        result.push_back((int)std::llround((note.beat-begin)*16.0));
        result.push_back((int)std::llround(note.length*32.0));
    }
    return result;
}
inline std::vector<int> hook(const SongArrangement& song,const ArrangementSection& section) {
    auto notes=phrase(song,section,2);std::vector<int> result;
    // DROP articulation is intentionally shorter than CHORUS. Test pitch/onset
    // identity separately from that musical role rather than requiring sameness.
    for(size_t i=0;i+2<notes.size();i+=3){result.push_back(notes[i]);result.push_back(notes[i+1]);}
    return result;
}
inline int run() {
    const char* prompts[]={
        "progressive house 128 BPM F minor 64 bars memorable hook evolving final drop",
        "tech house 126 BPM A minor 64 bars sparse rhythmic hook no pads evolving final drop",
        "trance 138 BPM D minor 64 bars memorable hook evolving final drop"};
    for(int genre=0;genre<3;++genre) {
        std::set<std::vector<int>> melodies,harmonies,renderedHarmony,grooves;
        for(uint64_t seed=0x530000;seed<0x530010;++seed) {
            SonaraAudioProcessor processor;processor.prepareToPlay(44100.0,512);
            if(!processor.generateTrackWithSeed(prompts[genre],seed))return 1;
            const auto song=processor.arrangementSnapshot();
            const ArrangementSection *drop=nullptr,*drop2=nullptr,*final=nullptr;
            for(const auto& s:song->getSections()) {
                if(s.name=="DROP")drop=&s;
                if(s.name=="DROP 2")drop2=&s;
                if(s.name=="FINAL HOOK")final=&s;
            }
            if(!drop||!drop2||!final||final->bars<4)return 2;
            if(hook(*song,*drop).empty()||hook(*song,*drop)!=hook(*song,*drop2)
               ||hook(*song,*drop)!=hook(*song,*final)) {
                std::cerr<<"Lost hook identity genre="<<genre<<" seed="<<seed<<'\n';return 3;
            }
            if(phrase(*song,*drop,final->bars)==phrase(*song,*final,final->bars)) {
                std::cerr<<"Final answer did not develop genre="<<genre<<" seed="<<seed<<'\n';return 4;
            }
            for(const auto& note:song->getLanes()[9].notes) {
                if(note.note<58||note.note>86)return 5;
                if(genre==1&&note.length>.231)return 6;
            }
            if(genre==1&&!song->getLanes()[8].notes.empty())return 7;
            melodies.insert(phrase(*song,*drop,4));
            // Degree/extension/rhythm data has no seed, timbre or arbitrary ID.
            harmonies.insert(song->getProgressionFingerprint());
            std::vector<int> actualHarmony;
            for(const auto& n:song->getLanes()[6].notes)
                if(n.beat>=drop->startBar*4.0&&n.beat<(drop->startBar+8)*4.0) {
                    actualHarmony.push_back(((n.note-song->getRootMidi())%12+12)%12);
                    actualHarmony.push_back((int)std::llround((n.beat-drop->startBar*4.0)*8));
                }
            renderedHarmony.insert(actualHarmony);
            std::vector<int> groove;
            for(int lane=0;lane<4;++lane) {
                groove.push_back(-100-lane);
                for(const auto& n:song->getLanes()[(size_t)lane].notes)
                    if(n.beat>=drop->startBar*4.0&&n.beat<(drop->startBar+4)*4.0) {
                        groove.push_back(n.note);
                        groove.push_back((int)std::llround((n.beat-drop->startBar*4.0)*32));
                    }
            }
            grooves.insert(groove);
        }
        std::cout<<"Genre "<<genre<<": "<<melodies.size()<<" melodic, "<<harmonies.size()
                 <<" harmonic plans, "<<renderedHarmony.size()<<" actual chord-note, "<<grooves.size()
                 <<" groove fingerprints / 16 production seeds\n";
        if(melodies.size()<14||harmonies.size()<(genre==1?4u:12u)
           ||renderedHarmony.size()<(genre==1?4u:12u)||grooves.size()<12)return 8;
    }
    return 0;
}
}
