#include "PluginProcessor.h"
#ifndef SONARA_HEADLESS_TEST
#include "PluginEditor.h"
#endif
#include <algorithm>
#include <cmath>

namespace {
uint64_t scrambleSongSeed(uint64_t x) noexcept
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

const sonara::ArrangementLane* laneNamed(const sonara::SongArrangement& a,const juce::String& name)
{
    for(const auto& lane:a.getLanes())if(lane.name==name)return &lane;
    return nullptr;
}

std::vector<int> melodyFingerprint(const sonara::SongArrangement& a)
{
    return a.getMelodyFingerprint();
}

float fingerprintSimilarity(const std::vector<int>& a,const std::vector<int>& b)
{
    if(a.empty()||b.empty())return 0.f;
    constexpr int stride=7;
    const int aNotes=((int)a.size()-1)/stride;
    const int bNotes=((int)b.size()-1)/stride;
    const int n=juce::jmin(aNotes,bNotes);
    if(n<8)return 0.f;

    float pitch=0.f,interval=0.f,contour=0.f,onset=0.f,gap=0.f,length=0.f;
    for(int i=0;i<n;++i)
    {
        const int ai=i*stride,bi=i*stride;
        if(a[(size_t)ai]==b[(size_t)bi])pitch+=1.f;
        if(std::abs(a[(size_t)ai+1]-b[(size_t)bi+1])<=1)interval+=1.f;
        if(a[(size_t)ai+2]==b[(size_t)bi+2])contour+=1.f;
        if(std::abs(a[(size_t)ai+4]-b[(size_t)bi+4])<=1)onset+=1.f;
        if(std::abs(a[(size_t)ai+5]-b[(size_t)bi+5])<=1)gap+=1.f;
        if(std::abs(a[(size_t)ai+6]-b[(size_t)bi+6])<=1)length+=1.f;
    }

    const float inv=1.f/(float)n;
    pitch*=inv;interval*=inv;contour*=inv;onset*=inv;gap*=inv;length*=inv;
    const float countRatio=(float)juce::jmin(aNotes,bNotes)/(float)juce::jmax(1,juce::jmax(aNotes,bNotes));

    // Human listeners notice a reused rhythm skeleton or interval contour even if
    // notes are transposed. Treat either one as "too similar" instead of averaging
    // it away with unrelated differences.
    const float rhythmSkeleton=(onset*.44f+gap*.36f+length*.20f)*(.82f+.18f*countRatio);
    const float contourSkeleton=(contour*.58f+interval*.42f)*(.84f+.16f*countRatio);
    const float pitchIdentity=(pitch*.75f+interval*.25f)*(.82f+.18f*countRatio);
    const float combined=(pitch*.16f+interval*.20f+contour*.18f+onset*.18f+gap*.16f+length*.12f)
                         *(.84f+.16f*countRatio);

    return juce::jlimit(0.f,1.f,juce::jmax(juce::jmax(rhythmSkeleton,contourSkeleton),
                                           juce::jmax(pitchIdentity,combined)));
}

float melodyRhythmSimilarity(const std::vector<int>& a,const std::vector<int>& b)
{
    if(a.empty()||b.empty())return 0.f;
    constexpr int stride=7;
    const int n=juce::jmin(((int)a.size()-1)/stride,((int)b.size()-1)/stride);
    if(n<8)return 0.f;
    float score=0.f;
    for(int i=0;i<n;++i)
    {
        const int ai=i*stride,bi=i*stride;
        if(std::abs(a[(size_t)ai+4]-b[(size_t)bi+4])<=1)score+=.42f;
        if(std::abs(a[(size_t)ai+5]-b[(size_t)bi+5])<=1)score+=.36f;
        if(std::abs(a[(size_t)ai+6]-b[(size_t)bi+6])<=1)score+=.22f;
    }
    const float countRatio=(float)juce::jmin(((int)a.size()-1)/stride,((int)b.size()-1)/stride)
                          /(float)juce::jmax(1,juce::jmax(((int)a.size()-1)/stride,((int)b.size()-1)/stride));
    return juce::jlimit(0.f,1.f,(score/(float)n)*(.84f+.16f*countRatio));
}

std::vector<int> lanePatternFingerprint(const sonara::SongArrangement& song,const juce::String& laneName)
{
    std::vector<int> fp;
    const auto* lane=laneNamed(song,laneName);
    if(lane==nullptr||lane->notes.empty())return fp;
    const int limit=juce::jmin(128,(int)lane->notes.size());
    fp.reserve((size_t)(limit*5+2));
    fp.push_back(lane->drums?1:0);
    int previous=lane->notes.front().note;
    double previousBeat=lane->notes.front().beat;
    for(int i=0;i<limit;++i)
    {
        const auto& note=lane->notes[(size_t)i];
        fp.push_back(lane->drums?note.note:note.note-song.getRootMidi());
        fp.push_back(i==0?0:juce::jlimit(-24,24,note.note-previous));
        fp.push_back((int)std::llround(std::fmod(juce::jmax(0.0,note.beat),16.0)*4.0));
        fp.push_back(i==0?0:juce::jlimit(0,64,(int)std::llround((note.beat-previousBeat)*8.0)));
        fp.push_back(juce::jlimit(1,32,(int)std::llround(note.length*8.0)));
        previous=note.note;
        previousBeat=note.beat;
    }
    fp.push_back((int)lane->notes.size());
    return fp;
}

std::vector<int> drumFingerprint(const sonara::SongArrangement& song)
{
    std::vector<int> fp;
    for(const auto& name:{juce::String("KICK"),juce::String("SNARE / CLAP"),juce::String("HATS"),juce::String("PERCUSSION")})
    {
        const auto lane=lanePatternFingerprint(song,name);
        fp.push_back(-1000-(int)fp.size());
        fp.insert(fp.end(),lane.begin(),lane.end());
    }
    return fp;
}

float flatFingerprintSimilarity(const std::vector<int>& a,const std::vector<int>& b)
{
    if(a.empty()||b.empty())return 0.f;
    const int n=juce::jmin((int)a.size(),(int)b.size());
    float exact=0.f,near=0.f;
    for(int i=0;i<n;++i)
    {
        if(a[(size_t)i]==b[(size_t)i])exact+=1.f;
        else if(std::abs(a[(size_t)i]-b[(size_t)i])==1)near+=1.f;
    }
    const float countRatio=(float)n/(float)juce::jmax(a.size(),b.size());
    return juce::jlimit(0.f,1.f,((exact+.30f*near)/(float)n)*(.82f+.18f*countRatio));
}

float harmonyQualityScore(const sonara::SongArrangement& song)
{
    const auto progression=song.getProgressionFingerprint();
    if(progression.size()<5)return 0.f;
    const int n=juce::jlimit(1,8,progression[0]);
    std::array<bool,7> used{};
    int unique=0,consecutiveRepeats=0;
    for(int i=0;i<n;++i)
    {
        const int degree=juce::jlimit(0,6,progression[(size_t)(2+i*3)]);
        if(!used[(size_t)degree]){used[(size_t)degree]=true;++unique;}
        if(i>0&&degree==progression[(size_t)(2+(i-1)*3)])++consecutiveRepeats;
    }
    const float variety=n<=2?1.f:juce::jlimit(0.f,1.f,(float)unique/(float)juce::jmin(n,5));
    const float repetition=1.f-(float)consecutiveRepeats/(float)juce::jmax(1,n-1);
    const bool tonalCentre=used[0];
    return juce::jlimit(0.f,1.f,.42f*variety+.34f*repetition+.24f*(tonalCentre?1.f:.55f));
}


const sonara::ArrangementSection* sectionNamed(const sonara::SongArrangement& song,const juce::String& name)
{
    for(const auto& s:song.getSections())if(s.name==name)return &s;
    return nullptr;
}

float laneDensityInSection(const sonara::SongArrangement& song,const juce::String& laneName,
                           const sonara::ArrangementSection* section)
{
    if(section==nullptr)return 0.f;
    const auto* lane=laneNamed(song,laneName);
    if(lane==nullptr)return 0.f;
    const double start=section->startBar*4.0;
    const double end=(section->startBar+section->bars)*4.0;
    int count=0;
    for(const auto& n:lane->notes)if(n.beat>=start&&n.beat<end)++count;
    return count/(float)juce::jmax(1,section->bars);
}

float sectionRhythmOverlap(const sonara::ArrangementLane* lane,
                           const sonara::ArrangementSection* a,
                           const sonara::ArrangementSection* b)
{
    if(lane==nullptr||a==nullptr||b==nullptr)return 0.f;
    std::array<bool,64> first{},second{};
    const double aStart=a->startBar*4.0,bStart=b->startBar*4.0;
    const double aEnd=aStart+8.0,bEnd=bStart+8.0;
    for(const auto& n:lane->notes)
    {
        if(n.beat>=aStart&&n.beat<aEnd)
        {
            const int q=juce::jlimit(0,63,(int)std::llround((n.beat-aStart)*8.0));
            first[(size_t)q]=true;
        }
        if(n.beat>=bStart&&n.beat<bEnd)
        {
            const int q=juce::jlimit(0,63,(int)std::llround((n.beat-bStart)*8.0));
            second[(size_t)q]=true;
        }
    }
    int aCount=0,bCount=0,common=0;
    for(size_t i=0;i<first.size();++i)
    {
        if(first[i])++aCount;
        if(second[i])++bCount;
        if(first[i]&&second[i])++common;
    }
    if(aCount==0||bCount==0)return 0.f;
    return juce::jlimit(0.f,1.f,common/(float)juce::jmax(1,juce::jmin(aCount,bCount)));
}

float internalTwoBarRepeat(const sonara::ArrangementLane* lane,
                           const sonara::ArrangementSection* section)
{
    if(lane==nullptr||section==nullptr||section->bars<4)return .5f;
    sonara::ArrangementSection second=*section;
    second.startBar+=2;
    return sectionRhythmOverlap(lane,section,&second);
}

float productionQualityScore(const sonara::SongArrangement& song)
{
    const auto* drop=sectionNamed(song,"DROP");
    const auto* breakdown=sectionNamed(song,"BREAKDOWN");
    const auto* finalHook=sectionNamed(song,"FINAL HOOK");
    const auto* lead=laneNamed(song,"LEAD");
    if(drop==nullptr||lead==nullptr)return 0.f;

    const float motifRepeat=internalTwoBarRepeat(lead,drop);
    const float hookRecall=finalHook?sectionRhythmOverlap(lead,drop,finalHook):.5f;

    const float dropKick=laneDensityInSection(song,"KICK",drop);
    const float breakKick=laneDensityInSection(song,"KICK",breakdown);
    const float drumContrast=juce::jlimit(0.f,1.f,(dropKick-breakKick+.5f)/4.0f);

    const float dropBass=laneDensityInSection(song,"BASS",drop);
    const float breakBass=laneDensityInSection(song,"BASS",breakdown);
    const float bassContrast=juce::jlimit(0.f,1.f,(dropBass-breakBass+.5f)/5.0f);

    const float dropLead=laneDensityInSection(song,"LEAD",drop);
    const float breakLead=laneDensityInSection(song,"LEAD",breakdown);
    const float leadContrast=juce::jlimit(0.f,1.f,(dropLead-breakLead+.5f)/5.0f);

    // Good EDM drops are neither empty nor note soup. Reward a useful density
    // window while keeping breakdown-to-drop contrast obvious.
    const float leadDensityShape=juce::jlimit(0.f,1.f,1.f-std::abs(dropLead-4.6f)/4.6f);
    const float kickDensityShape=juce::jlimit(0.f,1.f,1.f-std::abs(dropKick-4.2f)/4.2f);

    // Reward recognisable motif memory without demanding exact cloning.
    const float repeatShape=juce::jlimit(0.f,1.f,1.f-std::abs(motifRepeat-.72f)/.72f);
    const float recallShape=juce::jlimit(0.f,1.f,1.f-std::abs(hookRecall-.68f)/.68f);
    return juce::jlimit(0.f,1.f,
        repeatShape*.22f+recallShape*.18f+drumContrast*.18f+bassContrast*.14f+
        leadContrast*.12f+leadDensityShape*.10f+kickDensityShape*.06f);
}

float promptCompositionMatch(const juce::String& prompt,const sonara::SongArrangement& song)
{
    const auto p=prompt.toLowerCase();
    const int n=song.getHarmonyProgressionLength();
    float score=.82f;
    if((p.contains("tech house")||p.contains("minimal house"))&&n>3)score-=.30f;
    if((p.contains("cinematic")||p.contains("film"))&&n<6)score-=.24f;
    if((p.contains("pop")||p.contains("radio"))&&(n<3||n>6))score-=.18f;
    if((p.contains("progressive")||p.contains("melodic house"))&&(n<3||n>8))score-=.20f;
    if(p.contains("minor")&&!song.isMinor())score-=.30f;
    if(p.contains("major")&&song.isMinor())score-=.30f;
    if(p.contains("emotional")&&song.getMelodyArchetypeName()=="SPARSE MOTIF")score-=.08f;
    return juce::jlimit(0.f,1.f,score);
}

float melodyQualityScore(const sonara::SongArrangement& song)
{
    const auto* lead=laneNamed(song,"LEAD");
    if(lead==nullptr||lead->notes.size()<20)return 0.f;

    int hugeLeaps=0,repeatedRun=1,maxRepeated=1;
    int minNote=127,maxNote=0;
    double totalLength=0.0;
    std::array<bool,33> gapKinds{};
    int gapVariety=0;

    for(size_t i=0;i<lead->notes.size();++i)
    {
        const auto& n=lead->notes[i];
        minNote=juce::jmin(minNote,n.note);
        maxNote=juce::jmax(maxNote,n.note);
        totalLength+=n.length;

        if(i>0)
        {
            const auto& p=lead->notes[i-1];
            if(std::abs(n.note-p.note)>12)++hugeLeaps;
            repeatedRun=(n.note==p.note)?repeatedRun+1:1;
            maxRepeated=juce::jmax(maxRepeated,repeatedRun);
            const int q=juce::jlimit(0,32,(int)std::llround((n.beat-p.beat)*8.0));
            if(!gapKinds[(size_t)q]){gapKinds[(size_t)q]=true;++gapVariety;}
        }
    }

    const float leapRatio=(float)hugeLeaps/(float)juce::jmax<size_t>(1,lead->notes.size()-1);
    const float rangeScore=juce::jlimit(0.f,1.f,1.f-std::abs((float)(maxNote-minNote)-24.f)/36.f);
    const float leapScore=juce::jlimit(0.f,1.f,1.f-leapRatio*3.8f);
    const float repeatScore=maxRepeated<=2?1.f:(maxRepeated==3?.78f:(maxRepeated==4?.50f:.12f));
    const float rhythmScore=juce::jlimit(0.f,1.f,(float)gapVariety/6.f);
    const float avgLength=(float)(totalLength/(double)lead->notes.size());
    const float lengthScore=juce::jlimit(0.f,1.f,1.f-std::abs(avgLength-.38f)/.75f);
    const float density=(float)lead->notes.size()/(float)juce::jmax(1,song.getBars());
    const float densityScore=juce::jlimit(0.f,1.f,1.f-std::abs(density-3.7f)/4.5f);

    return juce::jlimit(0.f,1.f,
        rangeScore*.17f+leapScore*.23f+repeatScore*.18f+
        rhythmScore*.18f+lengthScore*.10f+densityScore*.14f);
}

juce::ValueTree makeLaneMixTree(const SonaraAudioProcessor& p)
{
    juce::ValueTree root("LANE_MIX");
    root.setProperty("schema",1,nullptr);
    for(int i=0;i<12;++i)
    {
        const auto m=p.getLaneMix(i);
        juce::ValueTree lane("MIX");
        lane.setProperty("index",i,nullptr);
        lane.setProperty("level",m.level,nullptr);
        lane.setProperty("pan",m.pan,nullptr);
        lane.setProperty("width",m.width,nullptr);
        lane.setProperty("fx",m.fxSend,nullptr);
        root.addChild(lane,-1,nullptr);
    }
    return root;
}

void applyLaneMixTree(SonaraAudioProcessor& p,const juce::ValueTree& root)
{
    if(!root.isValid()||root.getType().toString()!="LANE_MIX")return;
    for(int i=0;i<root.getNumChildren();++i)
    {
        const auto lane=root.getChild(i);
        const int index=juce::jlimit(0,11,(int)lane.getProperty("index",i));
        p.setLaneMix(index,SonaraAudioProcessor::LaneMixParameter::level,(float)lane.getProperty("level",1.f));
        p.setLaneMix(index,SonaraAudioProcessor::LaneMixParameter::pan,(float)lane.getProperty("pan",0.f));
        p.setLaneMix(index,SonaraAudioProcessor::LaneMixParameter::width,(float)lane.getProperty("width",1.f));
        p.setLaneMix(index,SonaraAudioProcessor::LaneMixParameter::fxSend,(float)lane.getProperty("fx",1.f));
    }
}
}

SonaraAudioProcessor::SonaraAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    sessionSalt = static_cast<uint64_t>(juce::Random::getSystemRandom().nextInt64())
                ^ static_cast<uint64_t>(juce::Time::getHighResolutionTicks());
    for(auto& e:songEngines)e.setLowCpuMode(true);
    // Lane order after drums: BASS, SUB, CHORDS, PLUCK, PAD, LEAD, COUNTER, FX.
    static constexpr int voiceBudget[musicalLaneCount]={1,1,3,2,3,3,2,1};
    for(int i=0;i<musicalLaneCount;++i)songEngines[(size_t)i].setVoiceLimit(voiceBudget[i]);
    for(int i=0;i<12;++i){laneMixLevel[(size_t)i].store(1.f);laneMixPan[(size_t)i].store(0.f);laneMixWidth[(size_t)i].store(1.f);laneMixFx[(size_t)i].store(1.f);}
    patchHistory.push_back(engine.patch());
    historyIndex = 0;
}

void SonaraAudioProcessor::prepareToPlay(double sr, int bs)
{
    previewSampleRate = juce::jmax(8000.0, sr);
    maximumBlockSize = juce::jmax(8192, bs);
    previewLengthSamples = (int64_t) std::llround(previewSampleRate * (60.0 / previewBpm) * 16.0);

    engine.prepare(sr, maximumBlockSize, getTotalNumOutputChannels());
    for (auto& e : songEngines) e.prepare(sr, maximumBlockSize, getTotalNumOutputChannels());
    drumSynth.prepare(sr);
    songReverb.reset();
    juce::Reverb::Parameters rp;
    rp.roomSize=.31f;rp.damping=.52f;rp.wetLevel=.22f;rp.dryLevel=0.f;rp.width=.82f;
    songReverb.setParameters(rp);

    const int channels = juce::jmax(1, getTotalNumOutputChannels());
    for (auto& b : songScratch)
    {
        b.setSize(channels, maximumBlockSize, false, false, true);
        b.clear();
    }
    songFxBus.setSize(channels,maximumBlockSize,false,false,true);
    songFxBus.clear();
    songDuckEnvelope.assign((size_t)maximumBlockSize,0.f);
    songDuckState=0.f;
    for (auto& m : songMidi) m.ensureSize(16384);
    for (auto& x : laneHpX) x.fill(0.f);
    for (auto& y : laneHpY) y.fill(0.f);
    for (auto& x : laneLpState) x.fill(0.f);
    masterHpX.fill(0.f); masterHpY.fill(0.f);
}

bool SonaraAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SonaraAudioProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& m)
{
    juce::ScopedNoDenormals noDenormals;
    b.clear();

    if (songPlaying.load(std::memory_order_acquire))
    {
        m.clear();
        renderSongBlock(b, b.getNumSamples());
        return;
    }

    injectPreviewMidi(m, b.getNumSamples());
    engine.render(b, m);

    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample(c, i, std::tanh(b.getSample(c, i) * .98f));
}

void SonaraAudioProcessor::startPreview()
{
    stopSongPreview();
    const double beats = referenceMelodyPreview && referenceLoaded && !reference.melody.empty() ? juce::jmax(1.0, reference.melodyBeats()) : 16.0;
    previewLengthSamples = (int64_t) std::llround(previewSampleRate * (60.0 / previewBpm) * beats);
    previewSample.store(0);
    previewPlaying.store(true);
    generationStatus = referenceMelodyPreview ? "RESOUND preview playing" : "Sound preview playing";
}

void SonaraAudioProcessor::stopPreview()
{
    previewPlaying.store(false);
    previewSample.store(0);
    engine.allNotesOff();
    generationStatus = "Sound preview stopped";
}

void SonaraAudioProcessor::injectPreviewMidi(juce::MidiBuffer& m, int numSamples)
{
    if (!previewPlaying.load() || numSamples <= 0) return;
    const int64_t start = previewSample.load(), end = start + numSamples;

    if (referenceMelodyPreview && referenceLoaded && !reference.melody.empty())
    {
        const double spb = previewSampleRate * 60.0 / previewBpm;
        const double startBeat = (double)start / spb, endBeat = (double)end / spb;
        auto it = std::lower_bound(reference.melody.begin(), reference.melody.end(), startBeat - 4.5,
                                   [](const sonara::ReferenceNote& n, double beat){ return n.beat < beat; });
        for (; it != reference.melody.end() && it->beat <= endBeat; ++it)
        {
            const int64_t on = (int64_t)std::llround(it->beat * spb);
            const int64_t off = (int64_t)std::llround((it->beat + it->length) * spb);
            if (on >= start && on < end) m.addEvent(juce::MidiMessage::noteOn(1,it->midiNote,(juce::uint8)juce::jlimit(1,127,it->velocity)),(int)(on-start));
            if (off >= start && off < end) m.addEvent(juce::MidiMessage::noteOff(1,it->midiNote),(int)(off-start));
        }
    }
    else
    {
        const int notes[] = {60,63,67,70,72,70,67,63,60,63,67,75,74,70,67,63};
        const int64_t step = juce::jmax<int64_t>(1, previewLengthSamples / 16);
        for (int i = 0; i < 16; ++i)
        {
            const int64_t on = (int64_t) i * step;
            const int64_t off = juce::jmin<int64_t>(previewLengthSamples - 1, on + (step * 3) / 4);
            if (on >= start && on < end) m.addEvent(juce::MidiMessage::noteOn(1, notes[i], (juce::uint8)108), (int)(on - start));
            if (off >= start && off < end) m.addEvent(juce::MidiMessage::noteOff(1, notes[i]), (int)(off - start));
        }
    }

    previewSample.store(end);
    if (end >= previewLengthSamples) previewPlaying.store(false);
}

bool SonaraAudioProcessor::writePreviewMidiFile(const juce::File& destination) const
{
    if(referenceMelodyPreview && referenceLoaded && !reference.melody.empty())
        return referenceAnalyzer.writeMelodyMidi(reference,destination);

    juce::MidiFile mf; mf.setTicksPerQuarterNote(960); juce::MidiMessageSequence seq;
    auto tempo = juce::MidiMessage::tempoMetaEvent((int) std::llround(60000000.0 / previewBpm));tempo.setTimeStamp(0);seq.addEvent(tempo);
    auto name=juce::MidiMessage::textMetaEvent(3,"SONARA Sound Preview");name.setTimeStamp(0);seq.addEvent(name);
    const int notes[] = {60,63,67,70,72,70,67,63,60,63,67,75,74,70,67,63};
    for (int i = 0; i < 16; ++i){const double on=i*960.0,off=on+720.0;auto a=juce::MidiMessage::noteOn(1,notes[i],(juce::uint8)108);a.setTimeStamp(on);seq.addEvent(a);auto z=juce::MidiMessage::noteOff(1,notes[i]);z.setTimeStamp(off);seq.addEvent(z);}
    seq.updateMatchedPairs();mf.addTrack(seq);destination.deleteFile();juce::FileOutputStream out(destination);return out.openedOk()&&mf.writeTo(out);
}

void SonaraAudioProcessor::setPatchWithHistory(const sonara::SoundDNA& d)
{
    engine.setPatch(d);
    if (historyIndex + 1 < (int) patchHistory.size()) patchHistory.erase(patchHistory.begin() + historyIndex + 1, patchHistory.end());
    patchHistory.push_back(d);
    if (patchHistory.size() > 32) patchHistory.erase(patchHistory.begin());
    historyIndex = (int) patchHistory.size() - 1;
}

void SonaraAudioProcessor::generatePatch(const juce::String& p)
{
    stopPreview(); referenceMelodyPreview=false;
    const auto seed = (uint64_t) p.hashCode64()
                    ^ sessionSalt
                    ^ (++generationCounter * 0xd6e8feb86659fd93ULL)
                    ^ 0x534f554e445f444eULL;
    auto d = generator.generate(p, seed, [this](float x, const juce::String& s){ generationProgress.store(x); generationStatus = s; });
    d.name = "AI • " + p.substring(0, 26);
    setPatchWithHistory(d);
    generationStatus = "Sound ready • unique SoundDNA";
}

void SonaraAudioProcessor::mutatePatch()
{
    auto d = generator.mutate(engine.patch(), engine.patch().seed + (++generationCounter), .45f, locks,
                              [this](float x, const juce::String& s){ generationProgress.store(x); generationStatus = s; });
    setPatchWithHistory(d); generationStatus = "Mutation ready";
}

void SonaraAudioProcessor::generateSimilarPatch()
{
    const auto source = engine.patch();
    const auto seed = source.seed + (++generationCounter * 0x517cc1b727220a95ULL);
    auto d = generator.mutate(source, seed, .18f, locks,
                              [this](float x, const juce::String& s){ generationProgress.store(x); generationStatus = "Similar: " + s; });
    d.sourcePrompt = source.sourcePrompt;
    setPatchWithHistory(d); generationStatus = "Similar variation ready";
}

void SonaraAudioProcessor::generateVariation(int index)
{
    const float amount = .12f + .12f * (float) juce::jlimit(1, 4, index);
    auto d = generator.mutate(engine.patch(), engine.patch().seed + (++generationCounter * 0x94d049bb133111ebULL), amount, locks);
    d.name = "Variation " + juce::String(index);
    setPatchWithHistory(d); generationProgress.store(1.f); generationStatus = "Variation " + juce::String(index) + " ready";
}

void SonaraAudioProcessor::randomizePatch()
{
    const auto sourcePrompt = engine.patch().sourcePrompt.isNotEmpty() ? engine.patch().sourcePrompt : "experimental wide synth";
    generatePatch(sourcePrompt + " randomized texture");
}

void SonaraAudioProcessor::undoPatch(){ if(historyIndex>0){ --historyIndex; engine.setPatch(patchHistory[(size_t)historyIndex]); generationStatus="Undo"; } }
void SonaraAudioProcessor::redoPatch(){ if(historyIndex+1<(int)patchHistory.size()){ ++historyIndex; engine.setPatch(patchHistory[(size_t)historyIndex]); generationStatus="Redo"; } }
void SonaraAudioProcessor::captureA(){ patchA=engine.patch();hasA=true;generationStatus="A captured"; }
void SonaraAudioProcessor::captureB(){ patchB=engine.patch();hasB=true;generationStatus="B captured"; }
void SonaraAudioProcessor::recallA(){ if(hasA){ engine.setPatch(patchA);generationStatus="A recalled"; } }
void SonaraAudioProcessor::recallB(){ if(hasB){ engine.setPatch(patchB);generationStatus="B recalled"; } }

void SonaraAudioProcessor::generateTrack(const juce::String& prompt)
{
    stopPreview();stopSongPreview();
    generationProgress.store(.03f);
    generationStatus="Planning new harmony, melody and groove identities";

    const auto previous=arrangementSnapshot();
    const auto previousFingerprint=previous?melodyFingerprint(*previous):std::vector<int>{};
    const auto previousHarmony=previous?previous->getHarmonyFingerprint():std::vector<int>{};
    const auto previousBass=previous?lanePatternFingerprint(*previous,"BASS"):std::vector<int>{};
    const auto previousDrums=previous?drumFingerprint(*previous):std::vector<int>{};
    const auto previousPluck=previous?lanePatternFingerprint(*previous,"PLUCK"):std::vector<int>{};
    const auto previousStructure=previous?previous->getStructureFingerprint():std::vector<int>{};

    std::shared_ptr<sonara::SongArrangement> made;
    std::shared_ptr<sonara::SongArrangement> bestCandidate;
    std::vector<int> acceptedFingerprint,acceptedHarmony,acceptedBass,acceptedDrums,acceptedPluck,acceptedStructure;
    std::vector<int> bestFingerprint,bestHarmony,bestBass,bestDrums,bestPluck,bestStructure;
    uint64_t seed=0,bestSeed=0;
    float melodySimilarityMax=1.f,harmonySimilarityMax=1.f,wholeSimilarityMax=1.f;
    float bestMelodySimilarity=1.f,bestHarmonySimilarity=1.f,bestWholeSimilarity=1.f;
    float bestQuality=0.f,bestPromptMatch=0.f,bestCombined=-1.f;

    const auto maxFlatSimilarity=[](const std::vector<int>& candidate,
                                    const std::vector<int>& previousValue,
                                    const std::deque<std::vector<int>>& history)
    {
        float value=previousValue.empty()?0.f:flatFingerprintSimilarity(previousValue,candidate);
        for(const auto& old:history)value=juce::jmax(value,flatFingerprintSimilarity(old,candidate));
        return value;
    };

    constexpr int maxAttempts=28;
    for(int attempt=0;attempt<maxAttempts;++attempt)
    {
        const uint64_t entropy=static_cast<uint64_t>(juce::Random::getSystemRandom().nextInt64())
                             ^ static_cast<uint64_t>(juce::Time::getHighResolutionTicks())
                             ^ ((uint64_t)(attempt+1)*0xd1342543de82ef95ULL);
        const uint64_t candidateSeed=scrambleSongSeed((uint64_t)prompt.hashCode64()
                           ^ sessionSalt
                           ^ (++generationCounter*0x9e3779b97f4a7c15ULL)
                           ^ entropy
                           ^ ((uint64_t)attempt*0xa24baed4963ee407ULL)
                           ^ 0x534f4e475f465245ULL);

        auto candidate=std::make_shared<sonara::SongArrangement>();
        // Candidate generation deliberately stops before SoundDNA synthesis. The
        // full palette is generated exactly once, after the winning composition
        // plan has passed prompt, quality and whole-song novelty scoring.
        candidate->generateComposition(prompt,previewBpm,candidateSeed);
        auto fingerprint=melodyFingerprint(*candidate);
        auto harmony=candidate->getHarmonyFingerprint();
        auto bass=lanePatternFingerprint(*candidate,"BASS");
        auto drums=drumFingerprint(*candidate);
        auto pluck=lanePatternFingerprint(*candidate,"PLUCK");
        auto structure=candidate->getStructureFingerprint();

        float melodySim=previousFingerprint.empty()?0.f:fingerprintSimilarity(previousFingerprint,fingerprint);
        float rhythmSim=previousFingerprint.empty()?0.f:melodyRhythmSimilarity(previousFingerprint,fingerprint);
        for(const auto& historic:melodyHistory)
        {
            melodySim=juce::jmax(melodySim,fingerprintSimilarity(historic,fingerprint));
            rhythmSim=juce::jmax(rhythmSim,melodyRhythmSimilarity(historic,fingerprint));
        }

        const float harmonySim=maxFlatSimilarity(harmony,previousHarmony,harmonyHistory);
        const float bassSim=maxFlatSimilarity(bass,previousBass,bassHistory);
        const float drumSim=maxFlatSimilarity(drums,previousDrums,drumHistory);
        const float pluckSim=maxFlatSimilarity(pluck,previousPluck,pluckHistory);
        const float structureSim=maxFlatSimilarity(structure,previousStructure,structureHistory);
        const float wholeSim=juce::jlimit(0.f,1.f,
              harmonySim*.35f+melodySim*.25f+rhythmSim*.12f+bassSim*.10f
             +drumSim*.08f+pluckSim*.05f+structureSim*.05f);

        const float melodyQuality=melodyQualityScore(*candidate);
        const float harmonyQuality=harmonyQualityScore(*candidate);
        const float productionQuality=productionQualityScore(*candidate);
        const float quality=melodyQuality*.52f+harmonyQuality*.23f+productionQuality*.25f;
        const float promptMatch=promptCompositionMatch(prompt,*candidate);
        const float novelty=1.f-wholeSim;
        const float combined=quality*.40f+promptMatch*.30f+novelty*.30f;

        if(!fingerprint.empty()&&!harmony.empty()&&quality>=.56f&&combined>bestCombined)
        {
            bestCombined=combined;
            bestQuality=quality;
            bestPromptMatch=promptMatch;
            bestMelodySimilarity=melodySim;
            bestHarmonySimilarity=harmonySim;
            bestWholeSimilarity=wholeSim;
            bestSeed=candidateSeed;
            bestCandidate=candidate;
            bestFingerprint=fingerprint;
            bestHarmony=harmony;
            bestBass=bass;
            bestDrums=drums;
            bestPluck=pluck;
            bestStructure=structure;
        }

        generationProgress.store(.08f+.018f*attempt);

        // Harmony and melody both have hard identity gates. This prevents a tiny
        // lead reroll from hiding a recycled chord/root skeleton.
        if(!fingerprint.empty()&&!harmony.empty()&&melodySim<.72f&&harmonySim<.86f
           &&wholeSim<.62f&&quality>=.70f&&promptMatch>=.70f)
        {
            made=std::move(candidate);
            acceptedFingerprint=std::move(fingerprint);
            acceptedHarmony=std::move(harmony);
            acceptedBass=std::move(bass);
            acceptedDrums=std::move(drums);
            acceptedPluck=std::move(pluck);
            acceptedStructure=std::move(structure);
            seed=candidateSeed;
            melodySimilarityMax=melodySim;
            harmonySimilarityMax=harmonySim;
            wholeSimilarityMax=wholeSim;
            bestQuality=quality;
            bestPromptMatch=promptMatch;
            break;
        }

        generationStatus=(quality<.70f?"Rejecting weak composition":
                          (harmonySim>=.86f?"Rejecting familiar harmony":
                          (melodySim>=.72f?"Rejecting familiar melody":"Rejecting familiar whole song")))
                       +juce::String(" • trying composition ")
                       +juce::String(attempt+2)+"/"+juce::String(maxAttempts);
    }

    // If all 24 candidates miss the strict threshold, take the objectively most
    // different candidate rather than blindly accepting the final reroll.
    if(!made&&bestCandidate)
    {
        made=std::move(bestCandidate);
        acceptedFingerprint=std::move(bestFingerprint);
        acceptedHarmony=std::move(bestHarmony);
        acceptedBass=std::move(bestBass);
        acceptedDrums=std::move(bestDrums);
        acceptedPluck=std::move(bestPluck);
        acceptedStructure=std::move(bestStructure);
        seed=bestSeed;
        melodySimilarityMax=bestMelodySimilarity;
        harmonySimilarityMax=bestHarmonySimilarity;
        wholeSimilarityMax=bestWholeSimilarity;
    }

    if(!made)
    {
        generationProgress.store(0.f);
        generationStatus="Song generation failed to create a valid melody";
        return;
    }

    if(!acceptedFingerprint.empty()&&!acceptedHarmony.empty())
    {
        melodyHistory.push_back(acceptedFingerprint);
        harmonyHistory.push_back(acceptedHarmony);
        bassHistory.push_back(acceptedBass);
        drumHistory.push_back(acceptedDrums);
        pluckHistory.push_back(acceptedPluck);
        structureHistory.push_back(acceptedStructure);
        const auto trim=[](auto& history){while(history.size()>10)history.pop_front();};
        trim(melodyHistory);trim(harmonyHistory);trim(bassHistory);
        trim(drumHistory);trim(pluckHistory);trim(structureHistory);
    }

    made->finalizeSoundPalette();
    lastSongSeed.store(seed,std::memory_order_relaxed);
    lastMelodyNovelty.store(juce::jlimit(0.f,1.f,1.f-melodySimilarityMax),std::memory_order_relaxed);
    lastHarmonyNovelty.store(juce::jlimit(0.f,1.f,1.f-harmonySimilarityMax),std::memory_order_relaxed);
    lastSongNovelty.store(juce::jlimit(0.f,1.f,1.f-wholeSimilarityMax),std::memory_order_relaxed);
    generationProgress.store(.66f);
    generationStatus="Loading fresh SoundDNA palette into the new composition";

    const auto& lanes=made->getLanes();
    if(lanes.size()>=4)drumSynth.configureKit(lanes[0].sound,lanes[1].sound,lanes[2].sound,lanes[3].sound);
    for(int i=0;i<musicalLaneCount;++i)
    {
        const int laneIndex=firstMusicalLane+i;
        if(juce::isPositiveAndBelow(laneIndex,(int)lanes.size()))
        {
            songEngines[(size_t)i].allNotesOff();
            songEngines[(size_t)i].setPatch(lanes[(size_t)laneIndex].sound);
        }
    }

    previewBpm=made->getBpm();
    std::atomic_store_explicit(&arrangement,std::shared_ptr<const sonara::SongArrangement>(made),std::memory_order_release);
    selectedLane.store(lanes.size()>9?9:0);
    generationProgress.store(1.f);
    generationStatus="NEW SONG READY • whole "+juce::String((1.f-wholeSimilarityMax)*100.f,0)
                   +"% • harmony "+juce::String((1.f-harmonySimilarityMax)*100.f,0)
                   +"% • melody "+juce::String((1.f-melodySimilarityMax)*100.f,0)
                   +"% • quality "+juce::String(bestQuality*100.f,0)
                   +"% • prompt "+juce::String(bestPromptMatch*100.f,0)+"%";
}

void SonaraAudioProcessor::randomizeEverything(const juce::String& prompt)
{
    locks={};
    auto q=prompt.trim();
    auto lower=q.toLowerCase();

    const bool hasBpm=lower.contains(" bpm");
    const bool hasKey=lower.contains(" minor")||lower.contains(" major");

    auto& rng=juce::Random::getSystemRandom();
    static constexpr int bpms[]={122,124,126,128,130,132,136,140,150,174};
    static constexpr const char* keys[]={"C minor","D minor","E minor","F minor","G minor","A minor","C major","D major","G major","A major"};

    if(!hasBpm)q+=", "+juce::String(bpms[rng.nextInt((int)(sizeof(bpms)/sizeof(bpms[0])))])+" BPM";
    if(!hasKey)q+=", "+juce::String(keys[rng.nextInt((int)(sizeof(keys)/sizeof(keys[0])))]);
    q+=", completely fresh composition, new harmony, new groove, new melody contour, new drum kit, new bass and sub";

    const juce::String soundBrief=q+" • completely new standalone signature synth, unique oscillator character, polished transient, controlled low end";
    generatePatch(soundBrief);
    generateTrack(q);
    generationStatus="EVERYTHING RANDOMIZED • new song + melody + structure + drums + bass + sub + SoundDNA";
}


juce::String SonaraAudioProcessor::makeSurprisePrompt()
{
    static constexpr const char* genres[]={
        "emotional progressive house","future rave","melodic EDM pop","dark tech house",
        "drum and bass","electro house","cinematic EDM","tropical progressive house",
        "festival trance EDM","experimental melodic bass"
    };
    static constexpr const char* moods[]={
        "euphoric and emotional","dark and mysterious","uplifting and nostalgic","aggressive and futuristic",
        "dreamy and cinematic","melancholic but energetic","warm and hopeful","tense then explosive"
    };
    static constexpr const char* keys[]={
        "F minor","D minor","A minor","C minor","G minor","E minor","A major","D major","G major","C major"
    };
    static constexpr int bpms[]={122,124,126,128,130,132,138,140,150,174};
    static constexpr const char* hooks[]={
        "huge memorable lead hook","short addictive synth hook","anthemic octave melody","syncopated pluck hook",
        "wide emotional supersaw theme","minimal dark vocal-like synth hook","cinematic call-and-response melody"
    };
    static constexpr const char* arrangements[]={
        "short intro, verse, build, huge drop, breakdown, chorus, second build, final hook",
        "fast intro, early drop, breakdown, melodic chorus, harder final hook",
        "atmospheric intro, restrained verse, rising build, euphoric drop, emotional breakdown, massive final hook",
        "club intro, groove verse, tension build, punchy drop, hook chorus, stripped break, explosive ending"
    };

    auto& r=juce::Random::getSystemRandom();
    const auto pick=[&](const auto& arr)->juce::String{
        constexpr int n=(int)(sizeof(arr)/sizeof(arr[0]));
        return juce::String(arr[r.nextInt(n)]);
    };

    const juce::String genre=pick(genres);
    const juce::String mood=pick(moods);
    const juce::String key=pick(keys);
    const int bpm=bpms[r.nextInt((int)(sizeof(bpms)/sizeof(bpms[0])))];
    const juce::String hook=pick(hooks);
    const juce::String arrangement=pick(arrangements);

    return genre+", "+juce::String(bpm)+" BPM, "+key+", "+mood+", "+hook+
           ", completely fresh chord progression and bass groove, distinct drums, "+arrangement;
}

void SonaraAudioProcessor::regenerateDrums(const juce::String& prompt)
{
    stopSongPreview();
    generationProgress.store(.08f);
    generationStatus = "Understanding drum prompt only";

    const uint64_t seed = (uint64_t) prompt.hashCode64()
                        ^ sessionSalt
                        ^ (++generationCounter * 0xa24baed4963ee407ULL)
                        ^ 0x4452554d535f4f4eULL;

    auto fresh = std::make_shared<sonara::SongArrangement>();
    fresh->generate(prompt + " drums only tight punchy fills transitions", previewBpm, seed);

    auto current = arrangementSnapshot();
    auto updated = current
        ? std::make_shared<sonara::SongArrangement>(*current)
        : std::make_shared<sonara::SongArrangement>(*fresh);

    auto& dst = updated->editLanes();
    const auto& src = fresh->getLanes();
    const int drumLanes = juce::jmin(4, juce::jmin((int)dst.size(), (int)src.size()));
    for (int i = 0; i < drumLanes; ++i)
        dst[(size_t)i] = src[(size_t)i];
    if(dst.size()>=4) drumSynth.configureKit(dst[0].sound,dst[1].sound,dst[2].sound,dst[3].sound);

    std::atomic_store_explicit(&arrangement,
        std::shared_ptr<const sonara::SongArrangement>(updated),
        std::memory_order_release);
    selectedLane.store(0);
    generationProgress.store(1.f);
    generationStatus = current
        ? "Drums regenerated only • melody, chords and bass preserved"
        : "Drums ready • base arrangement created because no song existed";
}

void SonaraAudioProcessor::startSongPreview()
{
    startSongPreviewAtBar(0);
}

void SonaraAudioProcessor::startSongPreviewAtBar(int bar)
{
    auto a=arrangementSnapshot();
    if(!a||a->isEmpty()){generationStatus="Generate a full track first";return;}

    stopPreview();
    const auto& lanes=a->getLanes();
    if(lanes.size()>=4)drumSynth.configureKit(lanes[0].sound,lanes[1].sound,lanes[2].sound,lanes[3].sound);

    const int safeBar=juce::jlimit(0,juce::jmax(0,a->getBars()-1),bar);
    const double spb=previewSampleRate*60.0/a->getBpm();
    const int64_t start=(int64_t)std::llround((double)safeBar*sonara::SongArrangement::beatsPerBar*spb);

    drumSynth.reset();songReverb.reset();songDuckState=0.f;
    for(auto& e:songEngines)e.allNotesOff();
    for(auto& x:laneHpX)x.fill(0.f);
    for(auto& y:laneHpY)y.fill(0.f);
    for(auto& x:laneLpState)x.fill(0.f);
    masterHpX.fill(0.f);masterHpY.fill(0.f);
    if(songFxBus.getNumSamples()>0)songFxBus.clear();
    songFadeRemaining.store(128,std::memory_order_release);

    songSample.store(start);
    songPlaying.store(true);

    juce::String section="SONG";
    for(const auto& s:a->getSections())
        if(safeBar>=s.startBar&&safeBar<s.startBar+s.bars){section=s.name;break;}
    generationStatus="Playing "+section+" • bar "+juce::String(safeBar+1);
}

void SonaraAudioProcessor::pauseSongPreview()
{
    if(!songPlaying.exchange(false))return;
    drumSynth.reset();songReverb.reset();songDuckState=0.f;
    for(auto& e:songEngines)e.allNotesOff();
    songFadeRemaining.store(0,std::memory_order_release);
    generationStatus="Song preview paused • bar "+juce::String(currentSongBar()+1);
}

void SonaraAudioProcessor::resumeSongPreview()
{
    auto a=arrangementSnapshot();
    if(!a||a->isEmpty()){generationStatus="Generate a full track first";return;}

    const double spb=previewSampleRate*60.0/a->getBpm();
    const int64_t total=(int64_t)std::llround(a->getTotalBeats()*spb);
    auto pos=songSample.load();
    if(pos<=0||pos>=total){startSongPreviewAtBar(0);return;}

    const auto& lanes=a->getLanes();
    if(lanes.size()>=4)drumSynth.configureKit(lanes[0].sound,lanes[1].sound,lanes[2].sound,lanes[3].sound);
    drumSynth.reset();songReverb.reset();songDuckState=0.f;
    for(auto& e:songEngines)e.allNotesOff();
    for(auto& x:laneHpX)x.fill(0.f);for(auto& y:laneHpY)y.fill(0.f);for(auto& x:laneLpState)x.fill(0.f);
    masterHpX.fill(0.f);masterHpY.fill(0.f);
    songFadeRemaining.store(128,std::memory_order_release);
    songPlaying.store(true,std::memory_order_release);
    generationStatus="Song preview resumed • "+currentSectionName()+" • bar "+juce::String(currentSongBar()+1);
}

void SonaraAudioProcessor::stopSongPreview()
{
    songPlaying.store(false);songSample.store(0);drumSynth.reset();songReverb.reset();songDuckState=0.f;
    for(auto& e:songEngines)e.allNotesOff();
    for(auto& x:laneHpX)x.fill(0.f);
    for(auto& y:laneHpY)y.fill(0.f);
    for(auto& x:laneLpState)x.fill(0.f);
    masterHpX.fill(0.f);masterHpY.fill(0.f);
    songFadeRemaining.store(0,std::memory_order_release);
}

double SonaraAudioProcessor::songPosition01() const noexcept
{
    auto a=arrangementSnapshot();if(!a)return 0.0;
    const double samplesPerBeat=previewSampleRate*60.0/a->getBpm();
    const double total=juce::jmax(1.0,a->getTotalBeats()*samplesPerBeat);
    return juce::jlimit(0.0,1.0,(double)songSample.load()/total);
}

int SonaraAudioProcessor::currentSongBar() const noexcept
{
    auto a=arrangementSnapshot();if(!a)return 0;
    return juce::jlimit(0,juce::jmax(0,a->getBars()-1),
        (int)std::floor(songPosition01()*a->getBars()));
}

juce::String SonaraAudioProcessor::currentSectionName() const
{
    auto a=arrangementSnapshot();if(!a)return {};
    const int bar=currentSongBar();
    for(const auto& s:a->getSections())
        if(bar>=s.startBar&&bar<s.startBar+s.bars)return s.name;
    return {};
}

void SonaraAudioProcessor::injectSongLaneMidi(const sonara::ArrangementLane& lane, juce::MidiBuffer& dest, int64_t startSample, int numSamples, double bpm) noexcept
{
    dest.clear();
    const double spb = previewSampleRate * 60.0 / bpm;
    const double startBeat = (double)startSample / spb;
    const double endBeat = (double)(startSample + numSamples) / spb;
    auto it = std::lower_bound(lane.notes.begin(), lane.notes.end(), startBeat - 4.2,
                               [](const sonara::ArrangementNote& n, double beat){ return n.beat < beat; });
    for (; it != lane.notes.end() && it->beat <= endBeat; ++it)
    {
        const double onS = it->beat * spb, offS = (it->beat + it->length) * spb;
        if (onS >= startSample && onS < startSample + numSamples)
            dest.addEvent(juce::MidiMessage::noteOn(lane.midiChannel, it->note, (juce::uint8)it->velocity), (int)(onS - startSample));
        if (offS >= startSample && offS < startSample + numSamples)
            dest.addEvent(juce::MidiMessage::noteOff(lane.midiChannel, it->note), (int)(offS - startSample));
    }
}

int SonaraAudioProcessor::collectDrumTriggers(const sonara::SongArrangement& a, int64_t startSample, int numSamples) noexcept
{
    const auto& lanes = a.getLanes();
    const double spb = previewSampleRate * 60.0 / a.getBpm();
    const double startBeat = (double)startSample / spb;
    const double endBeat = (double)(startSample + numSamples) / spb;
    int count = 0;

    for (int laneIndex = 0; laneIndex < juce::jmin(firstMusicalLane, (int)lanes.size()); ++laneIndex)
    {
        const auto& lane = lanes[(size_t)laneIndex];
        auto it = std::lower_bound(lane.notes.begin(), lane.notes.end(), startBeat,
                                   [](const sonara::ArrangementNote& n, double beat){ return n.beat < beat; });
        for (; it != lane.notes.end() && it->beat < endBeat && count < (int)drumTriggers.size(); ++it)
        {
            const int offset = juce::jlimit(0, numSamples - 1, (int)std::llround(it->beat * spb - startSample));
            drumTriggers[(size_t)count++] = { offset, it->note, it->velocity / 127.f };
        }
    }

    std::sort(drumTriggers.begin(), drumTriggers.begin() + count,
              [](const sonara::DrumTrigger& x, const sonara::DrumTrigger& y){ return x.sampleOffset < y.sampleOffset; });
    return count;
}

void SonaraAudioProcessor::renderSongBlock(juce::AudioBuffer<float>& out,int numSamples)
{
    auto a=arrangementSnapshot();
    if(!a||numSamples<=0){songPlaying.store(false);return;}

    const int renderSamples=juce::jmin(numSamples,maximumBlockSize);
    const auto& lanes=a->getLanes();
    const int64_t start=songSample.load();
    const double spb=previewSampleRate*60.0/a->getBpm();
    const int64_t total=(int64_t)std::llround(a->getTotalBeats()*spb);

    out.clear();
    songFxBus.clear(0,renderSamples);

    // Build a lightweight kick-triggered sidechain envelope before musical lanes
    // are mixed. This gives festival drops room for the kick without running a
    // compressor per lane.
    const int drumCount=collectDrumTriggers(*a,start,renderSamples);
    if((int)songDuckEnvelope.size()<renderSamples)
        songDuckEnvelope.resize((size_t)renderSamples,0.f);
    const float duckRelease=std::exp(-1.f/(float)(previewSampleRate*.18));
    float duck=songDuckState;
    int duckTriggerIndex=0;
    for(int s=0;s<renderSamples;++s)
    {
        while(duckTriggerIndex<drumCount&&drumTriggers[(size_t)duckTriggerIndex].sampleOffset<=s)
        {
            const auto& trigger=drumTriggers[(size_t)duckTriggerIndex];
            if(trigger.midiNote==36)
                duck=juce::jmax(duck,.65f+.35f*trigger.velocity);
            ++duckTriggerIndex;
        }
        songDuckEnvelope[(size_t)s]=duck;
        duck*=duckRelease;
    }
    songDuckState=duck;

    // Lane order: BASS, SUB, CHORDS, PLUCK, PAD, LEAD, COUNTER, FX.
    static constexpr float laneGain[musicalLaneCount]={.60f,.38f,.36f,.32f,.26f,.58f,.27f,.18f};
    static constexpr float hpHz[musicalLaneCount]={28.f,18.f,120.f,125.f,160.f,120.f,150.f,110.f};
    static constexpr float fxSend[musicalLaneCount]={0.f,0.f,.14f,.10f,.18f,.12f,.08f,.15f};

    for(int i=0;i<musicalLaneCount;++i)
    {
        const int laneIndex=firstMusicalLane+i;
        if(!juce::isPositiveAndBelow(laneIndex,(int)lanes.size()))continue;

        auto& midi=songMidi[(size_t)i];
        auto& scratch=songScratch[(size_t)i];
        injectSongLaneMidi(lanes[(size_t)laneIndex],midi,start,renderSamples,a->getBpm());

        const bool active=songEngines[(size_t)i].hasActiveVoices();
        if(midi.isEmpty()&&!active)continue;

        scratch.clear(0,renderSamples);
        songEngines[(size_t)i].render(scratch,midi,renderSamples);

        const float rc=1.f/(juce::MathConstants<float>::twoPi*hpHz[i]);
        const float dt=1.f/(float)previewSampleRate;
        const float hpAlpha=rc/(rc+dt);

        for(int ch=0;ch<scratch.getNumChannels()&&ch<2;++ch)
        {
            auto* d=scratch.getWritePointer(ch);
            float x1=laneHpX[(size_t)i][(size_t)ch];
            float y1=laneHpY[(size_t)i][(size_t)ch];
            for(int s=0;s<renderSamples;++s)
            {
                const float x=std::isfinite(d[s])?d[s]:0.f;
                const float y=hpAlpha*(y1+x-x1);
                x1=x;y1=y;d[s]=y;
            }
            laneHpX[(size_t)i][(size_t)ch]=x1;
            laneHpY[(size_t)i][(size_t)ch]=y1;
        }

        const auto mix=getLaneMix(laneIndex);

        // BASS low end stays near-mono.
        if(i==0&&scratch.getNumChannels()>=2)
        {
            auto* l=scratch.getWritePointer(0);
            auto* r=scratch.getWritePointer(1);
            for(int s=0;s<renderSamples;++s)
            {
                const float mid=.5f*(l[s]+r[s]);
                l[s]=mid*.88f+l[s]*.12f;
                r[s]=mid*.88f+r[s]*.12f;
            }
        }

        // SUB is mono and low-passed. It never enters the shared reverb bus.
        if(i==1)
        {
            const float lpRc=1.f/(juce::MathConstants<float>::twoPi*125.f);
            const float lpA=dt/(lpRc+dt);
            auto* l=scratch.getWritePointer(0);
            auto* r=scratch.getNumChannels()>1?scratch.getWritePointer(1):l;
            float state=laneLpState[(size_t)i][0];
            for(int s=0;s<renderSamples;++s)
            {
                const float mono=.5f*(l[s]+r[s]);
                state+=lpA*(mono-state);
                l[s]=state;
                r[s]=state;
            }
            laneLpState[(size_t)i][0]=state;
            laneLpState[(size_t)i][1]=state;
        }

        if(scratch.getNumChannels()>=2)
        {
            auto* l=scratch.getWritePointer(0);
            auto* r=scratch.getWritePointer(1);
            const float requestedWidth=juce::jlimit(0.f,1.5f,mix.width);
            const float width=i==1?0.f:(i==0?juce::jmin(.25f,requestedWidth):requestedWidth);
            const float pan=juce::jlimit(-1.f,1.f,mix.pan);
            const float panL=pan>0.f?1.f-pan:1.f;
            const float panR=pan<0.f?1.f+pan:1.f;
            for(int s=0;s<renderSamples;++s)
            {
                const float mid=.5f*(l[s]+r[s]);
                const float side=.5f*(l[s]-r[s])*width;
                l[s]=(mid+side)*panL;
                r[s]=(mid-side)*panR;
            }
        }

        // Duck only the layers that mask a festival kick. Lead/pluck/counter/FX
        // stay forward, while BASS/SUB duck most and CHORDS/PAD more gently.
        const float duckDepth=i==1?.54f:(i==0?.46f:(i==2?.28f:(i==4?.20f:0.f)));
        if(duckDepth>0.f)
        {
            for(int ch=0;ch<scratch.getNumChannels();++ch)
            {
                auto* d=scratch.getWritePointer(ch);
                for(int s=0;s<renderSamples;++s)
                    d[s]*=1.f-duckDepth*songDuckEnvelope[(size_t)s];
            }
        }

        const float mixedGain=laneGain[i]*mix.level;
        const float sendGain=fxSend[i]*mix.fxSend;
        for(int ch=0;ch<out.getNumChannels();++ch)
        {
            out.addFrom(ch,0,scratch,ch,0,renderSamples,mixedGain);
            if(sendGain>0.f)
                songFxBus.addFrom(ch,0,scratch,ch,0,renderSamples,mixedGain*sendGain);
        }
    }

    drumSynth.render(out,drumTriggers.data(),drumCount);

    // One shared wet-only reverb bus; drums/BASS/SUB stay dry.
    if(songFxBus.getNumChannels()>=2)
        songReverb.processStereo(songFxBus.getWritePointer(0),songFxBus.getWritePointer(1),renderSamples);
    else if(songFxBus.getNumChannels()==1)
        songReverb.processMono(songFxBus.getWritePointer(0),renderSamples);
    for(int ch=0;ch<out.getNumChannels();++ch)
        out.addFrom(ch,0,songFxBus,ch,0,renderSamples,.72f);

    const float masterRc=1.f/(juce::MathConstants<float>::twoPi*24.f);
    const float masterDt=1.f/(float)previewSampleRate;
    const float masterAlpha=masterRc/(masterRc+masterDt);

    const int fadeStart=songFadeRemaining.load(std::memory_order_acquire);
    for(int ch=0;ch<out.getNumChannels()&&ch<2;++ch)
    {
        auto* d=out.getWritePointer(ch);
        float x1=masterHpX[(size_t)ch],y1=masterHpY[(size_t)ch];
        for(int s=0;s<renderSamples;++s)
        {
            const float x=std::isfinite(d[s])?d[s]:0.f;
            const float hp=masterAlpha*(y1+x-x1);
            x1=x;y1=hp;
            float y=juce::jlimit(-.95f,.95f,std::tanh(hp*.82f));
            if(fadeStart>s)
            {
                const int remaining=fadeStart-s;
                y*=juce::jlimit(0.f,1.f,(128.f-(float)remaining)/128.f);
            }
            d[s]=y;
        }
        masterHpX[(size_t)ch]=x1;masterHpY[(size_t)ch]=y1;
    }
    songFadeRemaining.store(juce::jmax(0,fadeStart-renderSamples),std::memory_order_release);

    const int64_t next=start+renderSamples;
    songSample.store(next);
    if(next>=total)
    {
        songPlaying.store(false);songSample.store(total);
        for(auto& e:songEngines)e.allNotesOff();
    }
}
bool SonaraAudioProcessor::writeArrangementMidiFile(const juce::File& destination) const
{
    auto a = arrangementSnapshot(); return a ? a->writeMidiFile(destination) : false;
}

bool SonaraAudioProcessor::writeSelectedLaneMidiFile(const juce::File& destination) const
{
    auto a = arrangementSnapshot(); if (!a) return false;
    const auto& lanes = a->getLanes(); const int laneIndex = selectedLane.load();
    if (!juce::isPositiveAndBelow(laneIndex, (int)lanes.size())) return false;
    const auto& lane = lanes[(size_t)laneIndex];

    juce::MidiFile mf; mf.setTicksPerQuarterNote(960); juce::MidiMessageSequence seq;
    auto tempo = juce::MidiMessage::tempoMetaEvent((int)std::llround(60000000.0 / a->getBpm())); tempo.setTimeStamp(0); seq.addEvent(tempo);
    auto name = juce::MidiMessage::textMetaEvent(3, lane.name); name.setTimeStamp(0); seq.addEvent(name);
    for (const auto& n : lane.notes)
    {
        auto on=juce::MidiMessage::noteOn(lane.midiChannel,n.note,(juce::uint8)n.velocity); on.setTimeStamp(n.beat*960.0); seq.addEvent(on);
        auto off=juce::MidiMessage::noteOff(lane.midiChannel,n.note); off.setTimeStamp((n.beat+n.length)*960.0); seq.addEvent(off);
    }
    seq.updateMatchedPairs(); mf.addTrack(seq); destination.deleteFile(); juce::FileOutputStream out(destination);
    return out.openedOk() && mf.writeTo(out);
}


bool SonaraAudioProcessor::analyseReferenceFile(const juce::File& file)
{
    stopPreview(); stopSongPreview(); generationProgress.store(.05f); generationStatus="Analyzing reference audio";
    auto result=referenceAnalyzer.analyseAudio(file);
    if(!result.valid()){generationProgress.store(0.f);generationStatus="Reference analysis failed";return false;}
    reference=std::move(result);referenceLoaded=true;referenceMelodyPreview=false;previewBpm=juce::jlimit(60.0,200.0,reference.estimatedBpm);
    generationProgress.store(1.f);generationStatus="Reference ready • "+reference.keyName+" • "+juce::String(reference.melody.size())+" melody notes";return true;
}

bool SonaraAudioProcessor::importMidiFile(const juce::File& file)
{
    stopPreview(); stopSongPreview(); generationProgress.store(.1f); generationStatus="Importing MIDI";
    auto result=referenceAnalyzer.importMidi(file);
    if(result.melody.empty()){generationProgress.store(0.f);generationStatus="MIDI import failed";return false;}
    reference=std::move(result);referenceLoaded=true;referenceMelodyPreview=true;previewBpm=juce::jlimit(60.0,200.0,reference.estimatedBpm);
    generationProgress.store(1.f);generationStatus="MIDI imported • ready to RESOUND";return true;
}

void SonaraAudioProcessor::resoundReference(const juce::String& prompt)
{
    if(!referenceLoaded||reference.melody.empty()){generationStatus="Load a reference audio or MIDI first";return;}
    generatePatch(prompt+" resound instrument clean expressive");referenceMelodyPreview=true;previewBpm=juce::jlimit(60.0,200.0,reference.estimatedBpm);startPreview();generationStatus="RESOUND • generated SoundDNA playing extracted melody";
}

void SonaraAudioProcessor::rebuildInstrumentalFromReference(const juce::String& prompt)
{
    if(!referenceLoaded||reference.melody.empty()){generationStatus="Load a reference audio or MIDI first";return;}
    previewBpm=juce::jlimit(60.0,200.0,reference.estimatedBpm);
    generateTrack(prompt+" "+reference.keyName+" rebuild instrumental from reference melody");
    auto current=arrangementSnapshot();if(!current)return;auto rebuilt=std::make_shared<sonara::SongArrangement>(*current);auto& lanes=rebuilt->editLanes();
    int leadIndex=-1;for(int i=0;i<(int)lanes.size();++i)if(lanes[(size_t)i].name=="LEAD"){leadIndex=i;break;}if(leadIndex<0)return;
    auto& lead=lanes[(size_t)leadIndex];lead.notes.clear();const double loop=juce::jmax(4.0,reference.melodyBeats());const double total=rebuilt->getTotalBeats();
    for(double offset=0.0;offset<total;offset+=loop)for(const auto& n:reference.melody){const double beat=offset+n.beat;if(beat>=total)break;lead.notes.push_back({n.midiNote,n.velocity,beat,juce::jmin(n.length,total-beat)});}std::sort(lead.notes.begin(),lead.notes.end(),[](const sonara::ArrangementNote&a,const sonara::ArrangementNote&b){return a.beat<b.beat;});
    lead.sound=generator.generate(prompt+" emotional lead resounded from reference",(uint64_t)prompt.hashCode64()^(++generationCounter*0x94d049bb133111ebULL));
    for(int i=0;i<musicalLaneCount;++i){const int laneIndex=firstMusicalLane+i;if(juce::isPositiveAndBelow(laneIndex,(int)lanes.size()))songEngines[(size_t)i].setPatch(lanes[(size_t)laneIndex].sound);}std::atomic_store_explicit(&arrangement,std::shared_ptr<const sonara::SongArrangement>(rebuilt),std::memory_order_release);selectedLane.store(leadIndex);generationProgress.store(1.f);generationStatus="Reference instrumental rebuilt • new drums/bass/chords/synths + extracted melody";
}

bool SonaraAudioProcessor::writeReferenceMidiFile(const juce::File& file) const
{
    return referenceLoaded&&referenceAnalyzer.writeMelodyMidi(reference,file);
}

bool SonaraAudioProcessor::saveSound(const juce::File& file) const
{
    auto xml=engine.patch().toValueTree().createXml();return xml&&file.replaceWithText(xml->toString());
}

bool SonaraAudioProcessor::loadSound(const juce::File& file)
{
    auto xml=juce::XmlDocument::parse(file);if(!xml)return false;auto tree=juce::ValueTree::fromXml(*xml);if(!tree.isValid()||tree.getType().toString()!="SoundDNA")return false;setPatchWithHistory(sonara::SoundDNA::fromValueTree(tree));referenceMelodyPreview=false;generationStatus="Sound loaded";return true;
}

bool SonaraAudioProcessor::saveProject(const juce::File& file) const
{
    juce::ValueTree root("SONARA_PROJECT");
    root.setProperty("schema",2,nullptr);
    root.setProperty("bpm",previewBpm,nullptr);
    root.addChild(engine.patch().toValueTree(),-1,nullptr);
    root.addChild(locks.toValueTree(),-1,nullptr);
    root.addChild(makeLaneMixTree(*this),-1,nullptr);
    if(auto a=arrangementSnapshot())root.addChild(a->toValueTree(),-1,nullptr);
    auto xml=root.createXml();
    return xml&&file.replaceWithText(xml->toString());
}
bool SonaraAudioProcessor::loadProject(const juce::File& file)
{
    auto xml=juce::XmlDocument::parse(file);
    if(!xml)return false;
    auto root=juce::ValueTree::fromXml(*xml);
    if(!root.isValid()||root.getType().toString()!="SONARA_PROJECT")return false;

    auto dna=root.getChildWithName("SoundDNA");
    if(dna.isValid())setPatchWithHistory(sonara::SoundDNA::fromValueTree(dna));
    auto lockTree=root.getChildWithName("MUTATION_LOCKS");
    if(lockTree.isValid())locks=sonara::MutationLocks::fromValueTree(lockTree);
    applyLaneMixTree(*this,root.getChildWithName("LANE_MIX"));
    previewBpm=juce::jlimit(60.0,200.0,(double)root.getProperty("bpm",128.0));

    auto arr=root.getChildWithName("SONARA_ARRANGEMENT");
    if(arr.isValid())
    {
        auto made=std::make_shared<sonara::SongArrangement>(sonara::SongArrangement::fromValueTree(arr));
        const auto& lanes=made->getLanes();
        for(int i=0;i<musicalLaneCount;++i)
        {
            const int laneIndex=firstMusicalLane+i;
            if(juce::isPositiveAndBelow(laneIndex,(int)lanes.size()))
                songEngines[(size_t)i].setPatch(lanes[(size_t)laneIndex].sound);
        }
        std::atomic_store_explicit(&arrangement,std::shared_ptr<const sonara::SongArrangement>(made),std::memory_order_release);
    }

    generationStatus="Project loaded";
    generationProgress.store(1.f);
    return true;
}
bool SonaraAudioProcessor::exportFullMix(const juce::File& file)
{
    auto a=arrangementSnapshot();
    if(!a){generationStatus="Generate a track first";return false;}

    generationStatus="Rendering 24-bit full mix";
    generationProgress.store(.01f);
    sonara::AudioExporter::MixArray mix{};
    for(int i=0;i<12;++i)
    {
        const auto s=getLaneMix(i);
        mix[(size_t)i]={s.level,s.pan,s.width,s.fxSend};
    }

    const bool ok=audioExporter.renderFullMix(
        *a,file,44100.0,
        [this](float x,const juce::String&s){generationProgress.store(x);generationStatus=s;},
        &mix);

    generationProgress.store(ok?1.f:0.f);
    generationStatus=ok?"24-bit full mix ready • "+file.getFullPathName():"Full mix export failed";
    return ok;
}

bool SonaraAudioProcessor::exportSelectedLaneAudio(const juce::File& file)
{
    auto a=arrangementSnapshot();if(!a)return false;const bool ok=audioExporter.renderSelectedLane(*a,selectedLane.load(),file,44100.0,[this](float x,const juce::String&s){generationProgress.store(x);generationStatus=s;});generationProgress.store(ok?1.f:0.f);generationStatus=ok?"Selected lane WAV ready • "+file.getFullPathName():"Lane export failed";return ok;
}

bool SonaraAudioProcessor::exportAllStems(const juce::File& directory)
{
    auto a=arrangementSnapshot();if(!a)return false;const bool ok=audioExporter.renderAllStems(*a,directory,44100.0,[this](float x,const juce::String&s){generationProgress.store(x);generationStatus=s;});generationProgress.store(ok?1.f:0.f);generationStatus=ok?"All 24-bit stems ready • "+directory.getFullPathName():"Stem export failed";return ok;
}

void SonaraAudioProcessor::setLaneMix(int laneIndex,LaneMixParameter parameter,float value) noexcept
{
    if(!juce::isPositiveAndBelow(laneIndex,12))return;
    const size_t i=(size_t)laneIndex;
    switch(parameter)
    {
        case LaneMixParameter::level:
            laneMixLevel[i].store(juce::jlimit(0.f,1.5f,value),std::memory_order_relaxed);
            break;
        case LaneMixParameter::pan:
            laneMixPan[i].store(laneIndex==5?0.f:juce::jlimit(-1.f,1.f,value),std::memory_order_relaxed);
            break;
        case LaneMixParameter::width:
            laneMixWidth[i].store(laneIndex==5?0.f:juce::jlimit(0.f,1.5f,value),std::memory_order_relaxed);
            break;
        case LaneMixParameter::fxSend:
            laneMixFx[i].store(laneIndex==5?0.f:juce::jlimit(0.f,1.5f,value),std::memory_order_relaxed);
            break;
    }
}

SonaraAudioProcessor::LaneMixState SonaraAudioProcessor::getLaneMix(int laneIndex) const noexcept
{
    LaneMixState state;
    if(!juce::isPositiveAndBelow(laneIndex,12))return state;
    const size_t i=(size_t)laneIndex;
    state.level=laneMixLevel[i].load(std::memory_order_relaxed);
    state.pan=laneMixPan[i].load(std::memory_order_relaxed);
    state.width=laneMixWidth[i].load(std::memory_order_relaxed);
    state.fxSend=laneMixFx[i].load(std::memory_order_relaxed);
    return state;
}

void SonaraAudioProcessor::setMacro(Macro macro, float normalized)
{
    const float x = juce::jlimit(0.f,1.f,normalized); auto d = engine.patch();
    switch(macro)
    {
        case Macro::brightness: d.macroBrightness=x; d.cutoff=juce::jlimit(30.f,22000.f,80.f*std::pow(250.f,x)); break;
        case Macro::movement: d.macroMovement=x; d.lfoRate=.08f+7.92f*x*x; d.lfoCutoff=(x-.5f)*1.5f; d.lfoMorphA=(x-.5f)*1.4f; d.lfoMorphB=-(x-.5f)*1.2f; break;
        case Macro::space: d.macroSpace=x; d.chorus=juce::jlimit(0.f,1.f,x*.75f); d.reverb=juce::jlimit(0.f,1.f,x*.85f); d.delay=juce::jlimit(0.f,1.f,juce::jmax(0.f,(x-.18f)*.9f)); break;
        case Macro::impact: d.macroImpact=x; d.drive=juce::jlimit(0.f,1.f,.04f+x*.62f); d.subLevel=juce::jlimit(0.f,1.f,x*.52f); break;
    }
    engine.setPatch(d); generationStatus = "Macro adjusted";
}

juce::String SonaraAudioProcessor::exportProjectForCyanoryx() const
{
    auto a=arrangementSnapshot();
    return cyanoryx.serializeInterchange(engine.patch(), a ? a->toValueTree() : juce::ValueTree{}, previewBpm, referenceLoaded ? reference.keyName : juce::String("Unknown"));
}

bool SonaraAudioProcessor::importPatchFromCyanoryx(const juce::String& payload)
{
    sonara::SoundDNA imported; if(!cyanoryx.deserializePatch(payload,imported))return false;
    setPatchWithHistory(imported); generationProgress.store(1.f); generationStatus="Cyanoryx patch received"; return true;
}

void SonaraAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::ValueTree state("SONARA_STATE"); state.setProperty("schema",5,nullptr); state.setProperty("generationCounter",juce::String(generationCounter),nullptr); state.setProperty("bpm",previewBpm,nullptr);
    state.addChild(engine.patch().toValueTree(),-1,nullptr); state.addChild(locks.toValueTree(),-1,nullptr); state.addChild(makeLaneMixTree(*this),-1,nullptr); if(auto a=arrangementSnapshot()) state.addChild(a->toValueTree(),-1,nullptr);
    if(auto xml=state.createXml())copyXmlToBinary(*xml,dest);
}

void SonaraAudioProcessor::setStateInformation(const void* data,int bytes)
{
    if(data==nullptr||bytes<=0)return;
    if(auto xml=getXmlFromBinary(data,bytes))
    {
        const auto state=juce::ValueTree::fromXml(*xml); if(!state.isValid())return;
        if(state.getType().toString()=="SoundDNA"){setPatchWithHistory(sonara::SoundDNA::fromValueTree(state));locks={};generationCounter=1;generationStatus="Legacy preset restored";return;}
        if(state.getType().toString()!="SONARA_STATE")return;
        const auto dna=state.getChildWithName("SoundDNA"); if(!dna.isValid())return;
        engine.setPatch(sonara::SoundDNA::fromValueTree(dna));
        const auto lockState=state.getChildWithName("MUTATION_LOCKS"); locks=lockState.isValid()?sonara::MutationLocks::fromValueTree(lockState):sonara::MutationLocks{};
        applyLaneMixTree(*this,state.getChildWithName("LANE_MIX"));
        generationCounter=(uint64_t)juce::jmax<juce::int64>(1,state.getProperty("generationCounter","1").toString().getLargeIntValue());
        previewBpm=juce::jlimit(60.0,200.0,(double)state.getProperty("bpm",128.0));
        auto arr=state.getChildWithName("SONARA_ARRANGEMENT");if(arr.isValid()){auto made=std::make_shared<sonara::SongArrangement>(sonara::SongArrangement::fromValueTree(arr));const auto& lanes=made->getLanes();for(int i=0;i<musicalLaneCount;++i){const int laneIndex=firstMusicalLane+i;if(juce::isPositiveAndBelow(laneIndex,(int)lanes.size()))songEngines[(size_t)i].setPatch(lanes[(size_t)laneIndex].sound);}std::atomic_store_explicit(&arrangement,std::shared_ptr<const sonara::SongArrangement>(made),std::memory_order_release);}
        patchHistory.clear();patchHistory.push_back(engine.patch());historyIndex=0;generationProgress.store(1.f);generationStatus="Session restored";
    }
}

juce::AudioProcessorEditor* SonaraAudioProcessor::createEditor()
{
#ifndef SONARA_HEADLESS_TEST
    return new SonaraAudioProcessorEditor(*this);
#else
    return nullptr;
#endif
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new SonaraAudioProcessor();}
