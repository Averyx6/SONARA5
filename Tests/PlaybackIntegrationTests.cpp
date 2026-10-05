#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/Engine/MixPolicy.h"
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

uint64_t normalizedProgressionHash(const sonara::SongArrangement& a)
{
    const auto fp=a.getProgressionFingerprint();
    if(fp.size()<5)return 0;
    const int n=juce::jlimit(1,8,fp[0]);
    if((int)fp.size()<2+n*3)return 0;
    uint64_t h=1469598103934665603ULL;
    h^=(uint64_t)n;h*=1099511628211ULL;
    for(int i=0;i<n;++i)
    {
        h^=(uint64_t)(fp[(size_t)(2+i*3)]+17);
        h*=1099511628211ULL;
    }
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
            int leadIndex=-1,bassIndex=-1;
            for(int i=0;i<(int)a->getLanes().size();++i)
            {
                if(a->getLanes()[(size_t)i].name=="LEAD")leadIndex=i;
                if(a->getLanes()[(size_t)i].name=="BASS")bassIndex=i;
            }
            if(leadIndex<0||bassIndex<0)return fail("lane override test could not locate LEAD/BASS");
            const auto originalLeadSeed=a->getLanes()[(size_t)leadIndex].sound.seed;
            const auto originalBassSeed=a->getLanes()[(size_t)bassIndex].sound.seed;
            const auto originalLeadNotes=a->getLanes()[(size_t)leadIndex].notes.size();

            p.setSelectedLane(leadIndex);
            if(p.currentPatch().seed!=a->getLanes()[(size_t)leadIndex].sound.seed)
                return fail("selected lane SoundDNA was not loaded into live SONARA engine");
            if(!p.setSelectedLaneSound("glassy emotional festival pluck lead wide but clean",false))
                return fail("custom selected-lane SoundDNA prompt failed");
            auto custom=p.arrangementSnapshot();
            if(!custom||custom->getLanes()[(size_t)leadIndex].sound.seed==originalLeadSeed
               ||!custom->getLanes()[(size_t)leadIndex].sound.name.containsIgnoreCase("Custom"))
                return fail("custom lane sound was not published");
            if(p.currentPatch().seed!=custom->getLanes()[(size_t)leadIndex].sound.seed)
                return fail("custom lane SoundDNA did not stay synced to live SONARA MIDI engine");
            if(custom->getLanes()[(size_t)bassIndex].sound.seed!=originalBassSeed
               ||custom->getLanes()[(size_t)leadIndex].notes.size()!=originalLeadNotes)
                return fail("lane sound override changed another instrument or MIDI");

            if(!p.setSelectedLaneSound({},true))return fail("AUTO FIT selected lane failed");
            auto refit=p.arrangementSnapshot();
            if(!refit||refit->getLanes()[(size_t)bassIndex].sound.seed!=originalBassSeed
               ||refit->getLanes()[(size_t)leadIndex].notes.size()!=originalLeadNotes)
                return fail("AUTO FIT changed unrelated lane or MIDI");
            if(p.currentPatch().seed!=refit->getLanes()[(size_t)leadIndex].sound.seed)
                return fail("AUTO FIT lane SoundDNA did not sync to live SONARA engine");


            const double percEnergy=renderSingleDrumNote(*a,37);
            const double crashEnergy=renderSingleDrumNote(*a,49);
            const double impactEnergy=renderSingleDrumNote(*a,57);
            const double kickEnergy=renderSingleDrumNote(*a,36);
            const double snareEnergy=renderSingleDrumNote(*a,38);
            const double lowTomEnergy=renderSingleDrumNote(*a,45);
            const double midTomEnergy=renderSingleDrumNote(*a,47);
            const double highTomEnergy=renderSingleDrumNote(*a,50);
            if(percEnergy<=1.0e-8||crashEnergy<=1.0e-8||impactEnergy<=1.0e-8
               ||kickEnergy<=1.0e-8||snareEnergy<=1.0e-8)
                return fail("core drum voice rendered silent");
            if(lowTomEnergy<=1.0e-8||midTomEnergy<=1.0e-8||highTomEnergy<=1.0e-8)
                return fail("transition tom fill notes rendered silent");
            const double ratio=percEnergy/crashEnergy;
            if(ratio<.03||ratio>.82)return fail("note 37 percussion still behaves like crash/invalid hit");
            if(impactEnergy<=crashEnergy)
                return fail("drop impact note does not render its combined low hit and crash");
            if(kickEnergy<percEnergy*2.0||snareEnergy<percEnergy*1.25)
                return fail("festival kick/snare are still weaker than supporting percussion");
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

        if(sr==48000.0)
        {
            float dropPeak=0.f,breakPeak=0.f;
            double dropEnergy=0.0,breakEnergy=0.0;
            p.startSongPreviewAtBar(dropBar);
            if(renderSeconds(p,sr,.75,blockSize,dropPeak,dropEnergy)<0.0)
                return fail("v2.9 drop energy render failed");
            p.startSongPreviewAtBar(breakBar);
            if(renderSeconds(p,sr,.75,blockSize,breakPeak,breakEnergy)<0.0)
                return fail("v2.9 breakdown energy render failed");
            if(dropEnergy<=breakEnergy*1.35)
                return fail("v2.9 stored energy curve is not audible in preview");
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


    // v1.6 multi-instance regression: loaded but idle SONARAs must be effectively
    // silent/cheap, and five active live engines must remain finite under the
    // shared runtime eco budget.
    {
        constexpr int instanceCount=5,blockSize=256;
        std::array<std::unique_ptr<SonaraAudioProcessor>,instanceCount> instances;
        for(auto& instance:instances)
        {
            instance=std::make_unique<SonaraAudioProcessor>();
            instance->prepareToPlay(48000.0,blockSize);
        }

        juce::AudioBuffer<float> audio(2,blockSize);
        juce::MidiBuffer midi;
        const auto idleStart=std::chrono::steady_clock::now();
        double idleEnergy=0.0;
        for(int block=0;block<240;++block)
            for(auto& instance:instances)
            {
                audio.clear();midi.clear();
                instance->processBlock(audio,midi);
                for(int ch=0;ch<audio.getNumChannels();++ch)
                    for(int s=0;s<audio.getNumSamples();++s)
                        idleEnergy+=(double)audio.getSample(ch,s)*audio.getSample(ch,s);
            }
        const double idleElapsed=std::chrono::duration<double>(
            std::chrono::steady_clock::now()-idleStart).count();
        if(idleEnergy!=0.0)return fail("idle multi-instance fast path produced audio");
        if(idleElapsed>1.5)return fail("five idle SONARA instances still consume excessive callback CPU");

        double activeEnergy=0.0;float activePeak=0.f;
        const auto activeStart=std::chrono::steady_clock::now();
        for(int block=0;block<180;++block)
        {
            for(int i=0;i<instanceCount;++i)
            {
                audio.clear();midi.clear();
                if(block==0)midi.addEvent(juce::MidiMessage::noteOn(1,60+i*2,(juce::uint8)100),0);
                if(block==120)midi.addEvent(juce::MidiMessage::noteOff(1,60+i*2),0);
                instances[(size_t)i]->processBlock(audio,midi);
                if(!finiteAndSafe(audio,activePeak,activeEnergy))
                    return fail("multi-instance runtime eco path produced unsafe audio");
            }
        }
        const double activeElapsed=std::chrono::duration<double>(
            std::chrono::steady_clock::now()-activeStart).count();
        if(activeEnergy<=1.0e-8)return fail("multi-instance runtime eco path rendered silence");
        // 180 * 256 samples = .96 s of audio per instance. Five instances should
        // remain comfortably below five-times realtime on the CI runner.
        if(activeElapsed>4.8)return fail("five active SONARA instances exceeded multi-instance CPU budget");
    }

    // Direct production-path melody test: same exact prompt must never recycle
    // the previous rhythm/interval skeleton.
    {
        SonaraAudioProcessor composer;
        composer.prepareToPlay(48000.0,512);
        std::set<uint64_t> skeletons;
        std::set<uint64_t> generationSeeds;
        std::set<uint64_t> normalizedProgressions;
        for(int i=0;i<8;++i)
        {
            composer.generateTrack(prompt);
            auto song=composer.arrangementSnapshot();
            if(!song)return fail("same-prompt generation returned no arrangement");
            if(!generationSeeds.insert(composer.getSongGenerationSeed()).second)
                return fail("GENERATE TRACK retained the previous song after novelty rejection");
            const auto* liveLead=laneNamed(*song,"LEAD");
            if(!liveLead)return fail("generated song missing LEAD for live SoundDNA sync");
            if(composer.getSelectedLane()!=9||composer.currentPatch().seed!=liveLead->sound.seed)
                return fail("selected Piano Roll MIDI is not synced to the generated LEAD SoundDNA");
            const auto hash=leadSkeletonHash(*song);
            if(hash==0||!skeletons.insert(hash).second)
                return fail("GENERATE TRACK recycled a previous melody skeleton");
            const auto progressionHash=normalizedProgressionHash(*song);
            if(progressionHash==0||!normalizedProgressions.insert(progressionHash).second)
                return fail("GENERATE TRACK disguised a recycled root progression with new voicing");
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

    // v2.1 drum-only regeneration must preserve the current song architecture
    // and every non-drum lane while still producing genuinely fresh drums.
    {
        SonaraAudioProcessor drumEditor;
        drumEditor.prepareToPlay(48000.0,512);
        drumEditor.generateTrack(prompt);
        auto drumBefore=drumEditor.arrangementSnapshot();
        if(!drumBefore)return fail("drum-only test missing base arrangement");

        const int barsBefore=drumBefore->getBars();
        const auto sectionsBefore=drumBefore->getSections();
        std::vector<uint64_t> nonDrumHashes;
        std::vector<uint64_t> drumHashes;
        for(size_t i=0;i<drumBefore->getLanes().size();++i)
        {
            const auto h=laneHash(drumBefore->getLanes()[i]);
            if(i<4)drumHashes.push_back(h);
            else nonDrumHashes.push_back(h);
        }

        drumEditor.regenerateDrums("punchier festival drums with tighter hats and stronger transitions");
        auto drumAfter=drumEditor.arrangementSnapshot();
        if(!drumAfter)return fail("drum-only regeneration returned no arrangement");
        if(drumAfter->getBars()!=barsBefore)return fail("GENERATE DRUMS changed song length");
        if(drumAfter->getSections().size()!=sectionsBefore.size())
            return fail("GENERATE DRUMS changed section count");
        for(size_t i=0;i<sectionsBefore.size();++i)
        {
            const auto& a=sectionsBefore[i];
            const auto& b=drumAfter->getSections()[i];
            if(a.name!=b.name||a.startBar!=b.startBar||a.bars!=b.bars)
                return fail("GENERATE DRUMS changed existing song structure");
        }

        if(drumAfter->getLanes().size()<4+nonDrumHashes.size())
            return fail("GENERATE DRUMS removed arrangement lanes");
        for(size_t i=0;i<nonDrumHashes.size();++i)
            if(laneHash(drumAfter->getLanes()[i+4])!=nonDrumHashes[i])
                return fail("GENERATE DRUMS changed a non-drum lane or SoundDNA");

        bool freshDrums=false;
        for(size_t i=0;i<drumHashes.size()&&i<4;++i)
            freshDrums|=laneHash(drumAfter->getLanes()[i])!=drumHashes[i];
        if(!freshDrums)return fail("GENERATE DRUMS did not create fresh drums");
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

    // Reference actions must fail truthfully before any reference exists and must
    // never publish a stale arrangement merely because the UI button was clicked.
    {
        SonaraAudioProcessor emptyReference;
        emptyReference.prepareToPlay(48000.0,512);
        if(emptyReference.resoundReference("lead")||emptyReference.rebuildInstrumentalFromReference("song"))
            return fail("reference actions reported success without a usable reference");
        if(emptyReference.arrangementSnapshot())
            return fail("failed reference action published stale/phantom arrangement");
    }

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
    if(!randomizer.resoundReference("glassy emotional lead, wide but controlled"))
        return fail("RESOUND reported success path unavailable after valid MIDI reference");
    const auto arrangementBeforeReferenceRebuild=randomizer.arrangementSnapshot();
    if(!randomizer.rebuildInstrumentalFromReference("emotional progressive house, strong drums, warm chords"))
        return fail("reference REBUILD failed after valid MIDI import");
    const auto rebuiltReferenceSong=randomizer.arrangementSnapshot();
    if(!rebuiltReferenceSong||rebuiltReferenceSong==arrangementBeforeReferenceRebuild)
        return fail("reference REBUILD did not publish a genuinely new arrangement");
    const auto rebuiltLead=std::find_if(rebuiltReferenceSong->getLanes().begin(),rebuiltReferenceSong->getLanes().end(),
        [](const auto& lane){return lane.name=="LEAD";});
    const auto rebuiltBass=std::find_if(rebuiltReferenceSong->getLanes().begin(),rebuiltReferenceSong->getLanes().end(),
        [](const auto& lane){return lane.name=="BASS";});
    if(rebuiltLead==rebuiltReferenceSong->getLanes().end()||rebuiltLead->notes.empty()
       ||rebuiltBass==rebuiltReferenceSong->getLanes().end()||rebuiltBass->notes.empty())
        return fail("reference REBUILD did not create lead + backing arrangement");
    if(!randomizer.generationStatus.containsIgnoreCase("rebuilt"))
        return fail("reference REBUILD did not expose truthful completion state");
    auto extracted=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonara-reference-extracted",".mid");
    if(!randomizer.writeReferenceMidiFile(extracted)||extracted.getSize()<64)return fail("reference MIDI re-export failed");

    auto resoundWav=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonara-reference-resound",".wav");
    if(!randomizer.exportReferenceAudio(resoundWav)||resoundWav.getSize()<4096)
        return fail("RESOUND WAV export failed");
    juce::AudioFormatManager resoundFormats;resoundFormats.registerBasicFormats();
    auto resoundReader=resoundFormats.createReaderFor(resoundWav);
    if(!resoundReader||resoundReader->lengthInSamples<1024)
        return fail("RESOUND WAV could not be reopened");
    juce::AudioBuffer<float> resoundAudio(2,(int)std::min<juce::int64>(resoundReader->lengthInSamples,44100*4));
    resoundAudio.clear();
    if(!resoundReader->read(&resoundAudio,0,resoundAudio.getNumSamples(),0,true,true))
        return fail("RESOUND WAV readback failed");
    double resoundEnergy=0.0;float resoundPeak=0.f;
    for(int ch=0;ch<resoundAudio.getNumChannels();++ch)
        for(int i=0;i<resoundAudio.getNumSamples();++i)
        {
            const float x=resoundAudio.getSample(ch,i);
            if(!std::isfinite(x))return fail("RESOUND WAV contained non-finite audio");
            resoundPeak=juce::jmax(resoundPeak,std::abs(x));
            resoundEnergy+=(double)x*x;
        }
    if(resoundEnergy<=1.0e-7||resoundPeak>sonara::mixpolicy::masterCeiling()+.01f)
        return fail("RESOUND WAV was silent or exceeded safety ceiling");

    referenceMidi.deleteFile();extracted.deleteFile();resoundWav.deleteFile();

    // MIDI export paths.
    auto fullMidi=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonara-playback-full",".mid");
    if(!randomizer.writeArrangementMidiFile(fullMidi)||fullMidi.getSize()<512)return fail("full MIDI export failed");
    {
        juce::FileInputStream fullStream(fullMidi);
        juce::MidiFile fullFile;
        if(!fullStream.openedOk()||!fullFile.readFrom(fullStream))
            return fail("full song MIDI could not be parsed");
        const int expectedTracks=1+(int)surprised->getLanes().size(); // conductor + every arrangement lane
        if(fullFile.getNumTracks()!=expectedTracks)
            return fail("full song MIDI does not contain every generated lane as a separate track");
        const auto* conductor=fullFile.getTrack(0);
        if(conductor==nullptr||conductor->getNumEvents()==0)
            return fail("full song MIDI conductor/tempo track missing");
    }
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
    if(restored.getSelectedLane()!=subIndex
       ||restored.currentPatch().seed!=restored.arrangementSnapshot()->getLanes()[(size_t)subIndex].sound.seed)
        return fail("selected lane / live SoundDNA did not survive plugin state round-trip");
    if(!restored.hasReference())
        return fail("reference analysis did not survive plugin state round-trip");
    if(restored.getCurrentSongPrompt().isEmpty())
        return fail("song prompt did not survive plugin state round-trip");

    auto projectFile=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonara-project-roundtrip",".sonara");
    if(!randomizer.saveProject(projectFile))return fail("project save failed");
    SonaraAudioProcessor projectLoaded;
    projectLoaded.prepareToPlay(48000.0,512);
    if(!projectLoaded.loadProject(projectFile))return fail("project load failed");
    const auto projectMix=projectLoaded.getLaneMix(subIndex);
    if(std::abs(projectMix.level-.73f)>.001f||!projectLoaded.arrangementSnapshot())
        return fail("project round-trip lost mixer or arrangement");
    if(projectLoaded.getSelectedLane()!=subIndex
       ||projectLoaded.currentPatch().seed!=projectLoaded.arrangementSnapshot()->getLanes()[(size_t)subIndex].sound.seed)
        return fail("project round-trip lost selected lane SoundDNA");
    if(!projectLoaded.hasReference())
        return fail("project round-trip lost reference analysis");
    if(projectLoaded.getCurrentSongPrompt().isEmpty())
        return fail("project round-trip lost source prompt");

    auto soundOnlyProject=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonara-sound-only",".sonaraproject");
    SonaraAudioProcessor soundOnly;
    soundOnly.prepareToPlay(48000.0,512);
    soundOnly.generatePatch("warm clean pluck");
    if(!soundOnly.saveProject(soundOnlyProject))
        return fail("sound-only project save failed");
    if(!projectLoaded.loadProject(soundOnlyProject))
        return fail("sound-only project load failed");
    if(projectLoaded.arrangementSnapshot()||projectLoaded.hasReference())
        return fail("loading sound-only project retained stale song/reference state");

    juce::MemoryBlock soundOnlyState;
    soundOnly.getStateInformation(soundOnlyState);
    SonaraAudioProcessor staleStateTarget;
    staleStateTarget.prepareToPlay(48000.0,512);
    staleStateTarget.setStateInformation(state.getData(),(int)state.getSize());
    if(!staleStateTarget.arrangementSnapshot()||!staleStateTarget.hasReference())
        return fail("stale-state fixture did not restore full song/reference state");
    staleStateTarget.setStateInformation(soundOnlyState.getData(),(int)soundOnlyState.getSize());
    if(staleStateTarget.arrangementSnapshot()||staleStateTarget.hasReference())
        return fail("loading sound-only plugin state retained stale song/reference state");

    if(randomizer.analyseReferenceFile(juce::File{})||randomizer.hasReference())
        return fail("failed reference load retained stale reference state");

    soundOnlyProject.deleteFile();
    projectFile.deleteFile();

    auto subMidi=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonara-playback-sub",".mid");
    if(!randomizer.writeSelectedLaneMidiFile(subMidi)||subMidi.getSize()<64)return fail("selected SUB MIDI export failed");

    auto leadMidi=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonara-playback-lead",".mid");
    const int selectedBeforeLeadExport=randomizer.getSelectedLane();
    if(!randomizer.writeLeadMidiFile(leadMidi)||leadMidi.getSize()<64||leadMidi.getSize()>=fullMidi.getSize())
        return fail("dedicated LEAD MIDI export failed or still contains the full arrangement");
    if(randomizer.getSelectedLane()!=selectedBeforeLeadExport)
        return fail("LEAD MIDI export changed the selected lane");
    juce::FileInputStream leadStream(leadMidi);
    juce::MidiFile leadFile;
    if(!leadStream.openedOk()||!leadFile.readFrom(leadStream)||leadFile.getNumTracks()!=1)
        return fail("dedicated LEAD MIDI could not be parsed as one track");
    const auto* leadTrack=leadFile.getTrack(0);
    if(!leadTrack)return fail("dedicated LEAD MIDI track missing");
    for(int i=0;i<leadTrack->getNumEvents();++i)
    {
        const auto message=leadTrack->getEventPointer(i)->message;
        if(message.isNoteOn()&&message.getNoteNumber()<52)
            return fail("dedicated LEAD MIDI contains low support-lane notes");
    }

    randomizer.startChorusPreview();
    if(!randomizer.isSongPlaying()||randomizer.currentSectionName()!="CHORUS")
        return fail("PLAY CHORUS did not jump to the first CHORUS section");
    randomizer.startDropPreview();
    if(!randomizer.isSongPlaying()||randomizer.currentSectionName()!="DROP")
        return fail("PLAY DROP did not jump to the first DROP section");
    randomizer.stopSongPreview();

    fullMidi.deleteFile();subMidi.deleteFile();leadMidi.deleteFile();

    // v3.3 shared mix policy invariants. Preview and offline export both call these
    // exact helpers, preventing the duplicated constants from drifting apart again.
    if(sonara::mixpolicy::stereoWidth(1,1.5f,1.f)!=0.f)
        return fail("v3.3 SUB is no longer forced mono");
    if(sonara::mixpolicy::stereoWidth(0,1.5f,1.f)>.181f)
        return fail("v3.3 BASS stereo safety widened too far");
    if(sonara::mixpolicy::baseFxSend(0)!=0.f||sonara::mixpolicy::baseFxSend(1)!=0.f)
        return fail("v3.3 low end entered shared reverb send");
    if(sonara::mixpolicy::subLowPassHz()>120.f
       ||sonara::mixpolicy::energyGain(1,1.f)>=sonara::mixpolicy::energyGain(5,1.f))
        return fail("v3.3 low-end buildup guard regressed");
    {
        float x1=0.f,y1=0.f;
        const float y=sonara::mixpolicy::processMasterSample(1000.f,x1,y1,48000.0);
        if(!std::isfinite(y)||std::abs(y)>sonara::mixpolicy::masterCeiling()+1.0e-5f)
            return fail("v3.3 master safety ceiling regressed");
    }

    // Shortened offline export exercises the real full-mix/stem renderers without a long CI file.
    auto shortTree=surprised->toValueTree();
    shortTree.setProperty("bars",4,nullptr);
    auto shortSong=sonara::SongArrangement::fromValueTree(shortTree);
    sonara::AudioExporter exporter;
    auto wav=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonara-full-mix-test",".wav");
    if(!exporter.renderFullMix(shortSong,wav,44100.0,{})||wav.getSize()<4096)return fail("full mix WAV export failed");

    juce::AudioFormatManager exportFormats;exportFormats.registerBasicFormats();
    auto mixReader=exportFormats.createReaderFor(wav);
    if(!mixReader)return fail("full mix WAV could not be reopened");
    const int inspectSamples=(int)std::min<juce::int64>(mixReader->lengthInSamples,44100*12);
    juce::AudioBuffer<float> renderedMix(2,juce::jmax(1,inspectSamples));
    renderedMix.clear();
    if(!mixReader->read(&renderedMix,0,inspectSamples,0,true,true))
        return fail("full mix WAV readback failed");
    double mixEnergy=0.0;float mixPeak=0.f;
    for(int ch=0;ch<renderedMix.getNumChannels();++ch)
        for(int s=0;s<renderedMix.getNumSamples();++s)
        {
            const float x=renderedMix.getSample(ch,s);
            mixPeak=juce::jmax(mixPeak,std::abs(x));mixEnergy+=(double)x*x;
        }
    const double mixRms=std::sqrt(mixEnergy/
        juce::jmax(1,renderedMix.getNumChannels()*renderedMix.getNumSamples()));
    if(mixRms<.018||mixPeak<.12f||mixPeak>.965f)
        return fail("v1.7 exported full mix is too quiet or unsafe");

    auto stems=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("sonara-stems-test","");
    if(!exporter.renderAllStems(shortSong,stems,44100.0,{}))return fail("stem export failed");
    int wavCount=0;for(const auto& file:stems.findChildFiles(juce::File::findFiles,false,"*.wav")){++wavCount;file.deleteFile();}
    stems.deleteRecursively();wav.deleteFile();
    if(wavCount!=12)return fail("stem export did not produce 12 lane WAVs");

    std::cout<<"SONARA processor playback, CPU, seek, randomize, surprise and export tests passed\n";
    return 0;
}
