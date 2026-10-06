#pragma once
#include "SonaraEngine.h"
#include "MixPolicy.h"
#include "../Generation/SongArrangement.h"
#include "../Song/DrumSynth.h"
#include <algorithm>
#include <vector>

namespace sonara {
struct SongMixState { float level=1.f,pan=0.f,width=1.f,fxSend=1.f; };
using SongMixArray=std::array<SongMixState,12>;

// One bounded renderer is used by the host, full WAV and isolated stems.
// prepare/configure/reset are control-thread operations; render never allocates.
class SongRenderEngine {
public:
    SongRenderEngine(){prepare(44100.0,512);}
    using MixState=SongMixState;
    bool hasActiveVoices() const noexcept
    {for(const auto& e:engines)if(e&&e->hasActiveVoices())return true;return false;}
    void prepare(double rate,int maximumBlock)
    {
        sampleRate=juce::jmax(8000.0,rate);blockSize=juce::jmax(1,maximumBlock);
        const float dt=1.f/(float)sampleRate;
        for(int i=0;i<8;++i){const float rc=1.f/(juce::MathConstants<float>::twoPi*mixpolicy::highPassHz(i));hpCoefficient[(size_t)i]=rc/(rc+dt);}
        subCoefficient=dt/(1.f/(juce::MathConstants<float>::twoPi*mixpolicy::subLowPassHz())+dt);
        duckReleaseCoefficient=std::exp(-1.f/(float)(sampleRate*.18));
        static constexpr int voices[8]={1,1,3,2,3,3,2,1};
        for(int i=0;i<8;++i)
        {
            if(!engines[(size_t)i])engines[(size_t)i]=std::make_unique<SonaraEngine>();
            auto& e=*engines[(size_t)i];e.setLowCpuMode(true);e.setVoiceLimit(voices[i]);e.prepare(sampleRate,blockSize,2);
            midis[(size_t)i].ensureSize(32768);chase[(size_t)i].ensureSize(32768);
            scratch[(size_t)i].setSize(2,blockSize);
        }
        for(auto& d:drums)d.prepare(sampleRate);
        block.setSize(2,blockSize);fxBus.setSize(2,blockSize);drumBus.setSize(2,blockSize);
        duckEnvelope.resize((size_t)blockSize);
        juce::Reverb::Parameters rp;rp.roomSize=.31f;rp.damping=.52f;rp.wetLevel=.22f;rp.dryLevel=0.f;rp.width=.82f;
        reverb.setParameters(rp);reverb.setSampleRate(sampleRate);reset(0);
    }
    void configure(const SongArrangement& a)
    {
        if(a.getLanes().size()!=12||a.getSections().empty())
        {for(auto& e:events)e.clear();for(auto& h:holds)h.clear();boundaries.clear();sectionMix.clear();reset(0);return;}
        const double spb=sampleRate*60.0/a.getBpm();
        for(size_t i=0;i<events.size();++i)
        {
            auto& list=events[i];list.clear();holds[i].clear();
            if(i>=a.getLanes().size())continue;
            const auto& l=a.getLanes()[i];list.reserve(l.notes.size()*2);holds[i].reserve(l.notes.size());
            for(const auto& n:l.notes)
            {
                const auto on=(int64_t)std::llround(n.beat*spb),off=(int64_t)std::llround((n.beat+n.length)*spb);
                list.push_back({on,n.note,n.velocity,true});
                if(i>=4){list.push_back({off,n.note,0,false});holds[i].push_back({on,off,n.note,n.velocity});}
            }
            std::stable_sort(list.begin(),list.end(),[](const Event& x,const Event& y){return x.sample<y.sample||(x.sample==y.sample&&!x.on&&y.on);});
            channels[i]=l.midiChannel;
            if(i>=4)engines[i-4]->setPatch(l.sound);
        }
        if(a.getLanes().size()>=4)for(size_t i=0;i<drums.size();++i)
        {drums[i].configureKit(a.getLanes()[0].sound,a.getLanes()[1].sound,a.getLanes()[2].sound,a.getLanes()[3].sound);drums[i].setNoiseSalt(i+1);}
        boundaries.clear();sectionMix.clear();
        for(const auto& s:a.getSections())
        {
            boundaries.push_back((int64_t)std::llround((s.startBar+s.bars)*4.0*spb));
            CachedSection cached;const float energy=juce::jlimit(0.f,1.f,s.energy),dt=1.f/(float)sampleRate;
            cached.toneCoefficient=dt/(1.f/(juce::MathConstants<float>::twoPi*mixpolicy::toneCutoffHz(energy))+dt);
            cached.drumGain=mixpolicy::drumGain(s.name,energy);
            for(int i=0;i<8;++i)
            {
                cached.gain[(size_t)i]=mixpolicy::baseGain(i)*mixpolicy::sectionGain(i,s.name)*mixpolicy::energyGain(i,energy);
                cached.send[(size_t)i]=mixpolicy::baseFxSend(i)*mixpolicy::energySpace(energy)*a.getLanes()[(size_t)i+4].sound.macroSpace;
                cached.width[(size_t)i]=mixpolicy::stereoWidth(i,1.f,energy);
            }
            sectionMix.push_back(cached);
        }
        reset(0);
    }
    void reset(int64_t start)
    {
        seekSample=start;duckState=0.f;reverb.reset();
        for(auto& e:engines)if(e)e->reset();
        for(auto& d:drums)d.reset();
        for(auto* states:{&hpX,&hpY,&lpState,&toneState})for(auto& state:*states)state.fill(0.f);
        masterX.fill(0.f);masterY.fill(0.f);
        for(size_t i=0;i<chase.size();++i)
        {
            chase[i].clear();
            for(const auto& h:holds[i+4])if(h.on<start&&h.off>start)
                chase[i].addEvent(juce::MidiMessage::noteOn(channels[i+4],h.note,(juce::uint8)h.velocity),0);
        }
    }
    void render(const SongArrangement& a,juce::AudioBuffer<float>& out,int64_t start,int samples,const SongMixArray* mix=nullptr,int isolatedLane=-1)
    {
        if(sectionMix.empty()){out.clear();return;}
        int offset=0;
        while(offset<samples)
        {
            const int64_t position=start+offset;
            int n=juce::jmin(blockSize,samples-offset);
            const auto end=std::upper_bound(boundaries.begin(),boundaries.end(),position);
            if(end!=boundaries.end())n=(int)std::min<int64_t>(n,*end-position);
            renderPart(a,position,n,mix,isolatedLane);
            for(int ch=0;ch<out.getNumChannels();++ch)
                if(out.getNumChannels()==1)
                {out.copyFrom(0,offset,block.getReadPointer(0),n,.5f);out.addFrom(0,offset,block,1,0,n,.5f);}
                else out.copyFrom(ch,offset,block,juce::jmin(ch,1),0,n);
            offset+=n;
        }
    }
private:
    struct Event { int64_t sample;int note,velocity;bool on; };
    struct Hold { int64_t on,off;int note,velocity; };
    struct CachedSection {float toneCoefficient=1.f,drumGain=.7f;std::array<float,8> gain{},send{},width{};};
    void injectMidi(int lane,juce::MidiBuffer& midi,int64_t start,int n)
    {
        midi.clear();const auto& list=events[(size_t)lane];
        for(auto it=std::lower_bound(list.begin(),list.end(),start,[](const Event& e,int64_t s){return e.sample<s;});it!=list.end()&&it->sample<start+n;++it)
            midi.addEvent(it->on?juce::MidiMessage::noteOn(channels[(size_t)lane],it->note,(juce::uint8)it->velocity):juce::MidiMessage::noteOff(channels[(size_t)lane],it->note),(int)(it->sample-start));
        auto& held=chase[(size_t)(lane-4)];if(!held.isEmpty()){midi.addEvents(held,0,n,0);held.clear();}
    }
    int collectTriggers(int lane,int64_t start,int n,DrumTrigger* dest,int capacity)
    {
        const auto& list=events[(size_t)lane];int count=0;
        for(auto it=std::lower_bound(list.begin(),list.end(),start,[](const Event& e,int64_t s){return e.sample<s;});it!=list.end()&&it->sample<start+n&&count<capacity;++it)
            dest[count++]={(int)(it->sample-start),it->note,it->velocity/127.f};
        return count;
    }
    void renderPart(const SongArrangement& a,int64_t startSample,int n,const SongMixArray* mix,int isolatedLane)
    {
        juce::ScopedNoDenormals noDenormals;
        constexpr int firstMusical=4,musicalCount=8;
        const auto& lanes=a.getLanes();
        block.clear();fxBus.clear();
        const auto sectionIndex=std::min(sectionMix.size()-1,(size_t)(std::upper_bound(boundaries.begin(),boundaries.end(),startSample)-boundaries.begin()));
        const auto& automation=sectionMix[sectionIndex];

        int count=mix&&(*mix)[0].level<=.0001f?0:collectTriggers(0,startSample,n,triggers.data(),(int)triggers.size());
        const float duckRelease=duckReleaseCoefficient;
        float duck=duckState;int triggerIndex=0;
        for(int s=0;s<n;++s)
        {
            while(triggerIndex<count&&triggers[(size_t)triggerIndex].sampleOffset<=s)
            {
                const auto& trigger=triggers[(size_t)triggerIndex];
                if(trigger.midiNote==36)duck=juce::jmax(duck,.65f+.35f*trigger.velocity);
                ++triggerIndex;
            }
            duckEnvelope[(size_t)s]=duck;
            duck*=duckRelease;
        }
        duckState=duck;

        for(int i=0;i<musicalCount;++i)
        {
            auto& midi=midis[(size_t)i];
            auto& s=scratch[(size_t)i];
            injectMidi(firstMusical+i,midi,startSample,n);
            if(isolatedLane>=0&&isolatedLane!=firstMusical+i)continue;

            s.clear();
            engines[(size_t)i]->render(s,midi,n);

            const float hpA=hpCoefficient[(size_t)i];
            for(int ch=0;ch<2;++ch)
            {
                auto* d=s.getWritePointer(ch);
                float x1=hpX[(size_t)i][(size_t)ch],y1=hpY[(size_t)i][(size_t)ch];
                for(int smp=0;smp<n;++smp)
                {
                    const float x=std::isfinite(d[smp])?d[smp]:0.f;
                    const float y=hpA*(y1+x-x1);
                    x1=x;y1=y;d[smp]=y;
                }
                hpX[(size_t)i][(size_t)ch]=x1;hpY[(size_t)i][(size_t)ch]=y1;
            }

            if(i==0)
            {
                auto* l=s.getWritePointer(0);auto* r=s.getWritePointer(1);
                for(int smp=0;smp<n;++smp){const float mid=.5f*(l[smp]+r[smp]);const float side=mixpolicy::bassStereoFraction();l[smp]=mid*(1.f-side)+l[smp]*side;r[smp]=mid*(1.f-side)+r[smp]*side;}
            }
            else if(i==1)
            {
                const float lpA=subCoefficient;
                auto* l=s.getWritePointer(0);auto* r=s.getWritePointer(1);
                float state=lpState[(size_t)i][0];
                for(int smp=0;smp<n;++smp){const float mono=.5f*(l[smp]+r[smp]);state+=lpA*(mono-state);l[smp]=state;r[smp]=state;}
                lpState[(size_t)i][0]=state;lpState[(size_t)i][1]=state;
            }

            if(i>=2)
            {
                const float toneA=automation.toneCoefficient;
                for(int ch=0;ch<2;++ch)
                {
                    auto* d=s.getWritePointer(ch);
                    float state=toneState[(size_t)i][(size_t)ch];
                    for(int smp=0;smp<n;++smp){state+=toneA*(d[smp]-state);d[smp]=state;}
                    toneState[(size_t)i][(size_t)ch]=state;
                }
            }

            MixState mixState;
            const int globalLane=firstMusical+i;
            if(mix!=nullptr&&juce::isPositiveAndBelow(globalLane,(int)mix->size()))
                mixState=(*mix)[(size_t)globalLane];

            if(s.getNumChannels()>=2)
            {
                auto* l=s.getWritePointer(0);auto* r=s.getWritePointer(1);
                const float requestedWidth=juce::jlimit(0.f,1.5f,mixState.width);
                const float width=i==1?0.f:(i==0?juce::jmin(.18f,requestedWidth):requestedWidth*automation.width[(size_t)i]);
                const float pan=i==1?0.f:juce::jlimit(-1.f,1.f,mixState.pan);
                const float panL=pan>0.f?1.f-pan:1.f;
                const float panR=pan<0.f?1.f+pan:1.f;
                for(int smp=0;smp<n;++smp)
                {
                    const float mid=.5f*(l[smp]+r[smp]);
                    const float side=.5f*(l[smp]-r[smp])*width;
                    l[smp]=(mid+side)*panL;
                    r[smp]=(mid-side)*panR;
                }
            }

            const float duckDepth=mixpolicy::duckDepth(i);
            if(duckDepth>0.f)
            {
                for(int ch=0;ch<s.getNumChannels();++ch)
                {
                    auto* d=s.getWritePointer(ch);
                    for(int smp=0;smp<n;++smp)
                        d[smp]*=1.f-duckDepth*duckEnvelope[(size_t)smp];
                }
            }

            const float gain=automation.gain[(size_t)i]*juce::jlimit(0.f,1.5f,mixState.level);
            const float send=automation.send[(size_t)i]*juce::jlimit(0.f,1.5f,mixState.fxSend);
            for(int ch=0;ch<2;++ch)
            {
                block.addFrom(ch,0,s,ch,0,n,gain);
                if(send>0.f)fxBus.addFrom(ch,0,s,ch,0,n,gain*send);
            }
        }

        for(int lane=0;lane<4;++lane)
        {
            if(isolatedLane>=0&&isolatedLane!=lane)continue;
            drumBus.clear();
            const int hits=collectTriggers(lane,startSample,n,triggers.data(),(int)triggers.size());
            juce::AudioBuffer<float> drumView(drumBus.getArrayOfWritePointers(),2,0,n);
            drums[(size_t)lane].render(drumView,triggers.data(),hits);
            const auto m=mix?(*mix)[(size_t)lane]:MixState{};
            const float gain=automation.drumGain*juce::jlimit(0.f,1.5f,m.level);
            if(lane>0&&m.fxSend>0.f)
                for(int ch=0;ch<2;++ch)fxBus.addFrom(ch,0,drumBus,ch,0,n,gain*.04f*lanes[(size_t)lane].sound.macroSpace*m.fxSend);
            const float pan=juce::jlimit(-1.f,1.f,m.pan);
            for(int ch=0;ch<2;++ch)
                block.addFrom(ch,0,drumBus,ch,0,n,gain*(ch==0?(pan>0?1.f-pan:1.f):(pan<0?1.f+pan:1.f)));
        }

        reverb.processStereo(fxBus.getWritePointer(0),fxBus.getWritePointer(1),n);
        for(int ch=0;ch<2;++ch)block.addFrom(ch,0,fxBus,ch,0,n,.66f);

        for(int ch=0;ch<2;++ch)
        {
            auto* d=block.getWritePointer(ch);
            float x1=masterX[(size_t)ch],y1=masterY[(size_t)ch];
            for(int smp=0;smp<n;++smp)
                d[smp]=mixpolicy::processMasterSample(d[smp],x1,y1,sampleRate)*juce::jlimit(0.f,1.f,(float)(startSample+smp-seekSample)/128.f);
            masterX[(size_t)ch]=x1;masterY[(size_t)ch]=y1;
        }

    }
    double sampleRate=44100.0;int blockSize=512;int64_t seekSample=0;
    std::array<std::unique_ptr<SonaraEngine>,8> engines;
    std::array<DrumSynth,4> drums;
    std::array<juce::MidiBuffer,8> midis,chase;
    std::array<juce::AudioBuffer<float>,8> scratch;
    std::array<std::array<float,2>,8> hpX{},hpY{},lpState{},toneState{};
    juce::AudioBuffer<float> block,fxBus,drumBus;
    juce::Reverb reverb;
    std::array<float,2> masterX{},masterY{};
    std::array<DrumTrigger,256> triggers{};
    std::vector<float> duckEnvelope;float duckState=0.f;
    std::array<std::vector<Event>,12> events;
    std::array<std::vector<Hold>,12> holds;
    std::array<int,12> channels{};
    std::vector<int64_t> boundaries;
    std::vector<CachedSection> sectionMix;
    std::array<float,8> hpCoefficient{};float subCoefficient=1.f,duckReleaseCoefficient=1.f;
};
} // namespace sonara
