#pragma once
#include "../Source/PluginProcessor.h"
#include <cmath>
#include <iostream>

namespace sonara::benchmark {
struct Metrics {
    double energy=0.0; float peak=0.f; int64_t count=0;
    void add(const juce::AudioBuffer<float>& b) {
        for(int ch=0;ch<b.getNumChannels();++ch)for(int i=0;i<b.getNumSamples();++i) {
            const float x=b.getSample(ch,i);energy+=(double)x*x;peak=std::max(peak,std::abs(x));++count;
        }
    }
    double rms() const {return count?std::sqrt(energy/(double)count):0.0;}
    double db() const {return 20.0*std::log10(std::max(1e-12,rms()));}
};

// Reproducible production arrangements and actual rendered audio, rather than
// patch/seed IDs as a proxy for audible variety. Optional files are review evidence.
inline int run(const juce::File& output,bool enforceBalance=false) {
    if(!output.createDirectory())return 1;
    juce::String csv="prompt,seed,group,rms_dbfs,peak,notes\n";
    static const char* prompts[]={
        "progressive house 128 BPM F minor 64 bars memorable emotional hook powerful melodic drop",
        "tech house 126 BPM A minor 64 bars sparse rhythmic hook tight bass no pads",
        "trance 138 BPM D minor 64 bars uplifting memorable melody developed final drop"};
    for(int genre=0;genre<3;++genre)for(uint64_t seed:{0x510001ULL,0x510002ULL,0x510003ULL}) {
        SonaraAudioProcessor processor;processor.prepareToPlay(44100.0,512);
        if(!processor.generateTrackWithSeed(prompts[genre],seed))return 2;
        const auto song=processor.arrangementSnapshot();
        const ArrangementSection* drop=nullptr;
        for(const auto& section:song->getSections())if(section.name=="DROP"){drop=&section;break;}
        if(!drop)return 3;
        const int64_t start=(int64_t)std::llround(drop->startBar*4.0*44100.0*60.0/song->getBpm());
        const int samples=(int)std::llround(std::min(4,drop->bars)*4.0*44100.0*60.0/song->getBpm());
        const auto label=juce::String(genre)+"-"+juce::String::toHexString((juce::int64)seed);
        std::array<double,4> levels{};
        for(int group=0;group<4;++group) {
            SongMixArray mix{};int notes=0;
            for(int lane=0;lane<12;++lane) {
                const bool active=group==0||(group==1&&lane<4)||(group==2&&lane>=6&&lane<=10)||(group==3&&lane==9);
                mix[(size_t)lane].level=active?1.f:0.f;
                if(active)for(const auto& n:song->getLanes()[(size_t)lane].notes)
                    if(n.beat>=drop->startBar*4.0&&n.beat<(drop->startBar+std::min(4,drop->bars))*4.0)++notes;
            }
            SongRenderEngine renderer;renderer.prepare(44100.0,512);renderer.configure(*song);renderer.reset(start);
            juce::AudioBuffer<float> audio(2,512);Metrics metrics;
            std::unique_ptr<juce::AudioFormatWriter> writer;
            if(group==0||group==3) {
                const auto file=output.getChildFile(label+(group==0?"-mix.wav":"-lead.wav"));
                if(file.existsAsFile()&&!file.deleteFile())return 4;
                auto stream=std::make_unique<juce::FileOutputStream>(file);
                if(!stream->openedOk())return 4;
                juce::WavAudioFormat wav;auto* raw=stream.release();
                writer.reset(wav.createWriterFor(raw,44100.0,2,24,{},0));
                if(!writer){delete raw;return 5;}
            }
            for(int position=0;position<samples;position+=512) {
                const int n=std::min(512,samples-position);audio.setSize(2,n,false,false,true);
                renderer.render(*song,audio,start+position,n,&mix);metrics.add(audio);
                if(writer&&!writer->writeFromAudioSampleBuffer(audio,0,n))return 6;
            }
            csv+=juce::String(genre)+","+juce::String::toHexString((juce::int64)seed)+","+juce::String(group)+","+juce::String(metrics.db(),5)+","+juce::String(metrics.peak,7)+","+juce::String(notes)+"\n";
            levels[(size_t)group]=metrics.db();
            std::cout<<label<<" group="<<group<<" RMS dBFS="<<metrics.db()<<" peak="<<metrics.peak<<" notes="<<notes<<'\n';
            if(!std::isfinite(metrics.rms())||metrics.peak>.951f||metrics.rms()<1e-8)return 7;
        }
        if(enforceBalance&&(levels[0]<-15.0||levels[2]<-20.0||levels[3]<-24.0||levels[2]-levels[1]<-6.0)) {
            std::cerr<<"Production drop is too quiet or drum-dominated: "<<label<<"\n";return 10;
        }
        if(!output.getChildFile(label+"-arrangement.xml").replaceWithText(song->toValueTree().createXml()->toString()))return 8;
    }
    return output.getChildFile("metrics.csv").replaceWithText(csv)?0:9;
}
}
