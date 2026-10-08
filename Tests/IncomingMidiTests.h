#pragma once
#include "../Source/PluginProcessor.h"
#include <cmath>

namespace sonara::incomingtests {
inline void restore(SonaraAudioProcessor& p,const juce::ValueTree& state) {
    juce::MemoryBlock bytes;juce::AudioProcessor::copyXmlToBinary(*state.createXml(),bytes);
    p.prepareToPlay(44100.0,257);p.setStateInformation(bytes.getData(),(int)bytes.getSize());
}
inline juce::String standaloneParity() {
    juce::MemoryBlock state;
    {
        SonaraAudioProcessor designer;designer.prepareToPlay(44100.0,257);
        designer.generatePatch("wide supersaw lead chorus delay reverb");designer.getStateInformation(state);
    }
    const auto render=[&state] {
        SonaraAudioProcessor player;player.prepareToPlay(44100.0,257);
        player.setStateInformation(state.getData(),(int)state.getSize());
        std::vector<float> samples;
        for(int block=0;block<64;++block) {
            juce::AudioBuffer<float> audio(2,257);juce::MidiBuffer midi;
            if(block==0)midi.addEvent(juce::MidiMessage::noteOn(1,65,(juce::uint8)100),17);
            if(block==40)midi.addEvent(juce::MidiMessage::noteOff(1,65),73);
            player.processBlock(audio,midi);
            for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i)samples.push_back(audio.getSample(ch,i));
        }
        return samples;
    };
    const auto single=render();
    std::array<std::unique_ptr<SonaraAudioProcessor>,4> neighbours;
    for(auto& p:neighbours){p=std::make_unique<SonaraAudioProcessor>();p->prepareToPlay(44100.0,257);}
    const auto multiple=render();
    for(size_t i=0;i<single.size();++i)if(std::abs(single[i]-multiple[i])>1e-7f)
        return "Opening additional instances changed standalone SoundDNA audio";
    std::cout<<"Standalone sound is sample-identical with four additional idle instances\n";
    return {};
}
inline juce::String run() {
    if(const auto error=standaloneParity();error.isNotEmpty())return error;
    SonaraAudioProcessor source;source.prepareToPlay(44100.0,257);
    if(!source.generateTrackWithSeed("progressive house 128 BPM F minor 64 bars memorable melody",0x510011))return "MIDI fixture generation failed";
    juce::MemoryBlock bytes;source.getStateInformation(bytes);
    auto state=juce::ValueTree::fromXml(*juce::AudioProcessor::getXmlFromBinary(bytes.getData(),(int)bytes.getSize()));
    auto tree=source.arrangementSnapshot()->toValueTree();tree.setProperty("bars",4,nullptr);
    auto sections=tree.getChildWithName("SECTIONS");sections.removeAllChildren(nullptr);
    juce::ValueTree section("SECTION");section.setProperty("name","DROP",nullptr);section.setProperty("startBar",0,nullptr);section.setProperty("bars",4,nullptr);section.setProperty("energy",.95f,nullptr);sections.addChild(section,-1,nullptr);
    tree.getChildWithName("SECTION_GOALS").removeAllChildren(nullptr);
    auto fixture=SongArrangement::fromValueTree(tree);
    for(int lane=0;lane<12;++lane) {
        auto& l=fixture.editLanes()[(size_t)lane];l.notes.clear();
        const int notes[12]={36,38,42,37,41,29,65,72,69,77,81,74};
        l.notes.push_back({notes[lane],96,.25+lane*.13,.5});
        if(lane==6){l.notes.push_back({69,92,1.03,.5});l.notes.push_back({72,90,1.03,.5});}
    }
    fixture=SongArrangement::fromValueTree(fixture.toValueTree());
    if(!fixture.validate())return "MIDI fixture invalid";
    state.removeChild(state.getChildWithName("SONARA_ARRANGEMENT"),nullptr);state.addChild(fixture.toValueTree(),-1,nullptr);
    state.setProperty("laneMidiSound",true,nullptr);state.setProperty("selectedLane",9,nullptr);
    state.setProperty("midiRoutingMode",1,nullptr);
    const double spb=44100.0*60.0/fixture.getBpm();
    SonaraAudioProcessor preview,host;restore(preview,state);restore(host,state);preview.startSongPreview();
    double energy=0.0;float error=0.f;int firstDifference=-1,largestDifference=-1;
    const int total=(int)std::llround(fixture.getTotalBeats()*spb+4.0*44100.0);
    for(int start=0;start<total;start+=257) {
        const int n=std::min(257,total-start);juce::AudioBuffer<float> a(2,n),b(2,n);juce::MidiBuffer empty,midi;
        for(const auto& lane:fixture.getLanes())for(const auto& note:lane.notes) {
            const int on=(int)std::llround(note.beat*spb),off=(int)std::llround((note.beat+note.length)*spb);
            if(on>=start&&on<start+n)midi.addEvent(juce::MidiMessage::noteOn(lane.midiChannel,note.note,(juce::uint8)note.velocity),on-start);
            if(!lane.drums&&off>=start&&off<start+n)midi.addEvent(juce::MidiMessage::noteOff(lane.midiChannel,note.note),off-start);
        }
        preview.processBlock(a,empty);host.processBlock(b,midi);
        for(int ch=0;ch<2;++ch)for(int i=0;i<n;++i) {
            const float x=b.getSample(ch,i);if(!std::isfinite(x)||std::abs(x)>.951f)return "Incoming MIDI exceeded safe ceiling";
            energy+=(double)x*x;
            const float difference=std::abs(x-a.getSample(ch,i));
            if(difference>2e-6f&&firstDifference<0)firstDifference=start+i;
            if(difference>error){error=difference;largestDifference=start+i;}
        }
    }
    if(energy<1e-7||error>2e-6f)return "Incoming multitrack MIDI differs from preview: "+juce::String(error,9)+" first sample="+juce::String(firstDifference)+" max sample="+juce::String(largestDifference);

    // The same melody on FL note-colour channels 1, 10 and 16 must sound identical.
    state.setProperty("midiRoutingMode",0,nullptr);
    for(int lane:{4,5,6,7,8,9,10}) {
        state.setProperty("selectedLane",lane,nullptr);
        std::array<std::unique_ptr<SonaraAudioProcessor>,3> players;
        for(auto& p:players){p=std::make_unique<SonaraAudioProcessor>();restore(*p,state);}
        double laneEnergy=0;float laneError=0;
        for(int block=0;block<64;++block) {
            std::array<juce::AudioBuffer<float>,3> audio;
            for(int colour=0;colour<3;++colour) {
                audio[(size_t)colour].setSize(2,257);juce::MidiBuffer midi;const int channel=colour==0?1:(colour==1?10:16);
                if(block==0)midi.addEvent(juce::MidiMessage::noteOn(channel,lane==5?29:65,(juce::uint8)100),17);
                if(block==20)midi.addEvent(juce::MidiMessage::pitchWheel(channel,10000),31);
                if(block==40)midi.addEvent(juce::MidiMessage::noteOff(channel,lane==5?29:65),73);
                players[(size_t)colour]->processBlock(audio[(size_t)colour],midi);
            }
            for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i) {
                const float x=audio[0].getSample(ch,i);laneEnergy+=(double)x*x;
                for(int colour=1;colour<3;++colour)laneError=std::max(laneError,std::abs(x-audio[(size_t)colour].getSample(ch,i)));
            }
        }
        if(laneEnergy<1e-8||laneError>1e-7f)return "Piano Roll channel changed selected lane sound: "+juce::String(lane);
        const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonara-single-lane",".mid");
        if(!players[0]->writeSelectedLaneMidiFile(file))return "Single-lane MIDI export failed";
        juce::FileInputStream stream(file);juce::MidiFile midiFile;
        if(!midiFile.readFrom(stream)||midiFile.getNumTracks()!=1)return "Piano Roll export is not one track";
        int notes=0;for(int i=0;i<midiFile.getTrack(0)->getNumEvents();++i) {
            const auto& message=midiFile.getTrack(0)->getEventPointer(i)->message;
            if(message.isNoteOn()){++notes;if(message.getChannel()!=1)return "Piano Roll export retained a multitrack channel";}
        }
        file.deleteFile();if(notes!=(int)fixture.getLanes()[(size_t)lane].notes.size())return "Piano Roll export includes other lanes";
        players[0]->stopPreview();players[0]->setLaneMix(lane,SonaraAudioProcessor::LaneMixParameter::level,0.f);
        juce::AudioBuffer<float> muted(2,257);juce::MidiBuffer note;note.addEvent(juce::MidiMessage::noteOn(10,65,(juce::uint8)100),0);players[0]->processBlock(muted,note);
        if(muted.getMagnitude(0,257)>1e-8f)return "Host MIDI ignores the selected lane mixer mute";
    }
    std::cout<<"Incoming MIDI/preview parity error="<<error<<"; seven melodic lanes preserve MIDI colours, pitch wheel, mixer mute and single-track export\n";
    return {};
}
}
