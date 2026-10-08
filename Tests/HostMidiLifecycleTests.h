#pragma once
#include "IncomingMidiTests.h"
#include <chrono>

namespace sonara::lifecycletests {
class Playhead final : public juce::AudioPlayHead {
public:
    PositionInfo position;
    juce::Optional<PositionInfo> getPosition() const override {return position;}
};
inline juce::ValueTree fixture() {
    SonaraAudioProcessor source;source.prepareToPlay(44100.0,257);
    source.generateTrackWithSeed("progressive house 128 BPM F minor 64 bars",0x540054);
    juce::MemoryBlock bytes;source.getStateInformation(bytes);
    auto state=juce::ValueTree::fromXml(*juce::AudioProcessor::getXmlFromBinary(bytes.getData(),(int)bytes.getSize()));
    auto tree=source.arrangementSnapshot()->toValueTree();tree.setProperty("bars",4,nullptr);
    auto sections=tree.getChildWithName("SECTIONS");sections.removeAllChildren(nullptr);
    juce::ValueTree section("SECTION");section.setProperty("name","DROP",nullptr);section.setProperty("startBar",0,nullptr);section.setProperty("bars",4,nullptr);section.setProperty("energy",.95f,nullptr);sections.addChild(section,-1,nullptr);
    tree.getChildWithName("SECTION_GOALS").removeAllChildren(nullptr);
    auto song=SongArrangement::fromValueTree(tree);
    for(auto& lane:song.editLanes()) {lane.notes.clear();if(!lane.drums)lane.notes.push_back({65,100,0.0,3.0});}
    state.removeChild(state.getChildWithName("SONARA_ARRANGEMENT"),nullptr);state.addChild(song.toValueTree(),-1,nullptr);
    state.setProperty("laneMidiSound",true,nullptr);state.setProperty("selectedLane",9,nullptr);state.setProperty("midiRoutingMode",0,nullptr);
    return state;
}
inline std::vector<float> snippet(SonaraAudioProcessor& player,bool bend=false) {
    std::vector<float> samples;
    for(int block=0;block<64;++block) {
        juce::AudioBuffer<float> audio(2,257);juce::MidiBuffer midi;
        if(block==0)midi.addEvent(juce::MidiMessage::noteOn(1,65,(juce::uint8)100),17);
        if(bend&&block==20)midi.addEvent(juce::MidiMessage::pitchWheel(1,12000),31);
        if(block==40)midi.addEvent(juce::MidiMessage::noteOff(1,65),73);
        player.processBlock(audio,midi);
        for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i)samples.push_back(audio.getSample(ch,i));
    }
    return samples;
}
inline float difference(const std::vector<float>& a,const std::vector<float>& b) {
    float error=0.f;for(size_t i=0;i<a.size();++i)error=std::max(error,std::abs(a[i]-b[i]));return error;
}
inline juce::String restart() {
    SonaraAudioProcessor source;source.prepareToPlay(44100.0,257);
    source.generatePatch("wide supersaw lead chorus delay reverb");
    juce::MemoryBlock bytes;source.getStateInformation(bytes);
    SonaraAudioProcessor player;player.prepareToPlay(44100.0,257);player.setStateInformation(bytes.getData(),(int)bytes.getSize());
    const auto first=snippet(player);player.stopPreview();
    const auto repeated=snippet(player);
    const float initialError=difference(first,repeated);
    if(initialError>2e-6f) {
        for(size_t i=0;i<first.size();++i)if(std::abs(first[i]-repeated[i])>2e-6f) {
            std::cerr<<"Restart first difference index="<<i<<" first="<<first[i]<<" repeated="<<repeated[i]<<'\n';break;
        }
    }
    if(initialError>2e-6f)return "Standalone restart changed saved audio: "+juce::String(initialError,9);
    player.stopPreview();snippet(player,true);player.stopPreview();
    const float bendError=difference(first,snippet(player));
    if(bendError>2e-6f)return "Stopped pitch wheel leaked into next playback: "+juce::String(bendError,9);
    return {};
}
inline juce::String transport(const juce::ValueTree& state) {
    SonaraAudioProcessor timed,free;incomingtests::restore(timed,state);incomingtests::restore(free,state);
    Playhead playhead;playhead.position.setIsPlaying(true);playhead.position.setBpm(110.0);timed.setPlayHead(&playhead);
    float error=0.f;double energy=0;
    for(int block=0;block<128;++block) {
        playhead.position.setPpqPosition(block*257.0/44100.0*110.0/60.0);
        juce::AudioBuffer<float> a(2,257),b(2,257);juce::MidiBuffer x,y;
        if(block==0)x.addEvent(juce::MidiMessage::noteOn(16,65,(juce::uint8)100),17);
        if(block==40)x.addEvent(juce::MidiMessage::pitchWheel(16,10000),31);
        if(block==96)x.addEvent(juce::MidiMessage::noteOff(16,65),73);
        y=x;timed.processBlock(a,x);free.processBlock(b,y);
        for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i) {
            energy+=(double)a.getSample(ch,i)*a.getSample(ch,i);
            error=std::max(error,std::abs(a.getSample(ch,i)-b.getSample(ch,i)));
        }
    }
    if(energy<1e-7||error>2e-6f)return "Host tempo different from song resets sustained notes: "+juce::String(error,9);
    playhead.position.setPpqPosition(1.0); // seek: do not chase the internal arrangement
    juce::AudioBuffer<float> audio(2,257);juce::MidiBuffer midi;timed.processBlock(audio,midi);
    if(audio.getMagnitude(0,257)>1e-8f)return "Host seek injected internal song notes";
    midi.addEvent(juce::MidiMessage::noteOn(1,65,(juce::uint8)100),0);timed.processBlock(audio,midi);
    playhead.position.setIsPlaying(false);timed.processBlock(audio,midi);
    if(audio.getMagnitude(0,257)>1e-8f)return "Host stop retained notes or FX";
    midi.addEvent(juce::MidiMessage::noteOn(10,65,(juce::uint8)100),0);timed.processBlock(audio,midi);
    if(audio.getMagnitude(0,257)<1e-5f)return "Stopped host cannot play live Piano Roll notes";
    timed.setPlayHead(nullptr);
    std::cout<<"Different host tempo parity error="<<error<<"; seeks/stops clear notes without arrangement chase\n";
    return {};
}
inline juce::String loadedIdle(const juce::ValueTree& state) {
    std::array<std::unique_ptr<SonaraAudioProcessor>,5> players;
    for(auto& p:players){p=std::make_unique<SonaraAudioProcessor>();incomingtests::restore(*p,state);}
    const auto begin=std::chrono::steady_clock::now();float peak=0.f;
    for(int block=0;block<240;++block)for(auto& p:players) {
        juce::AudioBuffer<float> audio(2,257);juce::MidiBuffer midi;p->processBlock(audio,midi);
        peak=std::max(peak,audio.getMagnitude(0,257));
    }
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    std::cout<<"Five loaded arrangements idle: "<<elapsed<<" s, peak="<<peak<<'\n';
    if(peak>1e-8f||elapsed>1.5)return "Loaded arrangement idle exceeded existing five-instance budget";
    return {};
}
inline juce::String savedProject() {
    SonaraAudioProcessor designer;designer.prepareToPlay(44100.0,257);
    if(!designer.generateTrackWithSeed("tech house 126 BPM A minor 64 bars no pads",0x540123))return "Project fixture failed";
    designer.setSelectedLane(9);designer.setMidiRoutingMode(SonaraAudioProcessor::MidiRoutingMode::arrangementChannels);
    designer.setSelectedLaneSound("bright glassy pluck lead dry",false);
    designer.setLaneMix(9,SonaraAudioProcessor::LaneMixParameter::level,.63f);
    designer.setLaneMix(9,SonaraAudioProcessor::LaneMixParameter::pan,.32f);
    const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonara-audio-restore",".sonaraproject");
    if(!designer.saveProject(file))return "Project save failed";
    juce::MemoryBlock bytes;designer.getStateInformation(bytes);
    SonaraAudioProcessor fromFile,fromHost;fromFile.prepareToPlay(44100.0,257);fromHost.prepareToPlay(44100.0,257);
    if(!fromFile.loadProject(file))return "Project restore failed";file.deleteFile();
    fromHost.setStateInformation(bytes.getData(),(int)bytes.getSize());
    if(fromFile.getMidiRoutingMode()!=designer.getMidiRoutingMode())return "Project lost incoming channel mode";
    const float error=difference(snippet(fromFile),snippet(fromHost));
    if(error>2e-6f)return "Saved project and host state differ in actual MIDI audio: "+juce::String(error,9);
    std::cout<<"Project/state restored audio parity error="<<error<<'\n';return {};
}
inline juce::String run() {
    if(auto error=restart();error.isNotEmpty())return error;
    const auto state=fixture();
    if(auto error=transport(state);error.isNotEmpty())return error;
    if(auto error=loadedIdle(state);error.isNotEmpty())return error;
    return savedProject();
}
}
