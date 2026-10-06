#include "AudioExporter.h"
#include "../Engine/MixPolicy.h"
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

bool AudioExporter::renderSelectedLane(const SongArrangement& a,int lane,const juce::File& f,double sr,Progress cb,const MixArray* mix) const
{
    if(!juce::isPositiveAndBelow(lane,(int)a.getLanes().size()))return false;
    return renderSong(a,f,sr,cb,mix,lane);
}

SongArrangement AudioExporter::makeReferenceSong(const ReferenceAnalysis& reference,const SoundDNA& patch)
{
    juce::ValueTree root("SONARA_ARRANGEMENT");
    root.setProperty("bpm",juce::jlimit(60.0,200.0,reference.estimatedBpm),nullptr);
    const int bars=juce::jlimit(1,512,(int)std::ceil(reference.melodyBeats()/4.0));root.setProperty("bars",bars,nullptr);
    juce::ValueTree sections("SECTIONS"),section("SECTION");
    section.setProperty("name","REFERENCE",nullptr);section.setProperty("startBar",0,nullptr);section.setProperty("bars",bars,nullptr);section.setProperty("energy",.8f,nullptr);sections.addChild(section,-1,nullptr);root.addChild(sections,-1,nullptr);
    juce::ValueTree lanes("LANES");
    static const char* names[12]={"KICK","SNARE / CLAP","HATS","PERCUSSION","BASS","SUB","CHORDS","PLUCK","PAD","LEAD","COUNTER","FX / TRANSITIONS"};
    for(int i=0;i<12;++i){juce::ValueTree lane("LANE");lane.setProperty("name",names[i],nullptr);lane.setProperty("channel",i==9?1:(i<4?10:i-2),nullptr);lane.setProperty("drums",i<4,nullptr);lane.addChild((i==9?patch:SoundDNA{}).toValueTree(),-1,nullptr);lanes.addChild(lane,-1,nullptr);}root.addChild(lanes,-1,nullptr);
    auto song=SongArrangement::fromValueTree(root);
    for(const auto& n:reference.melody)song.editLanes()[9].notes.push_back({juce::jlimit(0,127,n.midiNote),juce::jlimit(1,127,n.velocity),juce::jmax(0.0,n.beat),juce::jmax(.0625,n.length)});
    return SongArrangement::fromValueTree(song.toValueTree());
}
bool AudioExporter::renderReferenceMelody(const ReferenceAnalysis& reference,const SoundDNA& patch,const juce::File& destination,double rate,Progress cb) const
{
    if(!reference.valid()||reference.melody.empty())return false;
    const auto song=makeReferenceSong(reference,patch);
    return renderSong(song,destination,rate,cb,nullptr,-1);
}

bool AudioExporter::renderFullMix(const SongArrangement& a,const juce::File& f,double sr,Progress cb,const MixArray* mix) const
{return renderSong(a,f,sr,cb,mix,-1);}

bool AudioExporter::renderSong(const SongArrangement& a,const juce::File& file,double sr,Progress cb,const MixArray* mix,int lane) const
{
    if(a.getLanes().size()!=12||!std::isfinite(sr)||sr<8000||sr>192000)return false;
    std::unique_ptr<juce::AudioFormatWriter> writer;if(!createWavWriter(file,sr,writer))return false;
    constexpr int blockSize=512;
    SongRenderEngine renderer;renderer.prepare(sr,blockSize);renderer.configure(a);
    juce::AudioBuffer<float> block(2,blockSize);
    const auto total=(int64_t)std::llround(a.getTotalBeats()*sr*60.0/a.getBpm()+sr*4.0);
    for(int64_t start=0;start<total;start+=blockSize)
    {
        if(cancel&&cancel->load()){writer.reset();file.deleteFile();return false;}
        const int n=(int)std::min<int64_t>(blockSize,total-start);
        renderer.render(a,block,start,n,mix,lane);
        if(!writer->writeFromAudioSampleBuffer(block,0,n)){writer.reset();file.deleteFile();return false;}
        if(cb&&start%(blockSize*64)==0)cb((float)start/(float)total,lane<0?"Rendering full mix":"Rendering "+a.getLanes()[(size_t)lane].name);
    }
    if(cb)cb(1.f,lane<0?"Full mix ready":"Lane ready");
    return true;
}

bool AudioExporter::renderAllStems(const SongArrangement& a,const juce::File& directory,double sampleRate,Progress cb,const MixArray* mix) const
{
    if(!directory.exists()&&!directory.createDirectory())return false;const auto& lanes=a.getLanes();if(lanes.empty())return false;
    for(size_t i=0;i<lanes.size();++i){if(cb)cb((float)i/(float)lanes.size(),"Stem "+lanes[i].name);const auto f=directory.getChildFile(juce::String((int)i+1).paddedLeft('0',2)+"_"+safeFileName(lanes[i].name)+".wav");if(!renderSelectedLane(a,(int)i,f,sampleRate,{},mix))return false;}
    if(cb)cb(1.f,"All stems ready");return true;
}

} // namespace sonara
