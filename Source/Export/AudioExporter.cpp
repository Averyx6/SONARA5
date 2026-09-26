#include "AudioExporter.h"
#include <algorithm>
#include <cmath>

namespace sonara {

bool AudioExporter::createWavWriter(const juce::File& file, double sampleRate, std::unique_ptr<juce::AudioFormatWriter>& writer)
{
    file.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream>(file);
    if(!stream->openedOk()) return false;
    juce::WavAudioFormat wav;
    auto* raw = stream.release();
    writer.reset(wav.createWriterFor(raw, sampleRate, juce::AudioChannelSet::stereo(), 24, {}, 0));
    if(!writer){delete raw;return false;}
    return true;
}

void AudioExporter::injectLaneMidi(const ArrangementLane& lane, juce::MidiBuffer& midi, int64_t startSample, int numSamples, double bpm, double sampleRate)
{
    midi.clear();const double spb=sampleRate*60.0/bpm;const double startBeat=(double)startSample/spb,endBeat=(double)(startSample+numSamples)/spb;
    auto it=std::lower_bound(lane.notes.begin(),lane.notes.end(),startBeat-5.0,[](const ArrangementNote& n,double beat){return n.beat<beat;});
    for(;it!=lane.notes.end()&&it->beat<=endBeat;++it){const double onS=it->beat*spb,offS=(it->beat+it->length)*spb;if(onS>=startSample&&onS<startSample+numSamples)midi.addEvent(juce::MidiMessage::noteOn(lane.midiChannel,it->note,(juce::uint8)it->velocity),(int)(onS-startSample));if(offS>=startSample&&offS<startSample+numSamples)midi.addEvent(juce::MidiMessage::noteOff(lane.midiChannel,it->note),(int)(offS-startSample));}
}

int AudioExporter::collectDrumTriggers(const ArrangementLane& lane,int64_t startSample,int numSamples,double bpm,double sampleRate,DrumTrigger* out,int capacity)
{
    if(out==nullptr||capacity<=0)return 0;const double spb=sampleRate*60.0/bpm;const double startBeat=(double)startSample/spb,endBeat=(double)(startSample+numSamples)/spb;int count=0;
    auto it=std::lower_bound(lane.notes.begin(),lane.notes.end(),startBeat,[](const ArrangementNote& n,double beat){return n.beat<beat;});
    for(;it!=lane.notes.end()&&it->beat<endBeat&&count<capacity;++it){const int offset=juce::jlimit(0,numSamples-1,(int)std::llround(it->beat*spb-startSample));out[count++]={offset,it->note,it->velocity/127.f};}
    return count;
}

juce::String AudioExporter::safeFileName(const juce::String& s)
{
    return s.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_ ").trim().replaceCharacter(' ','_');
}

bool AudioExporter::renderSelectedLane(const SongArrangement& a,int laneIndex,const juce::File& destination,double sampleRate,Progress cb) const
{
    const auto& lanes=a.getLanes();if(!juce::isPositiveAndBelow(laneIndex,(int)lanes.size()))return false;const auto& lane=lanes[(size_t)laneIndex];
    std::unique_ptr<juce::AudioFormatWriter> writer;if(!createWavWriter(destination,sampleRate,writer))return false;
    constexpr int blockSize=512;const double spb=sampleRate*60.0/a.getBpm();const int64_t total=(int64_t)std::llround(a.getTotalBeats()*spb+sampleRate*3.0);
    juce::AudioBuffer<float> block(2,blockSize);juce::MidiBuffer midi;midi.ensureSize(8192);std::array<DrumTrigger,128> triggers{};
    SonaraEngine synth;DrumSynth drums;if(lane.drums){drums.prepare(sampleRate);drums.configureKit(lane.sound,lane.sound,lane.sound,lane.sound);}else{synth.prepare(sampleRate,blockSize,2);synth.setPatch(lane.sound);}
    for(int64_t start=0;start<total;start+=blockSize)
    {
        const int n=(int)juce::jmin<int64_t>(blockSize,total-start);
        block.clear();
        juce::AudioBuffer<float> view(block.getArrayOfWritePointers(),block.getNumChannels(),0,n);
        if(lane.drums)
        {
            const int count=collectDrumTriggers(lane,start,n,a.getBpm(),sampleRate,triggers.data(),(int)triggers.size());
            drums.render(view,triggers.data(),count);
        }
        else
        {
            injectLaneMidi(lane,midi,start,n,a.getBpm(),sampleRate);
            synth.render(view,midi);
        }
        for(int ch=0;ch<2;++ch)for(int i=0;i<n;++i)
            block.setSample(ch,i,juce::jlimit(-.96f,.96f,std::tanh(block.getSample(ch,i)*.86f)));
        if(!writer->writeFromAudioSampleBuffer(block,0,n))return false;
        if(cb&&start%(blockSize*64)==0)cb((float)start/(float)total,"Rendering "+lane.name);
    }
    if(cb)cb(1.f,lane.name+" ready");return true;
}

bool AudioExporter::renderFullMix(const SongArrangement& a,const juce::File& destination,double sampleRate,Progress cb,const MixArray* mix) const
{
    std::unique_ptr<juce::AudioFormatWriter> writer;
    if(!createWavWriter(destination,sampleRate,writer))return false;

    constexpr int blockSize=512;
    constexpr int firstMusical=4;
    constexpr int expectedMusical=8;
    const auto& lanes=a.getLanes();
    const int musicalCount=juce::jmin(expectedMusical,juce::jmax(0,(int)lanes.size()-firstMusical));
    if(musicalCount<=0)return false;

    std::vector<std::unique_ptr<SonaraEngine>> engines;
    std::vector<juce::MidiBuffer> midis((size_t)musicalCount);
    std::vector<juce::AudioBuffer<float>> scratch((size_t)musicalCount);
    std::vector<std::array<float,2>> hpX((size_t)musicalCount),hpY((size_t)musicalCount),lpState((size_t)musicalCount);
    engines.reserve((size_t)musicalCount);

    static constexpr int voiceBudget[expectedMusical]={2,1,4,2,4,4,2,1};
    static constexpr float laneGain[expectedMusical]={.52f,.34f,.30f,.27f,.22f,.50f,.23f,.16f};
    static constexpr float hpHz[expectedMusical]={28.f,18.f,120.f,125.f,160.f,120.f,150.f,110.f};
    static constexpr float fxSend[expectedMusical]={0.f,0.f,.14f,.10f,.18f,.12f,.08f,.15f};

    for(int i=0;i<musicalCount;++i)
    {
        auto e=std::make_unique<SonaraEngine>();
        e->setLowCpuMode(true);
        e->setVoiceLimit(voiceBudget[i]);
        e->prepare(sampleRate,blockSize,2);
        e->setPatch(lanes[(size_t)(firstMusical+i)].sound);
        midis[(size_t)i].ensureSize(16384);
        scratch[(size_t)i].setSize(2,blockSize);
        hpX[(size_t)i].fill(0.f);hpY[(size_t)i].fill(0.f);lpState[(size_t)i].fill(0.f);
        engines.push_back(std::move(e));
    }

    DrumSynth drums;
    drums.prepare(sampleRate);
    if(lanes.size()>=4)drums.configureKit(lanes[0].sound,lanes[1].sound,lanes[2].sound,lanes[3].sound);

    juce::Reverb reverb;
    juce::Reverb::Parameters rp;
    rp.roomSize=.31f;rp.damping=.52f;rp.wetLevel=.22f;rp.dryLevel=0.f;rp.width=.82f;
    reverb.setParameters(rp);

    std::array<DrumTrigger,256> triggers{};
    juce::AudioBuffer<float> block(2,blockSize),fxBus(2,blockSize);
    std::array<float,2> masterX{},masterY{};

    const double spb=sampleRate*60.0/a.getBpm();
    const int64_t total=(int64_t)std::llround(a.getTotalBeats()*spb+sampleRate*4.0);

    for(int64_t startSample=0;startSample<total;startSample+=blockSize)
    {
        const int n=(int)juce::jmin<int64_t>(blockSize,total-startSample);
        block.clear();fxBus.clear();

        for(int i=0;i<musicalCount;++i)
        {
            const auto& lane=lanes[(size_t)(firstMusical+i)];
            auto& midi=midis[(size_t)i];
            auto& s=scratch[(size_t)i];
            injectLaneMidi(lane,midi,startSample,n,a.getBpm(),sampleRate);
            const bool active=engines[(size_t)i]->hasActiveVoices();
            if(midi.isEmpty()&&!active)continue;

            s.clear();
            juce::AudioBuffer<float> view(s.getArrayOfWritePointers(),2,0,n);
            engines[(size_t)i]->render(view,midi);

            const float rc=1.f/(juce::MathConstants<float>::twoPi*hpHz[i]);
            const float dt=1.f/(float)sampleRate;
            const float hpA=rc/(rc+dt);
            for(int ch=0;ch<2;++ch)
            {
                auto* d=view.getWritePointer(ch);
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
                auto* l=view.getWritePointer(0);auto* r=view.getWritePointer(1);
                for(int smp=0;smp<n;++smp){const float mid=.5f*(l[smp]+r[smp]);l[smp]=mid*.88f+l[smp]*.12f;r[smp]=mid*.88f+r[smp]*.12f;}
            }
            else if(i==1)
            {
                const float lpRc=1.f/(juce::MathConstants<float>::twoPi*125.f);
                const float lpA=dt/(lpRc+dt);
                auto* l=view.getWritePointer(0);auto* r=view.getWritePointer(1);
                float state=lpState[(size_t)i][0];
                for(int smp=0;smp<n;++smp){const float mono=.5f*(l[smp]+r[smp]);state+=lpA*(mono-state);l[smp]=state;r[smp]=state;}
                lpState[(size_t)i][0]=state;lpState[(size_t)i][1]=state;
            }

            MixState mixState;
            const int globalLane=firstMusical+i;
            if(mix!=nullptr&&juce::isPositiveAndBelow(globalLane,(int)mix->size()))
                mixState=(*mix)[(size_t)globalLane];

            if(view.getNumChannels()>=2)
            {
                auto* l=view.getWritePointer(0);auto* r=view.getWritePointer(1);
                const float requestedWidth=juce::jlimit(0.f,1.5f,mixState.width);
                const float width=i==1?0.f:(i==0?juce::jmin(.25f,requestedWidth):requestedWidth);
                const float pan=juce::jlimit(-1.f,1.f,mixState.pan);
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

            const float gain=laneGain[i]*juce::jlimit(0.f,1.5f,mixState.level);
            const float send=fxSend[i]*juce::jlimit(0.f,1.5f,mixState.fxSend);
            for(int ch=0;ch<2;++ch)
            {
                block.addFrom(ch,0,view,ch,0,n,gain);
                if(send>0.f)fxBus.addFrom(ch,0,view,ch,0,n,gain*send);
            }
        }

        int count=0;
        for(int i=0;i<juce::jmin(4,(int)lanes.size())&&count<(int)triggers.size();++i)
            count+=collectDrumTriggers(lanes[(size_t)i],startSample,n,a.getBpm(),sampleRate,
                                      triggers.data()+count,(int)triggers.size()-count);
        std::sort(triggers.begin(),triggers.begin()+count,
                  [](const DrumTrigger&x,const DrumTrigger&y){return x.sampleOffset<y.sampleOffset;});
        juce::AudioBuffer<float> blockView(block.getArrayOfWritePointers(),2,0,n);
        drums.render(blockView,triggers.data(),count);

        reverb.processStereo(fxBus.getWritePointer(0),fxBus.getWritePointer(1),n);
        for(int ch=0;ch<2;++ch)block.addFrom(ch,0,fxBus,ch,0,n,.72f);

        const float masterRc=1.f/(juce::MathConstants<float>::twoPi*24.f);
        const float masterDt=1.f/(float)sampleRate;
        const float masterA=masterRc/(masterRc+masterDt);
        for(int ch=0;ch<2;++ch)
        {
            auto* d=block.getWritePointer(ch);
            float x1=masterX[(size_t)ch],y1=masterY[(size_t)ch];
            for(int smp=0;smp<n;++smp)
            {
                const float x=std::isfinite(d[smp])?d[smp]:0.f;
                const float hp=masterA*(y1+x-x1);
                x1=x;y1=hp;
                d[smp]=juce::jlimit(-.92f,.92f,std::tanh(hp*.67f));
            }
            masterX[(size_t)ch]=x1;masterY[(size_t)ch]=y1;
        }

        if(!writer->writeFromAudioSampleBuffer(block,0,n))return false;
        if(cb&&startSample%(blockSize*64)==0)
            cb((float)startSample/(float)total,"Rendering full mix");
    }

    if(cb)cb(1.f,"Full mix ready");
    return true;
}

bool AudioExporter::renderAllStems(const SongArrangement& a,const juce::File& directory,double sampleRate,Progress cb) const
{
    if(!directory.exists()&&!directory.createDirectory())return false;const auto& lanes=a.getLanes();if(lanes.empty())return false;
    for(size_t i=0;i<lanes.size();++i){if(cb)cb((float)i/(float)lanes.size(),"Stem "+lanes[i].name);const auto f=directory.getChildFile(juce::String((int)i+1).paddedLeft('0',2)+"_"+safeFileName(lanes[i].name)+".wav");if(!renderSelectedLane(a,(int)i,f,sampleRate,{}))return false;}
    if(cb)cb(1.f,"All stems ready");return true;
}

} // namespace sonara