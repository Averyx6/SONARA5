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
    for(;it!=lane.notes.end()&&it->beat<=endBeat&&count<capacity;++it){const int offset=juce::jlimit(0,numSamples-1,(int)std::llround(it->beat*spb-startSample));out[count++]={offset,it->note,it->velocity/127.f};}
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
    SonaraEngine synth;DrumSynth drums;if(lane.drums)drums.prepare(sampleRate);else{synth.prepare(sampleRate,blockSize,2);synth.setPatch(lane.sound);}
    for(int64_t start=0;start<total;start+=blockSize){const int n=(int)juce::jmin<int64_t>(blockSize,total-start);block.clear();if(lane.drums){const int count=collectDrumTriggers(lane,start,n,a.getBpm(),sampleRate,triggers.data(),(int)triggers.size());drums.render(block,triggers.data(),count);}else{injectLaneMidi(lane,midi,start,n,a.getBpm(),sampleRate);synth.render(block,midi);}for(int ch=0;ch<2;++ch)for(int i=0;i<n;++i)block.setSample(ch,i,std::tanh(block.getSample(ch,i)*.86f));if(!writer->writeFromAudioSampleBuffer(block,0,n))return false;if(cb&&start%(blockSize*64)==0)cb((float)start/(float)total,"Rendering "+lane.name);}
    if(cb)cb(1.f,lane.name+" ready");return true;
}

bool AudioExporter::renderFullMix(const SongArrangement& a,const juce::File& destination,double sampleRate,Progress cb) const
{
    std::unique_ptr<juce::AudioFormatWriter> writer;if(!createWavWriter(destination,sampleRate,writer))return false;
    constexpr int blockSize=512;const auto& lanes=a.getLanes();const int firstMusical=4;const int musicalCount=juce::jmax(0,(int)lanes.size()-firstMusical);
    std::vector<std::unique_ptr<SonaraEngine>> engines;engines.reserve((size_t)musicalCount);std::vector<juce::MidiBuffer> midis((size_t)musicalCount);std::vector<juce::AudioBuffer<float>> scratch((size_t)musicalCount);
    for(int i=0;i<musicalCount;++i){auto e=std::make_unique<SonaraEngine>();e->prepare(sampleRate,blockSize,2);e->setPatch(lanes[(size_t)(firstMusical+i)].sound);midis[(size_t)i].ensureSize(8192);scratch[(size_t)i].setSize(2,blockSize);engines.push_back(std::move(e));}
    DrumSynth drums;drums.prepare(sampleRate);std::array<DrumTrigger,256> triggers{};juce::AudioBuffer<float> block(2,blockSize);
    const double spb=sampleRate*60.0/a.getBpm();const int64_t total=(int64_t)std::llround(a.getTotalBeats()*spb+sampleRate*4.0);
    for(int64_t start=0;start<total;start+=blockSize){const int n=(int)juce::jmin<int64_t>(blockSize,total-start);block.clear();
        for(int i=0;i<musicalCount;++i){auto& lane=lanes[(size_t)(firstMusical+i)];auto& m=midis[(size_t)i];auto& s=scratch[(size_t)i];injectLaneMidi(lane,m,start,n,a.getBpm(),sampleRate);s.clear();engines[(size_t)i]->render(s,m);const float gain=i==0?.72f:(i==4?.78f:.54f);for(int ch=0;ch<2;++ch)block.addFrom(ch,0,s,ch,0,n,gain);}
        int count=0;for(int i=0;i<juce::jmin(4,(int)lanes.size())&&count<(int)triggers.size();++i)count+=collectDrumTriggers(lanes[(size_t)i],start,n,a.getBpm(),sampleRate,triggers.data()+count,(int)triggers.size()-count);std::sort(triggers.begin(),triggers.begin()+count,[](const DrumTrigger&x,const DrumTrigger&y){return x.sampleOffset<y.sampleOffset;});drums.render(block,triggers.data(),count);
        for(int ch=0;ch<2;++ch)for(int i=0;i<n;++i)block.setSample(ch,i,std::tanh(block.getSample(ch,i)*.72f));if(!writer->writeFromAudioSampleBuffer(block,0,n))return false;if(cb&&start%(blockSize*64)==0)cb((float)start/(float)total,"Rendering full mix");}
    if(cb)cb(1.f,"Full mix ready");return true;
}

bool AudioExporter::renderAllStems(const SongArrangement& a,const juce::File& directory,double sampleRate,Progress cb) const
{
    if(!directory.exists()&&!directory.createDirectory())return false;const auto& lanes=a.getLanes();if(lanes.empty())return false;
    for(size_t i=0;i<lanes.size();++i){if(cb)cb((float)i/(float)lanes.size(),"Stem "+lanes[i].name);const auto f=directory.getChildFile(juce::String((int)i+1).paddedLeft('0',2)+"_"+safeFileName(lanes[i].name)+".wav");if(!renderSelectedLane(a,(int)i,f,sampleRate,{}))return false;}
    if(cb)cb(1.f,"All stems ready");return true;
}

} // namespace sonara