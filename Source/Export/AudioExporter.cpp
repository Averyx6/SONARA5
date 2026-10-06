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
    if(!a.validate()||!std::isfinite(sr)||sr<8000||sr>192000)return false;
    juce::TemporaryFile temporary(file);
    std::unique_ptr<juce::AudioFormatWriter> writer;if(!createWavWriter(temporary.getFile(),sr,writer))return false;
    constexpr int blockSize=512;
    SongRenderEngine renderer;renderer.prepare(sr,blockSize);renderer.configure(a);
    juce::AudioBuffer<float> block(2,blockSize);
    const auto total=(int64_t)std::llround(a.getTotalBeats()*sr*60.0/a.getBpm()+sr*4.0);
    for(int64_t start=0;start<total;start+=blockSize)
    {
        if(cancel&&cancel->load()){writer.reset();return false;}
        const int n=(int)std::min<int64_t>(blockSize,total-start);
        renderer.render(a,block,start,n,mix,lane);
        if(!writer->writeFromAudioSampleBuffer(block,0,n)){writer.reset();return false;}
        if(cb&&start%(blockSize*64)==0)cb((float)start/(float)total,lane<0?"Rendering full mix":"Rendering "+a.getLanes()[(size_t)lane].name);
    }
    writer.reset();
    if(cancel&&cancel->load())return false;
    if(!temporary.overwriteTargetFileWithTemporary())return false;
    if(cb)cb(1.f,lane<0?"Full mix ready":"Lane ready");
    return true;
}

bool AudioExporter::renderAllStems(const SongArrangement& a,const juce::File& directory,double sampleRate,Progress cb,const MixArray* mix) const
{
    if(!directory.exists()&&!directory.createDirectory())return false;const auto& lanes=a.getLanes();if(lanes.empty())return false;
    for(size_t i=0;i<lanes.size();++i){if(cb)cb((float)i/(float)lanes.size(),"Stem "+lanes[i].name);const auto f=directory.getChildFile(juce::String((int)i+1).paddedLeft('0',2)+"_"+safeFileName(lanes[i].name)+".wav");if(!renderSelectedLane(a,(int)i,f,sampleRate,{},mix))return false;}
    const auto instructions="SONARA stems\nBPM="+juce::String(a.getBpm(),3)+"\nkey="+a.getKeyLabel()+"\nseed="+juce::String::toHexString((juce::int64)a.getSongId()).paddedLeft('0',16)
        +"\nsample_rate="+juce::String(sampleRate,0)+"\nbars="+juce::String(a.getBars())
        +"\nAll WAVs start at bar 1 and include the same four-second decay tail.\nSet FL Studio's tempo above and place every stem at the same Playlist start.\nMIDI carries notes and timing; audio carries SONARA's rendered sound.\nEach stem uses the lane's SoundDNA, section automation and mix settings.\nThe safe nonlinear master is applied separately to each stem; their sum is not bit-identical to the mastered full-mix WAV.\n";
    if(!directory.getChildFile("STEMS.txt").replaceWithText(instructions))return false;
    if(cb)cb(1.f,"All stems ready");return true;
}

} // namespace sonara
