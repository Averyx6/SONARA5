#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/Export/AudioExporter.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>
#include <set>

namespace {
int fail(const juce::String& m){std::cerr<<"SONARA playback test failure: "<<m<<"\n";return 1;}

const sonara::ArrangementLane* laneNamed(const sonara::SongArrangement& a,const juce::String& name)
{
    for(const auto& l:a.getLanes())if(l.name==name)return &l;
    return nullptr;
}

uint64_t laneHash(const sonara::ArrangementLane& lane)
{
    uint64_t h=1469598103934665603ULL;
    const int n=juce::jmin(128,(int)lane.notes.size());
    for(int i=0;i<n;++i)
    {
        const auto& x=lane.notes[(size_t)i];
        const uint64_t vals[]={(uint64_t)x.note,(uint64_t)std::llround(x.beat*16.0),
                               (uint64_t)std::llround(x.length*32.0),(uint64_t)x.velocity};
        for(auto v:vals){h^=v+0x9e37ULL;h*=1099511628211ULL;}
    }
    h^=lane.sound.seed;h*=1099511628211ULL;
    return h;
}
uint64_t leadSkeletonHash(const sonara::SongArrangement& a)
{
    const auto* lead=laneNamed(a,"LEAD");
    if(!lead||lead->notes.size()<8)return 0;
    uint64_t h=1469598103934665603ULL;
    const int n=juce::jmin(96,(int)lead->notes.size());
    for(int i=1;i<n;++i)
    {
        const auto& x=lead->notes[(size_t)i];
        const auto& p=lead->notes[(size_t)i-1];
        const int interval=juce::jlimit(-18,18,x.note-p.note);
        const int gap=juce::jlimit(0,64,(int)std::llround((x.beat-p.beat)*8.0));
        const int length=juce::jlimit(1,32,(int)std::llround(x.length*8.0));
        const int contour=interval>0?2:(interval<0?0:1);
        const int values[]={interval+18,gap,length,contour};
        for(const int v:values){h^=(uint64_t)(v+257);h*=1099511628211ULL;}
    }
    h^=(uint64_t)lead->notes.size();h*=1099511628211ULL;
    return h;
}

double renderSingleDrumNote(const sonara::SongArrangement& a,int midiNote)
{
    if(a.getLanes().size()<4)return 0.0;
    sonara::DrumSynth drums;drums.prepare(48000.0);
    drums.configureKit(a.getLanes()[0].sound,a.getLanes()[1].sound,a.getLanes()[2].sound,a.getLanes()[3].sound);
    juce::AudioBuffer<float> b(2,4096);b.clear();
    const sonara::DrumTrigger hit{0,midiNote,.85f};
    drums.render(b,&hit,1);
    double energy=0.0;
    for(int ch=0;ch<2;++ch)for(int i=0;i<b.getNumSamples();++i){const float x=b.getSample(ch,i);energy+=(double)x*x;}
    return energy;
}


bool finiteAndSafe(const juce::AudioBuffer<float>& b,float& peak,double& energy)
{
    for(int ch=0;ch<b.getNumChannels();++ch)
        for(int i=0;i<b.getNumSamples();++i)
        {
            const float x=b.getSample(ch,i);
            if(!std::isfinite(x))return false;
            peak=juce::jmax(peak,std::abs(x));
            energy+=(double)x*x;
        }
    return peak<=.951f;
}

double renderSeconds(SonaraAudioProcessor& p,double sr,double seconds,int blockSize,float& peak,double& energy)
{
    juce::AudioBuffer<float> audio(2,blockSize);
    juce::MidiBuffer midi;
    const int blocks=(int)std::ceil(seconds*sr/blockSize);
    const auto t0=std::chrono::steady_clock::now();
    for(int b=0;b<blocks;++b)
    {
        audio.clear();midi.clear();
        p.processBlock(audio,midi);
        if(!finiteAndSafe(audio,peak,energy))return -1.0;
    }
    return std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
}

double renderLaneEnergy(const sonara::ArrangementLane& lane,double sr,int voices)
{
    sonara::SonaraEngine e;
    e.setLowCpuMode(true);e.setVoiceLimit(voices);e.prepare(sr,512,2);e.setPatch(lane.sound);
    juce::AudioBuffer<float> b(2,512);juce::MidiBuffer midi;midi.ensureSize(4096);
    if(lane.notes.empty())return 0.0;
    const auto& n=lane.notes.front();
    double energy=0.0;float peak=0.f;
    for(int block=0;block<20;++block)
    {
        b.clear();midi.clear();
        if(block==0)midi.addEvent(juce::MidiMessage::noteOn(lane.midiChannel,n.note,(juce::uint8)n.velocity),0);
        if(block==12)midi.addEvent(juce::MidiMessage::noteOff(lane.midiChannel,n.note),0);
        e.render(b,midi);
        if(!finiteAndSafe(b,peak,energy))return -1.0;
    }
    return energy;
}

double renderDrumEnergy(const sonara::SongArrangement& a,double sr)
{
    if(a.getLanes().size()<4)return 0.0;
    sonara::DrumSynth drums;drums.prepare(sr);
    drums.configureKit(a.getLanes()[0].sound,a.getLanes()[1].sound,a.getLanes()[2].sound,a.getLanes()[3].sound);
    juce::AudioBuffer<float> b(2,512);b.clear();
    std::array<sonara::DrumTrigger,5> hits{{
        {0,36,1.f},{96,38,.9f},{176,42,.7f},{260,39,.75f},{384,46,.65f}
    }};
    drums.render(b,hits.data(),(int)hits.size());
    double energy=0.0;float peak=0.f;
    if(!finiteAndSafe(b,peak,energy))return -1.0;
    return energy;
}
}

int main()
{
    const juce::String prompt="Emotional progressive house, 128 BPM, F minor, emotional memorable hook, huge melodic drop";

    // TEST E + F + G + H + performance: exercise the real processor/processBlock path.
    for(const double sr:{44100.0,48000.0,96000.0})
    {
        SonaraAudioProcessor p;
        constexpr int blockSize=512;
        p.prepareToPlay(sr,blockSize);
        p.generateTrack(prompt);
        auto a=p.arrangementSnapshot();
        if(!a||a->getLanes().size()!=12)return fail("processor did not generate 12 lanes");
        if(a->getSongId()!=p.getSongGenerationSeed())return fail("winning candidate was not the arrangement actually published");
        if(a->getHarmonyId()==0||a->getMelodyId()==0)return fail("composition identity IDs were not created");

        const auto* bass=laneNamed(*a,"BASS");
        const auto* sub=laneNamed(*a,"SUB");
        if(!bass||!sub||bass->notes.empty()||sub->notes.empty())return fail("BASS/SUB MIDI missing");
        if(renderLaneEnergy(*bass,sr,2)<=1.0e-7)return fail("BASS rendered zero/unsafe audio");
        if(renderLaneEnergy(*sub,sr,1)<=1.0e-7)return fail("SUB rendered zero/unsafe audio");
        if(renderDrumEnergy(*a,sr)<=1.0e-7)return fail("drums rendered zero/unsafe audio");
        if(sr==48000.0)
        {
            const double percEnergy=renderSingleDrumNote(*a,37);
            const double crashEnergy=renderSingleDrumNote(*a,49);
            if(percEnergy<=1.0e-8||crashEnergy<=1.0e-8)return fail("percussion/crash voice rendered silent");
            const double ratio=percEnergy/crashEnergy;
            if(ratio<.03||ratio>.82)return fail("note 37 percussion still behaves like crash/invalid hit");
        }

        int dropBar=0,breakBar=0,hookBar=0;
        for(const auto& s:a->getSections())
        {
            if(s.name=="DROP")dropBar=s.startBar;
            else if(s.name=="BREAKDOWN")breakBar=s.startBar;
            else if(s.name=="FINAL HOOK")hookBar=s.startBar;
        }

        for(const int bar:{0,dropBar,breakBar,hookBar})
        {
            p.startSongPreviewAtBar(bar);
            float peak=0.f;double energy=0.0;
            const double elapsed=renderSeconds(p,sr,.35,blockSize,peak,energy);
            if(elapsed<0.0||energy<=1.0e-8)return fail("section seek produced unsafe/silent preview");
            if(p.currentSongBar()<bar)return fail("section seek playhead moved backwards");
        }

        p.startSongPreviewAtBar(dropBar);
        {
            float pausePeak=0.f;double pauseEnergy=0.0;
            if(renderSeconds(p,sr,.12,blockSize,pausePeak,pauseEnergy)<0.0)return fail("pre-pause render failed");
            const auto pausedAt=p.songPosition01();
            p.pauseSongPreview();
            if(p.isSongPlaying()||p.songPosition01()+1.0e-6<pausedAt)return fail("pause did not preserve position");
            p.resumeSongPreview();
            if(!p.isSongPlaying())return fail("resume did not restart preview");
        }

        float peak=0.f;double energy=0.0;
        const double elapsed=renderSeconds(p,sr,30.0,blockSize,peak,energy);
        if(elapsed<0.0||energy<=1.0e-6)return fail("30-second preview unsafe or silent");
        const double realtimeFactor=elapsed/30.0;
        const double rms=std::sqrt(energy/(30.0*sr*2.0));
        std::cout<<"preview "<<sr<<" Hz realtime factor "<<realtimeFactor<<" peak "<<peak<<" rms "<<rms<<"\n";
        if(realtimeFactor>=.50)return fail("preview exceeded 50% realtime CPU budget on CI runner");
        if(rms<.012)return fail("preview mix is still too quiet");

        const auto oldSeed=p.getSongGenerationSeed();
        p.startSongPreviewAtBar(dropBar);
        p.generateTrack(prompt);
        if(p.isSongPlaying())return fail("generation did not stop active preview safely");
        if(p.getSongGenerationSeed()==oldSeed)return fail("generate while active reused song seed");
    }

    // Direct production-path melody test: same exact prompt must never recycle
    // the previous rhythm/interval skeleton.
    {
        SonaraAudioProcessor composer;
        composer.prepareToPlay(48000.0,512);
        std::set<uint64_t> skeletons;
        for(int i=0;i<8;++i)
        {
            composer.generateTrack(prompt);
            auto song=composer.arrangementSnapshot();
            if(!song)return fail("same-prompt generation returned no arrangement");
            const auto hash=leadSkeletonHash(*song);
            if(hash==0||!skeletons.insert(hash).second)
                return fail("GENERATE TRACK recycled a previous melody skeleton");
            if(i>0&&composer.getMelodyNovelty()<.45f)
                return fail("GENERATE TRACK accepted a melody with low novelty");
            if(i>0&&composer.getHarmonyNovelty()<.14f)
                return fail("GENERATE TRACK accepted near-identical normalized harmony");
            if(i>0&&composer.getSongNovelty()<.36f)
                return fail("GENERATE TRACK accepted a low-novelty whole song");
            if(song->getSongId()!=composer.getSongGenerationSeed())
                return fail("candidate scorer selected one song but processor published another");
        }
    }

    // TEST I: RANDOMIZE EVERYTHING materially changes multiple musical systems.
    SonaraAudioProcessor randomizer;
    randomizer.prepareToPlay(48000.0,512);
    randomizer.generateTrack(prompt);
    auto before=randomizer.arrangementSnapshot();
    if(!before)return fail("missing base arrangement");
    std::vector<std::pair<juce::String,uint64_t>> oldHashes;
    for(const auto& name:{juce::String("KICK"),juce::String("BASS"),juce::String("SUB"),juce::String("CHORDS"),juce::String("LEAD")})
    {
        const auto* lane=laneNamed(*before,name);if(!lane)return fail("base lane missing "+name);
        oldHashes.push_back({name,laneHash(*lane)});
    }
    const auto oldSeed=randomizer.getSongGenerationSeed();
    randomizer.randomizeEverything(prompt);
    auto after=randomizer.arrangementSnapshot();
    if(!after||randomizer.getSongGenerationSeed()==oldSeed)return fail("RANDOMIZE EVERYTHING did not create a new song");
    for(const auto& entry:oldHashes)
    {
        const auto& name=entry.first;const auto hash=entry.second;
        const auto* lane=laneNamed(*after,name);if(!lane||laneHash(*lane)==hash)return fail("RANDOMIZE EVERYTHING kept "+name);
    }

    // TEST J: SURPRISE ME must create a usable prompt and complete arrangement.
    const auto surprise=randomizer.makeSurprisePrompt();
    if(surprise.length()<40||!surprise.containsIgnoreCase("BPM"))return fail("SURPRISE ME prompt invalid");
    randomizer.generateTrack(surprise);
    auto surprised=randomizer.arrangementSnapshot();
    if(!surprised||surprised->getLanes().size()!=12||surprised->getSections().size()!=8)return fail("SURPRISE ME arrangement incomplete");

    // Reference audio path: create a real WAV and run the same analyser used by LOAD AUDIO.
    auto referenceWav=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonara-reference-audio",".wav");
    {
        juce::WavAudioFormat wavFormat;
        auto stream=std::make_unique<juce::FileOutputStream>(referenceWav);
        if(!stream->openedOk())return fail("could not create reference WAV");
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wavFormat.createWriterFor(stream.release(),44100.0,juce::AudioChannelSet::stereo(),24,{},0));
        if(!writer)return fail("could not create WAV writer");
        constexpr int samples=44100*2;
        juce::AudioBuffer<float> tone(2,samples);
        for(int i=0;i<samples;++i)
        {
            const float env=.35f+.15f*std::sin(juce::MathConstants<double>::twoPi*i/(44100.0*.5));
            const float x=env*(float)std::sin(juce::MathConstants<double>::twoPi*220.0*i/44100.0);
            tone.setSample(0,i,x);tone.setSample(1,i,x);
        }
        if(!writer->writeFromAudioSampleBuffer(tone,0,samples))return fail("reference WAV write failed");
    }
    if(!randomizer.analyseReferenceFile(referenceWav)||!randomizer.hasReference())
        return fail("LOAD AUDIO analysis path failed");
    referenceWav.deleteFile();

    // Reference/MIDI path: import -> analysis state -> RESOUND -> re-export.
    auto referenceMidi=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonara-reference-source",".mid");
    {
        juce::MidiFile mf;mf.setTicksPerQuarterNote(960);juce::MidiMessageSequence seq;
        auto tempo=juce::MidiMessage::tempoMetaEvent(500000);tempo.setTimeStamp(0);seq.addEvent(tempo);
        for(int i=0;i<8;++i)
        {
            auto on=juce::MidiMessage::noteOn(1,60+(i%5)*2,(juce::uint8)100);on.setTimeStamp(i*480);seq.addEvent(on);
            auto off=juce::MidiMessage::noteOff(1,60+(i%5)*2);off.setTimeStamp(i*480+360);seq.addEvent(off);
        }
        seq.updateMatchedPairs();mf.addTrack(seq);
        juce::FileOutputStream out(referenceMidi);
        if(!out.openedOk()||!mf.writeTo(out))return fail("could not create reference MIDI test file");
    }
    if(!randomizer.importMidiFile(referenceMidi)||!randomizer.hasReference())return fail("reference MIDI import failed");
    randomizer.resoundReference("glassy emotional lead, wide but controlled");
    auto extracted=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonara-reference-extracted",".mid");
    if(!randomizer.writeReferenceMidiFile(extracted)||extracted.getSize()<64)return fail("reference MIDI re-export failed");
    referenceMidi.deleteFile();extracted.deleteFile();

    // MIDI export paths.
    auto fullMidi=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonara-playback-full",".mid");
    if(!randomizer.writeArrangementMidiFile(fullMidi)||fullMidi.getSize()<512)return fail("full MIDI export failed");
    int subIndex=-1;for(int i=0;i<(int)surprised->getLanes().size();++i)if(surprised->getLanes()[(size_t)i].name=="SUB"){subIndex=i;break;}
    if(subIndex<0)return fail("SUB index missing");
    randomizer.setSelectedLane(subIndex);
    randomizer.setLaneMix(subIndex,SonaraAudioProcessor::LaneMixParameter::level,.73f);
    randomizer.setLaneMix(subIndex,SonaraAudioProcessor::LaneMixParameter::pan,.12f); // SUB must clamp this back to center
    randomizer.setLaneMix(subIndex,SonaraAudioProcessor::LaneMixParameter::width,0.f);
    randomizer.setLaneMix(subIndex,SonaraAudioProcessor::LaneMixParameter::fxSend,0.f);

    juce::MemoryBlock state;
    randomizer.getStateInformation(state);
    SonaraAudioProcessor restored;
    restored.prepareToPlay(48000.0,512);
    restored.setStateInformation(state.getData(),(int)state.getSize());
    const auto restoredMix=restored.getLaneMix(subIndex);
    if(std::abs(restoredMix.level-.73f)>.001f||std::abs(restoredMix.pan)>.001f
       ||std::abs(restoredMix.width)>.001f||std::abs(restoredMix.fxSend)>.001f)
        return fail("lane mixer state did not survive plugin state round-trip");
    if(!restored.arrangementSnapshot()||restored.arrangementSnapshot()->getLanes().size()!=12)
        return fail("song arrangement did not survive plugin state round-trip");

    auto projectFile=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonara-project-roundtrip",".sonara");
    if(!randomizer.saveProject(projectFile))return fail("project save failed");
    SonaraAudioProcessor projectLoaded;
    projectLoaded.prepareToPlay(48000.0,512);
    if(!projectLoaded.loadProject(projectFile))return fail("project load failed");
    const auto projectMix=projectLoaded.getLaneMix(subIndex);
    if(std::abs(projectMix.level-.73f)>.001f||!projectLoaded.arrangementSnapshot())
        return fail("project round-trip lost mixer or arrangement");
    projectFile.deleteFile();

    auto subMidi=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonara-playback-sub",".mid");
    if(!randomizer.writeSelectedLaneMidiFile(subMidi)||subMidi.getSize()<64)return fail("selected SUB MIDI export failed");
    fullMidi.deleteFile();subMidi.deleteFile();

    // Shortened offline export exercises the real full-mix/stem renderers without a long CI file.
    auto shortTree=surprised->toValueTree();
    shortTree.setProperty("bars",4,nullptr);
    auto shortSong=sonara::SongArrangement::fromValueTree(shortTree);
    sonara::AudioExporter exporter;
    auto wav=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonara-full-mix-test",".wav");
    if(!exporter.renderFullMix(shortSong,wav,44100.0,{})||wav.getSize()<4096)return fail("full mix WAV export failed");
    auto stems=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonara-stems-test","");
    if(!exporter.renderAllStems(shortSong,stems,44100.0,{}))return fail("stem export failed");
    int wavCount=0;for(const auto& file:stems.findChildFiles(juce::File::findFiles,false,"*.wav")){++wavCount;file.deleteFile();}
    stems.deleteRecursively();wav.deleteFile();
    if(wavCount!=12)return fail("stem export did not produce 12 lane WAVs");

    std::cout<<"SONARA processor playback, CPU, seek, randomize, surprise and export tests passed\n";
    return 0;
}
